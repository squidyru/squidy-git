// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "filespage.h"

#include "diffview.h"
#include "filepreview.h"
#include "flatcombobox.h"
#include "icons.h"
#include "pdfpreview.h"
#include "platform/platformservices.h"
#include "sourceview.h"
#include "syntaxhighlighter.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace {

constexpr int PathRole = Qt::UserRole + 1;
constexpr int IsDirectoryRole = Qt::UserRole + 2;
constexpr int LoadedRole = Qt::UserRole + 3;
constexpr int RevisionIndexRole = Qt::UserRole + 4;

constexpr int MaximumTimelineCount = 200;
constexpr qint64 MaximumViewerSize = 2 * 1024 * 1024;
constexpr qsizetype MaximumHighlightedSize = 512 * 1024;
constexpr qsizetype MaximumHexDumpSize = 64 * 1024;
constexpr int SplitterWidth = 0;
constexpr int FilterDelayMs = 250;
constexpr int MaximumMatchCount = 2000;

enum class ViewerMode {
    Changes,
    Contents,
    DifferenceFromCurrent
};

QString formatSize(const qint64 bytes) {
    if (bytes < 0) {
        return {};
    }
    if (bytes < 1024) {
        return FilesPage::tr("%n byte(s)", nullptr, static_cast<int>(bytes));
    }
    if (bytes < 1024 * 1024) {
        return FilesPage::tr("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    return FilesPage::tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QString formatMoment(const QDateTime &moment) {
    if (!moment.isValid()) {
        return {};
    }
    return QLocale::system().toString(moment.toLocalTime(),
                                      QStringLiteral("d MMM yyyy H:mm"));
}

QString changeDescription(const QChar status) {
    switch (status.unicode()) {
        case u'A': return FilesPage::tr("Added");
        case u'D': return FilesPage::tr("Deleted");
        case u'R': return FilesPage::tr("Renamed");
        case u'C': return FilesPage::tr("Copied");
        case u'T': return FilesPage::tr("Type changed");
        default: return FilesPage::tr("Modified");
    }
}

// Git itself treats a NUL byte near the start of a blob as the binary marker.
bool looksBinary(const QByteArray &content) {
    return content.first(qMin<qsizetype>(content.size(), 8000)).contains('\0');
}

bool looksLikePdf(const QByteArray &content) {
    return content.startsWith("%PDF-");
}

bool isObjectId(const QString &revision) {
    if (revision.size() != 40 && revision.size() != 64) {
        return false;
    }
    for (const QChar character : revision) {
        const ushort value = character.unicode();
        if (!((value >= u'0' && value <= u'9')
              || (value >= u'a' && value <= u'f')
              || (value >= u'A' && value <= u'F'))) {
            return false;
        }
    }
    return true;
}

}

/// Shows a picture centred and scaled down to fit, never blown up: a 16 by 16
/// icon should look like one.
class ImagePreview final : public QWidget {
public:
    explicit ImagePreview(QWidget *parent = nullptr)
        : QWidget(parent) {
    }

    void setImage(const QImage &image) {
        image_ = image;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        const ThemePalette &palette = Theme::instance()->palette();
        painter.fillRect(rect(), palette.surface);
        if (image_.isNull()) {
            return;
        }

        const int margin = 16;
        const QSize available(width() - margin, height() - margin);
        if (available.isEmpty()) {
            return;
        }
        QSize target = image_.size();
        if (target.width() > available.width() || target.height() > available.height()) {
            target.scale(available, Qt::KeepAspectRatio);
        }

        const QRect box(QPoint((width() - target.width()) / 2,
                               (height() - target.height()) / 2),
                        target);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(box, image_);
        painter.setPen(palette.border);
        painter.drawRect(box.adjusted(-1, -1, 0, 0));
    }

private:
    QImage image_;
};

FilesPage::FilesPage(const QString &repositoryRoot, QWidget *parent)
    : QWidget(parent),
      treeWatcher_(new QFutureWatcher<FileTreeLoad>(this)),
      listingWatcher_(new QFutureWatcher<FileListingLoad>(this)),
      historyWatcher_(new QFutureWatcher<FileHistoryLoad>(this)),
      viewerWatcher_(new QFutureWatcher<FileViewerLoad>(this)) {
    setObjectName(QStringLiteral("filesPage"));
    const bool opened = git_.openRepository(repositoryRoot).succeeded();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildToolRow());

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(SplitterWidth);
    splitter->addWidget(buildTree());

    auto *rightSplitter = new QSplitter(Qt::Vertical);
    rightSplitter->setHandleWidth(SplitterWidth);
    rightSplitter->addWidget(buildTimeline());
    rightSplitter->addWidget(buildViewer());
    rightSplitter->setStretchFactor(0, 2);
    rightSplitter->setStretchFactor(1, 3);
    rightSplitter->setSizes({240, 340});

    splitter->addWidget(rightSplitter);
    splitter->setCollapsible(0, false);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({320, 940});
    layout->addWidget(splitter, 1);

    connect(treeWatcher_, &QFutureWatcher<FileTreeLoad>::finished, this, [this] {
        applyDirectory(treeWatcher_->result());
        startNextDirectory();
    });
    connect(listingWatcher_, &QFutureWatcher<FileListingLoad>::finished, this,
            [this] { applyListing(listingWatcher_->result()); });
    connect(historyWatcher_, &QFutureWatcher<FileHistoryLoad>::finished, this,
            [this] { applyHistory(historyWatcher_->result()); });
    connect(viewerWatcher_, &QFutureWatcher<FileViewerLoad>::finished, this,
            [this] { applyViewer(viewerWatcher_->result()); });

    if (opened) {
        reloadTree();
    }
}

QWidget *FilesPage::buildToolRow() {
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(4, 0, 4, 2);
    layout->setSpacing(8);

    auto *label = new QLabel(tr("Revision:"));
    revision_ = new FlatComboBox;
    revision_->setObjectName(QStringLiteral("fileRevision"));
    revision_->setMinimumWidth(220);
    revision_->addItem(tr("Working copy"), QString());
    revision_->addItem(tr("HEAD"), QStringLiteral("HEAD"));

    connect(revision_, &QComboBox::currentIndexChanged, this, [this] {
        // The path is kept so the same file reopens at the new revision.
        pendingPath_ = currentPath_;
        reloadTree();
    });

    fileFilter_ = new QLineEdit;
    fileFilter_->setObjectName(QStringLiteral("fileNameFilter"));
    fileFilter_->setPlaceholderText(tr("File name"));
    fileFilter_->setClearButtonEnabled(true);
    fileFilter_->setFixedWidth(200);
    fileFilter_->addAction(Icons::icon(Icons::Glyph::Search,
                                       Theme::instance()->palette().mutedText),
                           QLineEdit::TrailingPosition);

    filterTimer_ = new QTimer(this);
    filterTimer_->setSingleShot(true);
    filterTimer_->setInterval(FilterDelayMs);
    connect(filterTimer_, &QTimer::timeout, this, [this] { runFilter(); });
    connect(fileFilter_, &QLineEdit::textChanged, this,
            [this] { filterTimer_->start(); });

    layout->addWidget(label);
    layout->addWidget(revision_);
    layout->addStretch(1);
    layout->addWidget(fileFilter_);
    return row;
}

QWidget *FilesPage::buildTree() {
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    treeCaption_ = new QLabel(tr("REPOSITORY"));
    treeCaption_->setObjectName(QStringLiteral("sectionCaption"));
    treeCaption_->setContentsMargins(6, 2, 0, 0);
    layout->addWidget(treeCaption_);

    tree_ = new QTreeWidget;
    tree_->setObjectName(QStringLiteral("fileTree"));
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("Name"), tr("Size")});
    tree_->setUniformRowHeights(true);
    tree_->setAlternatingRowColors(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setIconSize(QSize(16, 16));
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    tree_->header()->resizeSection(1, 84);
    layout->addWidget(tree_, 1);

    connect(tree_, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) { expandDirectory(item); });
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item) { selectTreeFile(item); });
    connect(tree_, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &position) { showTreeContextMenu(position); });

    return panel;
}

QWidget *FilesPage::buildTimeline() {
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    timelineCaption_ = new QLabel(tr("Select a file to see its history"));
    timelineCaption_->setObjectName(QStringLiteral("sectionCaption"));
    timelineCaption_->setContentsMargins(6, 2, 0, 0);
    layout->addWidget(timelineCaption_);

    timeline_ = new QTreeWidget;
    timeline_->setObjectName(QStringLiteral("fileTimeline"));
    timeline_->setColumnCount(5);
    timeline_->setHeaderLabels({tr("Date"), tr("Author"), tr("Change"), tr("Commit"),
                                tr("Description")});
    timeline_->setRootIsDecorated(false);
    timeline_->setUniformRowHeights(true);
    timeline_->setSelectionMode(QAbstractItemView::SingleSelection);
    timeline_->setContextMenuPolicy(Qt::CustomContextMenu);
    timeline_->header()->setStretchLastSection(true);
    timeline_->header()->resizeSection(0, 130);
    timeline_->header()->resizeSection(1, 120);
    timeline_->header()->resizeSection(2, 90);
    timeline_->header()->resizeSection(3, 70);
    layout->addWidget(timeline_, 1);

    connect(timeline_, &QTreeWidget::currentItemChanged, this, [this] { requestViewer(); });
    connect(timeline_, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (const GitFileRevision *revision = selectedRevision(); revision != nullptr) {
            Q_EMIT commitActivated(revision->commit.hash);
        }
    });
    connect(timeline_, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &position) { showTimelineContextMenu(position); });

    return panel;
}

QWidget *FilesPage::buildViewer() {
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(6, 2, 4, 0);
    row->setSpacing(8);

    viewerCaption_ = new QLabel;
    viewerCaption_->setObjectName(QStringLiteral("sectionCaption"));

    viewerMode_ = new FlatComboBox;
    viewerMode_->setObjectName(QStringLiteral("fileViewerMode"));
    viewerMode_->addItem(tr("Changes of the commit"), static_cast<int>(ViewerMode::Changes));
    viewerMode_->addItem(tr("File contents"), static_cast<int>(ViewerMode::Contents));
    viewerMode_->addItem(tr("Difference from the current version"),
                         static_cast<int>(ViewerMode::DifferenceFromCurrent));
    viewerMode_->setFixedWidth(250);
    connect(viewerMode_, &QComboBox::currentIndexChanged, this, [this] { requestViewer(); });

    row->addWidget(viewerCaption_, 1);
    row->addWidget(viewerMode_);
    layout->addLayout(row);

    viewer_ = new QStackedWidget;
    diffView_ = new DiffView;
    diffView_->setMode(DiffView::Mode::ReadOnly);
    diffView_->setPlaceholderMessage(tr("Select a file of the repository"));
    contentsView_ = new SourceView;
    highlighter_ = new SyntaxHighlighter(contentsView_->document());
    connect(Theme::instance(), &Theme::changed, this, [this] { highlighter_->rehighlight(); });
    imageView_ = new ImagePreview;
    pdfView_ = new PdfPreview;
    viewer_->addWidget(diffView_);
    viewer_->addWidget(contentsView_);
    viewer_->addWidget(imageView_);
    viewer_->addWidget(pdfView_);
    layout->addWidget(viewer_, 1);

    return panel;
}

void FilesPage::setReferences(const QList<GitBranchInfo> &branches,
                              const QList<GitTagInfo> &tags) {
    const QString selected = currentRevision();

    const QSignalBlocker blocker(revision_);
    revision_->clear();
    revision_->addItem(tr("Working copy"), QString());
    revision_->addItem(tr("HEAD"), QStringLiteral("HEAD"));

    if (!branches.isEmpty()) {
        revision_->insertSeparator(revision_->count());
        for (const GitBranchInfo &branch : branches) {
            revision_->addItem(branch.name, branch.name);
        }
    }
    if (!tags.isEmpty()) {
        revision_->insertSeparator(revision_->count());
        for (const GitTagInfo &tag : tags) {
            revision_->addItem(tag.name, tag.name);
        }
    }

    int index = revision_->findData(selected);
    if (index < 0 && isObjectId(selected)) {
        revision_->insertSeparator(revision_->count());
        revision_->addItem(tr("Commit %1").arg(selected.left(7)), selected);
        index = revision_->count() - 1;
    }
    revision_->setCurrentIndex(index >= 0 ? index : 0);

    // A branch or tag can disappear between refreshes; the tree then has to
    // follow the revision it fell back to.
    if (currentRevision() != selected) {
        pendingPath_ = currentPath_;
        reloadTree();
    }
}

QString FilesPage::currentRevision() const {
    return revision_->currentData().toString();
}

void FilesPage::reload() {
    pendingPath_ = currentPath_;
    reloadTree();
}

void FilesPage::showFile(const QString &path, const QString &revision) {
    int index = revision_->findData(revision);
    if (index < 0) {
        // A commit reached from elsewhere is kept as its own entry.
        revision_->addItem(tr("Commit %1").arg(revision.left(7)), revision);
        index = revision_->count() - 1;
    }

    {
        const QSignalBlocker blocker(revision_);
        revision_->setCurrentIndex(index);
    }

    {
        const QSignalBlocker blocker(fileFilter_);
        fileFilter_->clear();
    }
    filterTimer_->stop();
    treeCaption_->setText(tr("REPOSITORY"));

    pendingPath_ = path;
    reloadTree();
}

void FilesPage::reloadTree() {
    if (listingRevision_ != currentRevision()) {
        listing_.clear();
        listingRevision_.clear();
    }

    if (fileFilter_ != nullptr && !fileFilter_->text().trimmed().isEmpty()) {
        runFilter();
        return;
    }

    ++treeGeneration_;
    ++historyGeneration_;
    directoryItems_.clear();
    directoryQueue_.clear();
    tree_->clear();
    timeline_->clear();
    revisions_.clear();
    currentPath_.clear();
    timelineCaption_->setText(tr("Select a file to see its history"));
    clearFileSelection();
    requestDirectory({});
}

void FilesPage::requestDirectory(const QString &directory) {
    if (!directoryQueue_.contains(directory)) {
        directoryQueue_.append(directory);
    }
    startNextDirectory();
}

void FilesPage::startNextDirectory() {
    if (treeWatcher_->isRunning() || directoryQueue_.isEmpty()) {
        return;
    }

    const QString directory = directoryQueue_.takeFirst();
    const QString revision = currentRevision();
    const quint64 generation = treeGeneration_;
    const GitClient git = git_;

    treeWatcher_->setFuture(QtConcurrent::run([git, revision, directory, generation] {
        FileTreeLoad load;
        load.generation = generation;
        load.revision = revision;
        load.directory = directory;
        load.entries = git.treeEntries(revision, directory, &load.error);
        return load;
    }));
}

void FilesPage::runFilter() {
    const QString query = fileFilter_->text().trimmed();
    if (query.isEmpty()) {
        treeCaption_->setText(tr("REPOSITORY"));
        pendingPath_ = currentPath_;
        reloadTree();
        return;
    }

    if (listingRevision_ == currentRevision() && !listing_.isEmpty()) {
        showMatches(query);
        return;
    }

    treeCaption_->setText(tr("Searching…"));
    ++listingGeneration_;
    const QString revision = currentRevision();
    const quint64 generation = listingGeneration_;
    const GitClient git = git_;

    listingWatcher_->setFuture(QtConcurrent::run([git, revision, generation] {
        FileListingLoad load;
        load.generation = generation;
        load.revision = revision;
        load.entries = git.allFiles(revision, &load.error);
        return load;
    }));
}

void FilesPage::applyListing(const FileListingLoad &load) {
    if (load.generation != listingGeneration_) {
        return;
    }
    if (!load.error.isEmpty()) {
        treeCaption_->setText(load.error);
        return;
    }

    listing_ = load.entries;
    listingRevision_ = load.revision;
    showMatches(fileFilter_->text().trimmed());
}

void FilesPage::showMatches(const QString &query) {
    if (query.isEmpty()) {
        return;
    }

    ++treeGeneration_;
    directoryQueue_.clear();
    directoryItems_.clear();
    tree_->clear();

    int matches = 0;
    {
        const QSignalBlocker blocker(tree_);
        for (const GitTreeEntry &entry : listing_) {
            if (!entry.name.contains(query, Qt::CaseInsensitive)) {
                continue;
            }
            if (matches == MaximumMatchCount) {
                break;
            }
            ++matches;

            QTreeWidgetItem *parent = tree_->invisibleRootItem();
            const QStringList parts = entry.path.split(u'/');
            QString directory;
            for (qsizetype level = 0; level + 1 < parts.size(); ++level) {
                directory += level > 0 ? QStringLiteral("/") + parts.at(level)
                                       : parts.at(level);
                QTreeWidgetItem *folder = directoryItems_.value(directory);
                if (folder == nullptr) {
                    folder = new QTreeWidgetItem(parent, {parts.at(level)});
                    folder->setIcon(0, Icons::icon(Icons::Glyph::Folder));
                    folder->setData(0, PathRole, directory);
                    folder->setData(0, IsDirectoryRole, true);
                    folder->setData(0, LoadedRole, true);
                    directoryItems_.insert(directory, folder);
                }
                parent = folder;
            }

            auto *item = new QTreeWidgetItem(parent, {entry.name});
            item->setIcon(0, Icons::icon(Icons::fileGlyph(entry.name)));
            item->setData(0, PathRole, entry.path);
            item->setData(0, IsDirectoryRole, false);
            item->setData(0, LoadedRole, true);
            item->setToolTip(0, entry.path);
            item->setText(1, formatSize(entry.size));
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        }
    }

    tree_->expandAll();

    if (matches == 0) {
        treeCaption_->setText(tr("No file name holds “%1”").arg(query));
        return;
    }
    treeCaption_->setText(matches == MaximumMatchCount
                              ? tr("First %n match(es)", nullptr, matches)
                              : tr("%n match(es)", nullptr, matches));
}

void FilesPage::applyDirectory(const FileTreeLoad &load) {
    if (load.generation != treeGeneration_) {
        return;
    }

    QTreeWidgetItem *parent = load.directory.isEmpty()
                                  ? tree_->invisibleRootItem()
                                  : directoryItems_.value(load.directory);
    if (parent == nullptr) {
        return;
    }

    {
        const QSignalBlocker blocker(tree_);
        for (QTreeWidgetItem *child : parent->takeChildren()) {
            delete child;
        }

        if (!load.error.isEmpty()) {
            Q_EMIT messagePosted(load.error, 6000);
            return;
        }

        for (const GitTreeEntry &entry : load.entries) {
            auto *item = new QTreeWidgetItem(parent, {entry.name});
            item->setData(0, PathRole, entry.path);
            item->setData(0, IsDirectoryRole, entry.directory);
            item->setData(0, LoadedRole, false);
            item->setToolTip(0, entry.path);

            if (entry.directory) {
                item->setIcon(0, Icons::icon(Icons::Glyph::Folder));
                directoryItems_.insert(entry.path, item);
                new QTreeWidgetItem(item);
            } else if (entry.submodule) {
                item->setIcon(0, Icons::icon(Icons::Glyph::Submodule));
            } else {
                item->setIcon(0, Icons::icon(Icons::fileGlyph(entry.name)));
                item->setText(1, formatSize(entry.size));
                item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            }
        }
    }

    followPendingPath(load.directory);
}

void FilesPage::expandDirectory(QTreeWidgetItem *item) {
    if (item == nullptr || item->data(0, LoadedRole).toBool()) {
        return;
    }
    item->setData(0, LoadedRole, true);
    requestDirectory(item->data(0, PathRole).toString());
}

void FilesPage::followPendingPath(const QString &directory) {
    if (pendingPath_.isEmpty()) {
        return;
    }

    const QString prefix = directory.isEmpty() ? QString() : directory + u'/';
    if (!pendingPath_.startsWith(prefix)) {
        return;
    }

    const QString remainder = pendingPath_.sliced(prefix.size());
    const qsizetype separator = remainder.indexOf(u'/');
    const QString childPath = prefix + (separator >= 0 ? remainder.first(separator) : remainder);

    QTreeWidgetItem *parent = directory.isEmpty() ? tree_->invisibleRootItem()
                                                  : directoryItems_.value(directory);
    if (parent == nullptr) {
        pendingPath_.clear();
        return;
    }

    for (int index = 0; index < parent->childCount(); ++index) {
        QTreeWidgetItem *child = parent->child(index);
        if (child->data(0, PathRole).toString() != childPath) {
            continue;
        }
        if (separator >= 0) {
            tree_->expandItem(child);
        } else {
            pendingPath_.clear();
            tree_->setCurrentItem(child);
            tree_->scrollToItem(child);
        }
        return;
    }

    // The path does not exist at this revision, which is normal after moving
    // the tree to a commit older than the file.
    const QString missing = pendingPath_;
    pendingPath_.clear();
    timelineCaption_->setText(tr("“%1” does not exist at this revision").arg(missing));
}

void FilesPage::selectTreeFile(QTreeWidgetItem *item) {
    if (item == nullptr || item->data(0, IsDirectoryRole).toBool()) {
        return;
    }

    const QString path = item->data(0, PathRole).toString();
    if (path.isEmpty() || path == currentPath_) {
        return;
    }

    currentPath_ = path;
    requestHistory(path);
}

void FilesPage::showTreeContextMenu(const QPoint &position) {
    QTreeWidgetItem *item = tree_->itemAt(position);
    if (item == nullptr) {
        return;
    }

    const QString path = item->data(0, PathRole).toString();
    QMenu menu(this);

    menu.addAction(tr("Copy path"), this, [path] {
        QApplication::clipboard()->setText(path);
    });

    const QString absolute = QDir(git_.repositoryRoot()).filePath(path);
    if (QFileInfo::exists(absolute)) {
        menu.addAction(tr("Show in file manager"), this, [absolute] {
            static_cast<void>(
                PlatformServices::instance().revealInFileManager(absolute));
        });
    }

    menu.exec(tree_->viewport()->mapToGlobal(position));
}

void FilesPage::requestHistory(const QString &path) {
    ++historyGeneration_;
    timelineCaption_->setText(tr("Reading the history of “%1”…").arg(path));
    timeline_->clear();
    revisions_.clear();
    clearFileSelection();

    const QString revision = currentRevision();
    const quint64 generation = historyGeneration_;
    const GitClient git = git_;

    historyWatcher_->setFuture(QtConcurrent::run([git, path, revision, generation] {
        FileHistoryLoad load;
        load.generation = generation;
        load.path = path;
        load.revisions = git.fileHistory(path, revision, MaximumTimelineCount, &load.error);
        return load;
    }));
}

void FilesPage::applyHistory(const FileHistoryLoad &load) {
    if (load.generation != historyGeneration_) {
        return;
    }

    revisions_ = load.revisions;
    timeline_->clear();

    if (!load.error.isEmpty()) {
        timelineCaption_->setText(load.error);
        return;
    }
    if (revisions_.isEmpty()) {
        timelineCaption_->setText(tr("“%1” has no history at this revision").arg(load.path));
        return;
    }

    const bool truncated = revisions_.size() >= MaximumTimelineCount;
    timelineCaption_->setText(truncated
                                  ? tr("%1 — the most recent %n revision(s)", nullptr,
                                       static_cast<int>(revisions_.size())).arg(load.path)
                                  : tr("%1 — %n revision(s)", nullptr,
                                       static_cast<int>(revisions_.size())).arg(load.path));

    for (qsizetype index = 0; index < revisions_.size(); ++index) {
        const GitFileRevision &revision = revisions_.at(index);
        auto *item = new QTreeWidgetItem(timeline_, {
            formatMoment(revision.commit.authoredAt),
            revision.commit.author,
            changeDescription(revision.status),
            revision.commit.shortHash,
            revision.commit.subject
        });
        item->setData(0, RevisionIndexRole, static_cast<int>(index));
        if (revision.isRename()) {
            item->setToolTip(2, tr("Renamed from “%1”").arg(revision.previousPath));
        }
    }

    timeline_->setCurrentItem(timeline_->topLevelItem(0));
}

void FilesPage::showTimelineContextMenu(const QPoint &position) {
    const GitFileRevision *revision = selectedRevision();
    if (timeline_->itemAt(position) == nullptr || revision == nullptr) {
        return;
    }

    const QString hash = revision->commit.hash;
    QMenu menu(this);
    menu.addAction(tr("Show the commit in History"), this,
                   [this, hash] { Q_EMIT commitActivated(hash); });
    menu.addAction(tr("Copy commit hash"), this, [hash] {
        QApplication::clipboard()->setText(hash);
    });
    menu.exec(timeline_->viewport()->mapToGlobal(position));
}

const GitFileRevision *FilesPage::selectedRevision() const {
    const QTreeWidgetItem *item = timeline_->currentItem();
    if (item == nullptr) {
        return nullptr;
    }

    const int index = item->data(0, RevisionIndexRole).toInt();
    if (index < 0 || index >= revisions_.size()) {
        return nullptr;
    }
    return &revisions_.at(index);
}

void FilesPage::clearFileSelection() {
    ++viewerGeneration_;
    viewerCaption_->clear();
    viewer_->setCurrentWidget(diffView_);
    diffView_->setPatch({});
    diffView_->setPlaceholderMessage(tr("Select a file of the repository"));
    contentsView_->clear();
}

void FilesPage::requestViewer() {
    const GitFileRevision *selected = selectedRevision();
    if (selected == nullptr) {
        clearFileSelection();
        return;
    }

    ++viewerGeneration_;

    const GitFileRevision revision = *selected;
    const quint64 generation = viewerGeneration_;
    const auto mode = static_cast<ViewerMode>(viewerMode_->currentData().toInt());
    const GitClient git = git_;
    // The current version is the one at the revision the tree is showing; the
    // working copy has no blob to compare against.
    const QString treeRevision = currentRevision().isEmpty() ? QStringLiteral("HEAD")
                                                             : currentRevision();
    const QString currentPath = currentPath_;

    QString caption = tr("%1 at %2").arg(revision.path, revision.commit.shortHash);
    if (revision.isRename()) {
        caption += tr(" · renamed from %1").arg(revision.previousPath);
    }
    viewerCaption_->setText(caption);

    viewerWatcher_->setFuture(QtConcurrent::run(
        [git, revision, mode, treeRevision, currentPath, generation] {
            FileViewerLoad load;
            load.generation = generation;
            load.path = revision.path;

            switch (mode) {
                case ViewerMode::Changes: {
                    const GitCommandResult result = git.commitDiff(revision.commit.hash,
                                                                   revision.path);
                    if (!result.succeeded()) {
                        load.placeholder = result.errorText();
                    } else if (result.output.isEmpty()) {
                        load.placeholder = tr("The commit does not change this file.");
                    } else {
                        load.patch = result.outputText();
                        load.content = FileViewerContent::Patch;
                    }
                    break;
                }
                case ViewerMode::Contents: {
                    if (revision.isDeletion()) {
                        load.placeholder = tr("The commit deleted the file, so this revision "
                                              "has no contents.");
                        break;
                    }

                    const qint64 size = git.fileSize(revision.commit.hash, revision.path);
                    if (size < 0) {
                        load.placeholder = tr("The file is missing at this revision.");
                        break;
                    }
                    if (size > MaximumViewerSize) {
                        load.placeholder = tr("The file is too large to display (%1).")
                                               .arg(formatSize(size));
                        break;
                    }

                    const GitCommandResult result = git.fileContent(revision.commit.hash,
                                                                    revision.path);
                    if (!result.succeeded()) {
                        load.placeholder = result.errorText();
                        break;
                    }

                    if (looksLikePdf(result.output) && PdfPreview::isSupported()) {
                        load.content = FileViewerContent::Pdf;
                        load.bytes = result.output;
                        load.note = tr("PDF · %1").arg(formatSize(size));
                        break;
                    }

                    QString format;
                    load.image = FilePreview::decodeImage(result.output, &format);
                    if (!load.image.isNull()) {
                        load.content = FileViewerContent::Image;
                        load.note = tr("%1 · %2 × %3 · %4").arg(format)
                                        .arg(load.image.width())
                                        .arg(load.image.height())
                                        .arg(formatSize(size));
                        break;
                    }

                    load.content = FileViewerContent::Text;
                    if (looksBinary(result.output)) {
                        load.highlight = false;
                        load.contents = FilePreview::hexDump(result.output);
                        load.note = size > MaximumHexDumpSize
                                        ? tr("binary, first %1 of %2")
                                              .arg(formatSize(MaximumHexDumpSize),
                                                   formatSize(size))
                                        : tr("binary, %1").arg(formatSize(size));
                        break;
                    }

                    load.contents = result.outputText();
                    break;
                }
                case ViewerMode::DifferenceFromCurrent: {
                    const GitCommandResult result = git.fileDiff(revision.commit.hash,
                                                                 revision.path, treeRevision,
                                                                 currentPath);
                    if (!result.succeeded()) {
                        load.placeholder = result.errorText();
                    } else if (result.output.isEmpty()) {
                        load.placeholder = tr("This version matches the current one.");
                    } else {
                        load.patch = result.outputText();
                        load.content = FileViewerContent::Patch;
                    }
                    break;
                }
            }

            return load;
        }));
}

void FilesPage::applyViewer(const FileViewerLoad &load) {
    if (load.generation != viewerGeneration_) {
        return;
    }

    if (!load.note.isEmpty()) {
        viewerCaption_->setText(QStringLiteral("%1 · %2").arg(viewerCaption_->text(),
                                                             load.note));
    }

    // The document keeps the bytes of the previous revision alive otherwise.
    if (load.content != FileViewerContent::Pdf) {
        pdfView_->clear();
    }

    switch (load.content) {
        case FileViewerContent::Placeholder:
            viewer_->setCurrentWidget(diffView_);
            diffView_->setPatch({});
            diffView_->setPlaceholderMessage(load.placeholder);
            return;
        case FileViewerContent::Patch:
            viewer_->setCurrentWidget(diffView_);
            diffView_->setPatch(load.patch);
            return;
        case FileViewerContent::Image:
            viewer_->setCurrentWidget(imageView_);
            imageView_->setImage(load.image);
            return;
        case FileViewerContent::Pdf: {
            const int pages = pdfView_->showDocument(load.bytes);
            if (pages < 0) {
                viewer_->setCurrentWidget(contentsView_);
                highlighter_->showFile({}, FilePreview::hexDump(load.bytes));
                return;
            }
            viewerCaption_->setText(tr("%1 · %n page(s)", nullptr, pages)
                                        .arg(viewerCaption_->text()));
            viewer_->setCurrentWidget(pdfView_);
            return;
        }
        case FileViewerContent::Text:
            break;
    }

    viewer_->setCurrentWidget(contentsView_);
    // Highlighting walks every line, so it is spent on files a reader can
    // actually take in rather than on generated blobs; a hex dump has no
    // language of its own either.
    const bool highlighted = load.highlight
                             && load.contents.size() <= MaximumHighlightedSize;
    highlighter_->showFile(highlighted ? load.path : QString(), load.contents);
}
