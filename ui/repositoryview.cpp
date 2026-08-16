// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "repositoryview.h"

#include "commitgraph.h"
#include "commitmodel.h"
#include "dialogs.h"
#include "diffview.h"
#include "filespage.h"
#include "flatcombobox.h"
#include "icons.h"
#include "platform/platformservices.h"
#include "repositorywatcher.h"
#include "theme.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFrame>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSignalBlocker>
#include <QLocale>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTextBrowser>
#include <QTextDocument>
#include <QThreadPool>
#include <QTimer>
#include <QTreeView>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QtConcurrentRun>

namespace {

constexpr int PathRole = Qt::UserRole + 20;
constexpr int OriginalPathRole = Qt::UserRole + 21;
constexpr int IndexStatusRole = Qt::UserRole + 22;
constexpr int WorkStatusRole = Qt::UserRole + 23;
constexpr int IsUntrackedRole = Qt::UserRole + 24;
constexpr int IsConflictedRole = Qt::UserRole + 25;
constexpr int IsDirectoryRole = Qt::UserRole + 26;
constexpr int NavigationKindRole = Qt::UserRole + 30;
constexpr int NavigationValueRole = Qt::UserRole + 31;
constexpr int NavigationExtraRole = Qt::UserRole + 32;
constexpr int NavigationAheadRole = Qt::UserRole + 33;
constexpr int NavigationBehindRole = Qt::UserRole + 34;
constexpr int HistoryRevisionRole = Qt::UserRole + 35;
constexpr int FullDiffContextLines = 1'000'000;
constexpr int RepositorySplitterWidth = 0;
constexpr int SeparatorHeight = 1;
constexpr int JumpListVisibleRows = 12;
constexpr int CounterHeight = 16;
constexpr int CounterPadding = 4;
constexpr qreal CounterRadius = 4.0;
constexpr int AutoFetchDefaultMinutes = 10;
constexpr int AutoFetchMinimumMinutes = 1;
constexpr int AutoFetchMaximumMinutes = 24 * 60;
constexpr int AutoFetchStartupDelayMs = 5'000;
constexpr int AutoFetchTimeoutMs = 90'000;
// Limit concurrent checks when several tabs are restored together.
constexpr int AutoFetchParallelChecks = 2;

enum NavigationKind {
    NavigationSection,
    NavigationFileStatus,
    NavigationHistory,
    NavigationSearch,
    NavigationBranchFolder,
    NavigationBranch,
    NavigationTag,
    NavigationRemote,
    NavigationRemoteBranch,
    NavigationStash,
    NavigationSubmodule,
    NavigationPlaceholder
};

QColor navigationTextColor() {
    return Theme::instance()->mode() == Theme::Mode::Light
               ? QColor(QStringLiteral("#454545"))
               : Theme::instance()->palette().text;
}

/// Compact history combo with a custom dropdown arrow.
QIcon navigationSectionIcon(const Icons::Glyph glyph, const QColor &color) {
    // Toolbar glyphs intentionally have generous whitespace. Trim only two
    // logical pixels here: this enlarges sidebar icons slightly while leaving
    // enough room for antialiasing and rounded pen caps on every edge.
    const QPixmap source = Icons::pixmap(glyph, 32, color);
    const int inset = qRound(2.0 * source.devicePixelRatio());
    QPixmap enlarged = source.copy(inset, inset,
                                   source.width() - inset * 2,
                                   source.height() - inset * 2);
    enlarged.setDevicePixelRatio(source.devicePixelRatio());
    return QIcon(enlarged);
}

class NavigationTree final : public QTreeWidget {
protected:
    void drawBranches(QPainter *painter, const QRect &rect,
                      const QModelIndex &index) const override {
        if (!model()->hasChildren(index)) {
            return;
        }

        const qreal centerX = rect.right() - 7.0;
        const qreal centerY = rect.center().y();
        QPainterPath chevron;
        if (isExpanded(index)) {
            chevron.moveTo(centerX - 5.0, centerY - 2.0);
            chevron.lineTo(centerX, centerY + 3.0);
            chevron.lineTo(centerX + 5.0, centerY - 2.0);
        } else {
            chevron.moveTo(centerX - 2.0, centerY - 5.0);
            chevron.lineTo(centerX + 3.0, centerY);
            chevron.lineTo(centerX - 2.0, centerY + 5.0);
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(Theme::instance()->mode() == Theme::Mode::Light
                                 ? QColor(QStringLiteral("#B5B5B5"))
                                 : Theme::instance()->palette().mutedText,
                             1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(chevron);
        painter->restore();
    }
};

/// Local branches use a small ring inside the indentation area;
/// it does not push the branch name to the right like a regular item icon.
class NavigationDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        if (index.data(NavigationKindRole).toInt() != NavigationBranch) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem branchOption(option);
        initStyleOption(&branchOption, index);
        const QString branchName = branchOption.text;
        const QFont branchFont = branchOption.font;
        branchOption.text.clear();
        branchOption.icon = {};
        QStyle *style = branchOption.widget != nullptr ? branchOption.widget->style()
                                                       : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &branchOption, painter,
                           branchOption.widget);

        const int ahead = index.data(NavigationAheadRole).toInt();
        const int behind = index.data(NavigationBehindRole).toInt();
        QStringList counters;
        if (ahead > 0) {
            counters.append(QStringLiteral("%1↑").arg(ahead));
        }
        if (behind > 0) {
            counters.append(QStringLiteral("%1↓").arg(behind));
        }

        QRect badgeRect;
        if (!counters.isEmpty()) {
            // Match the arrow glyph used by the tab counters.
            QFont badgeFont = QApplication::font();
            badgeFont.setPixelSize(10);
            badgeFont.setBold(true);

            const QString badgeText = counters.join(u' ');
            const int badgeWidth = QFontMetrics(badgeFont).horizontalAdvance(badgeText)
                                   + CounterPadding * 2;
            badgeRect = QRect(option.rect.right() - badgeWidth,
                              option.rect.center().y() - CounterHeight / 2,
                              badgeWidth, CounterHeight);

            const ThemePalette &palette = Theme::instance()->palette();
            const bool light = Theme::instance()->mode() == Theme::Mode::Light;

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(light ? QColor(QStringLiteral("#151515")) : palette.text);
            painter->drawRoundedRect(QRectF(badgeRect), CounterRadius, CounterRadius);
            painter->setFont(badgeFont);
            painter->setPen(light ? QColor(Qt::white) : palette.surface);
            painter->drawText(badgeRect, Qt::AlignCenter, badgeText);
            painter->restore();
        }

        painter->save();
        painter->setFont(branchFont);
        painter->setPen(option.state.testFlag(QStyle::State_Selected)
                            ? option.palette.color(QPalette::HighlightedText)
                            : navigationTextColor());
        const int right = badgeRect.isValid() ? badgeRect.left() - 6
                                               : option.rect.right() - 4;
        const QRect textRect(option.rect.left() + 11, option.rect.top(),
                             qMax(0, right - option.rect.left() - 11), option.rect.height());
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                          QFontMetrics(branchFont).elidedText(branchName, Qt::ElideRight,
                                                              textRect.width()));
        painter->restore();

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(navigationTextColor(), 2.0, Qt::SolidLine,
                             Qt::RoundCap, Qt::RoundJoin));
        painter->drawEllipse(QPointF(option.rect.left() - 5.0,
                                     option.rect.center().y()), 4.0, 4.0);
        painter->restore();
    }
};

QColor statusColor(const QChar status) {
    const ThemePalette &palette = Theme::instance()->palette();
    switch (status.unicode()) {
        case 'M': return palette.warning;
        case 'A': return palette.success;
        case 'D': return palette.danger;
        case 'R': return palette.accent;
        case 'C': return palette.accent;
        case 'U': return palette.danger;
        case '?': return palette.untracked;
        case 'T': return palette.warning;
        default: return palette.mutedText;
    }
}

/// File states are marked with symbols rather than letters.
QString statusLetter(const QChar status) {
    switch (status.unicode()) {
        case 'M': return QStringLiteral("…");
        case 'A': return QStringLiteral("+");
        case 'D': return QStringLiteral("−");
        case 'R': return QStringLiteral("→");
        case 'C': return QStringLiteral("⇉");
        case 'U': return QStringLiteral("!");
        case 'T': return QStringLiteral("±");
        case '?': return QStringLiteral("?");
        default: return QStringLiteral("·");
    }
}

QString statusDescription(const QChar status) {
    switch (status.unicode()) {
        case 'M': return RepositoryView::tr("Modified");
        case 'A': return RepositoryView::tr("Added");
        case 'D': return RepositoryView::tr("Deleted");
        case 'R': return RepositoryView::tr("Renamed");
        case 'C': return RepositoryView::tr("Copied");
        case 'U': return RepositoryView::tr("Conflict");
        case 'T': return RepositoryView::tr("Type changed");
        case '?': return RepositoryView::tr("New file");
        default: return RepositoryView::tr("Unchanged");
    }
}

QIcon statusBadge(const QChar status) {
    const int size = 15;
    QPixmap canvas(size * 2, size * 2);
    canvas.setDevicePixelRatio(2.0);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(statusColor(status));
    painter.drawRoundedRect(QRectF(0.5, 0.5, size - 1.0, size - 1.0), 3.5, 3.5);

    QFont font = painter.font();
    font.setPixelSize(10);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, statusLetter(status));
    painter.end();
    return {canvas};
}

QTreeWidgetItem *addSection(QTreeWidget *tree, const QString &title, const QIcon &icon = {},
                            const bool expanded = true) {
    auto *section = new QTreeWidgetItem(tree, {title.toUpper()});
    section->setData(0, NavigationKindRole, NavigationSection);
    if (!icon.isNull()) {
        section->setIcon(0, icon);
    }
    section->setFlags(section->flags() & ~Qt::ItemIsSelectable);
    QFont font = section->font(0);
    font.setPixelSize(12);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.2);
    section->setFont(0, font);
    section->setSizeHint(0, QSize(0, 30));
    section->setForeground(0, Theme::instance()->palette().sectionText);
    section->setExpanded(expanded);
    return section;
}

QTreeWidgetItem *addNavigationItem(QTreeWidgetItem *parent, const QString &text,
                                   const NavigationKind kind, const QString &value = {},
                                   const QIcon &icon = {}) {
    auto *item = new QTreeWidgetItem(parent, {text});
    item->setData(0, NavigationKindRole, kind);
    item->setData(0, NavigationValueRole, value);
    if (!icon.isNull()) {
        item->setIcon(0, icon);
    }
    QFont font = item->font(0);
    font.setPixelSize(12);
    font.setWeight(QFont::Normal);
    item->setFont(0, font);
    item->setSizeHint(0, QSize(0, 25));
    return item;
}

QString elideMiddle(const QString &text, const int maximum) {
    if (text.size() <= maximum) {
        return text;
    }
    const int half = (maximum - 1) / 2;
    return text.left(half) + QStringLiteral("…") + text.right(half);
}

/// Settings keys must not contain path separators, which QSettings reads as groups.
QString detailsRow(const QString &label, const QString &value) {
    return QStringLiteral("<tr><td valign='top'><b>%1&nbsp;&nbsp;</b></td>"
                          "<td valign='top'>%2</td></tr>")
        .arg(label, value);
}

QString detailsTable(const QString &rows) {
    return QStringLiteral("<table cellspacing='0' cellpadding='0'>%1</table>").arg(rows);
}

QString pageSettingsKey(const QString &repositoryRoot) {
    const QByteArray digest = QCryptographicHash::hash(repositoryRoot.toUtf8(),
                                                       QCryptographicHash::Md5);
    return QStringLiteral("pages/%1").arg(QString::fromLatin1(digest.toHex()));
}

// Keep slow remotes out of the snapshot worker pool.
class AutoFetchPool final : public QThreadPool {
public:
    AutoFetchPool() {
        setMaxThreadCount(AutoFetchParallelChecks);
    }
};

QThreadPool *autoFetchPool() {
    static AutoFetchPool pool;
    return &pool;
}

bool autoFetchEnabled() {
    return QSettings().value(QStringLiteral("autoFetch/enabled"), true).toBool();
}

int autoFetchIntervalMinutes() {
    const int minutes = QSettings()
                            .value(QStringLiteral("autoFetch/intervalMinutes"),
                                   AutoFetchDefaultMinutes)
                            .toInt();
    return qBound(AutoFetchMinimumMinutes, minutes, AutoFetchMaximumMinutes);
}

QString historyHeaderSettingsKey(const QString &repositoryRoot) {
    const QByteArray digest = QCryptographicHash::hash(repositoryRoot.toUtf8(),
                                                       QCryptographicHash::Md5);
    return QStringLiteral("historyColumns/%1").arg(QString::fromLatin1(digest.toHex()));
}

QTreeWidgetItem *firstLeaf(QTreeWidgetItem *item) {
    if (item == nullptr) {
        return nullptr;
    }
    if (item->childCount() == 0) {
        return item;
    }
    for (int index = 0; index < item->childCount(); ++index) {
        if (QTreeWidgetItem *leaf = firstLeaf(item->child(index)); leaf != nullptr) {
            return leaf;
        }
    }
    return nullptr;
}

}

RepositoryView::RepositoryView(const QString &path, QWidget *parent)
    : QWidget(parent),
      operationWatcher_(new QFutureWatcher<GitCommandResult>(this)),
      autoFetchTimer_(new QTimer(this)),
      autoFetchWatcher_(new QFutureWatcher<GitCommandResult>(this)),
      diskWatcher_(new RepositoryWatcher(this)),
      snapshotWatcher_(new QFutureWatcher<RepositorySnapshot>(this)) {
    valid_ = git_.openRepository(path).succeeded();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildStateBanner());

    workspaceSplitter_ = new QSplitter(Qt::Horizontal);
    workspaceSplitter_->setHandleWidth(RepositorySplitterWidth);
    workspaceSplitter_->addWidget(buildSidebar());

    pages_ = new QStackedWidget;
    pages_->addWidget(buildFileStatusPage());
    pages_->addWidget(buildHistoryPage());
    pages_->addWidget(buildSearchPage());

    filesPage_ = new FilesPage(git_.repositoryRoot());
    pages_->addWidget(filesPage_);
    connect(filesPage_, &FilesPage::messagePosted, this,
            [this](const QString &message, const int timeoutMs) {
                Q_EMIT messagePosted(message, timeoutMs);
            });
    connect(filesPage_, &FilesPage::commitActivated, this, [this](const QString &hash) {
        showPage(Page::History);
        jumpToRevision(hash);
    });

    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(pages_, 1);

    workspaceSplitter_->addWidget(content);
    workspaceSplitter_->setCollapsible(0, false);
    workspaceSplitter_->setStretchFactor(0, 0);
    workspaceSplitter_->setStretchFactor(1, 1);
    workspaceSplitter_->setSizes({210, 1150});
    layout->addWidget(workspaceSplitter_, 1);

    connect(operationWatcher_, &QFutureWatcher<GitCommandResult>::finished, this,
            [this] { finishRemoteOperation(); });
    autoFetchTimer_->setSingleShot(true);
    connect(autoFetchTimer_, &QTimer::timeout, this, [this] { runAutoFetch(); });
    connect(autoFetchWatcher_, &QFutureWatcher<GitCommandResult>::finished, this,
            [this] { finishAutoFetch(); });
    connect(snapshotWatcher_, &QFutureWatcher<RepositorySnapshot>::finished, this,
            [this] { applySnapshot(snapshotWatcher_->result()); });
    connect(diskWatcher_, &RepositoryWatcher::repositoryChangedOnDisk, this,
            [this] { refreshAll(); });
    connect(Theme::instance(), &Theme::changed, this, [this] {
        if (valid_) {
            refreshNavigation();
            refreshStatus();
        }
    });

    if (valid_) {
        const int storedPage = QSettings()
                                   .value(pageSettingsKey(git_.repositoryRoot()), 0)
                                   .toInt();
        currentPage_ = static_cast<Page>(qBound(0, storedPage, 3));
        diskWatcher_->setRepository(git_.gitDirectory());
        refreshAll();
        showPage(currentPage_);
        QTimer::singleShot(AutoFetchStartupDelayMs, this, [this] { runAutoFetch(); });
    }
}

bool RepositoryView::isValid() const {
    return valid_;
}

QString RepositoryView::repositoryRoot() const {
    return git_.repositoryRoot();
}

QString RepositoryView::repositoryName() const {
    return git_.repositoryName();
}

QString RepositoryView::currentBranchName() const {
    return currentBranch_;
}

int RepositoryView::aheadCount() const {
    return ahead_;
}

int RepositoryView::behindCount() const {
    return behind_;
}

int RepositoryView::changeCount() const {
    return static_cast<int>(files_.size());
}

bool RepositoryView::isBusy() const {
    return operationInProgress_;
}

bool RepositoryView::hasStagedChanges() const {
    return hasStagedChanges_;
}

/// A banner that only appears while a merge, rebase or cherry-pick is unfinished.
QWidget *RepositoryView::buildStateBanner() {
    stateBanner_ = new QWidget;
    stateBanner_->setObjectName(QStringLiteral("stateBanner"));
    auto *layout = new QHBoxLayout(stateBanner_);
    layout->setContentsMargins(14, 7, 14, 7);
    layout->setSpacing(10);

    auto *warningIcon = new QLabel;
    warningIcon->setPixmap(Icons::pixmap(Icons::Glyph::Warning, 18, QColor()));
    layout->addWidget(warningIcon);

    stateBadge_ = new QLabel;
    stateBadge_->setObjectName(QStringLiteral("stateBadge"));
    layout->addWidget(stateBadge_);
    layout->addStretch();

    continueButton_ = new QPushButton(tr("Continue"));
    abortButton_ = new QPushButton(tr("Abort"));
    abortButton_->setProperty("danger", true);
    connect(continueButton_, &QPushButton::clicked, this, [this] {
        runOperation(tr("Continuing the operation"),
                     [this] { return git_.continueOperation(); });
    });
    connect(abortButton_, &QPushButton::clicked, this, [this] {
        runOperation(tr("Aborting the operation"),
                     [this] { return git_.abortOperation(); });
    });
    layout->addWidget(continueButton_);
    layout->addWidget(abortButton_);

    stateBanner_->hide();
    return stateBanner_;
}

QWidget *RepositoryView::buildViewSwitcher() {
    auto *switcher = new QWidget;
    switcher->setObjectName(QStringLiteral("viewSwitcher"));
    switcher->setMaximumWidth(184);
    auto *layout = new QVBoxLayout(switcher);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const auto addButton = [this, layout](const QString &text, const Page page) {
        auto *button = new QPushButton(text);
        button->setObjectName(QStringLiteral("viewSwitchButton"));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        connect(button, &QPushButton::clicked, this, [this, page] { showPage(page); });
        layout->addWidget(button);
        return button;
    };

    fileStatusButton_ = addButton(tr("File Status"), Page::FileStatus);
    historyButton_ = addButton(tr("History"), Page::History);
    filesButton_ = addButton(tr("Files"), Page::Files);
    searchButton_ = addButton(tr("Search"), Page::Search);
    fileStatusButton_->setChecked(true);

    // Switching to the log explicitly drops any branch filter picked in the sidebar.
    connect(historyButton_, &QPushButton::clicked, this, [this] {
        if (!historyRevision_.isEmpty() || historyScope_->currentIndex() != 0) {
            historyRevision_.clear();
            const QSignalBlocker blocker(historyScope_);
            historyScope_->setCurrentIndex(0);
            refreshHistory();
        }
    });

    return switcher;
}

QWidget *RepositoryView::buildSidebar() {
    auto *sidebar = new QWidget;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setMinimumWidth(180);
    sidebar->setMaximumWidth(280);

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(9, 5, 11, 5);
    layout->setSpacing(0);

    auto *workspaceHeader = new QWidget;
    workspaceHeader->setObjectName(QStringLiteral("workspaceHeader"));
    workspaceHeader->setMaximumWidth(184);
    auto *workspaceHeaderLayout = new QHBoxLayout(workspaceHeader);
    workspaceHeaderLayout->setContentsMargins(18, 0, 0, 0);
    workspaceHeaderLayout->setSpacing(5);
    auto *workspaceIcon = new QLabel;
    const QColor workspaceIconColor = Theme::instance()->mode() == Theme::Mode::Light
                                          ? QColor(QStringLiteral("#4F6B92"))
                                          : Theme::instance()->palette().sectionText;
    workspaceIcon->setPixmap(Icons::pixmap(Icons::Glyph::FileStatus, 20,
                                           workspaceIconColor));
    workspaceHeaderLayout->addWidget(workspaceIcon);
    auto *workspaceTitle = new QLabel(tr("WORKSPACE"));
    workspaceTitle->setObjectName(QStringLiteral("workspaceTitle"));
    workspaceHeaderLayout->addWidget(workspaceTitle);
    workspaceHeaderLayout->addStretch();
    layout->addWidget(workspaceHeader);
    layout->addWidget(buildViewSwitcher());
    layout->addSpacing(6);

    auto *topSeparator = new QFrame;
    topSeparator->setObjectName(QStringLiteral("sidebarTopSeparator"));
    topSeparator->setFrameShape(QFrame::HLine);
    topSeparator->setFixedHeight(SeparatorHeight);
    topSeparator->setMaximumWidth(184);
    layout->addWidget(topSeparator);
    layout->addSpacing(9);

    auto *navigationFilter = new QLineEdit;
    navigationFilter->setObjectName(QStringLiteral("navigationFilter"));
    navigationFilter->setMaximumWidth(182);
    navigationFilter->setPlaceholderText(tr("Search"));
    navigationFilter->setClearButtonEnabled(true);
    navigationFilter->addAction(
        Icons::icon(Icons::Glyph::Search, Theme::instance()->palette().mutedText),
        QLineEdit::TrailingPosition);
    layout->addWidget(navigationFilter);

    layout->addSpacing(8);

    navigationTree_ = new NavigationTree;
    navigationTree_->setObjectName(QStringLiteral("navigationTree"));
    navigationTree_->setHeaderHidden(true);
    navigationTree_->setRootIsDecorated(true);
    navigationTree_->setIndentation(17);
    navigationTree_->setUniformRowHeights(false);
    navigationTree_->setIconSize(QSize(20, 20));
    navigationTree_->setItemDelegate(new NavigationDelegate(navigationTree_));
    navigationTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(navigationTree_, 1);

    connect(navigationFilter, &QLineEdit::textChanged, navigationTree_,
            [this](const QString &text) {
                const QString needle = text.trimmed();
                for (int row = 0; row < navigationTree_->topLevelItemCount(); ++row) {
                    QTreeWidgetItem *section = navigationTree_->topLevelItem(row);
                    bool anyVisible = needle.isEmpty();
                    for (int child = 0; child < section->childCount(); ++child) {
                        QTreeWidgetItem *item = section->child(child);
                        const bool visible = needle.isEmpty()
                                             || item->text(0).contains(needle,
                                                                      Qt::CaseInsensitive);
                        item->setHidden(!visible);
                        anyVisible = anyVisible || visible;
                    }
                    section->setHidden(!anyVisible);
                }
            });

    connect(navigationTree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current) { activateNavigationItem(current); });
    connect(navigationTree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item) { handleNavigationDoubleClick(item); });
    connect(navigationTree_, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &position) { showNavigationContextMenu(position); });

    return sidebar;
}

QWidget *RepositoryView::buildFileStatusPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto *toolRow = new QHBoxLayout;
    fileFilter_ = new QLineEdit;
    fileFilter_->setPlaceholderText(tr("Filter files…"));
    fileFilter_->setClearButtonEnabled(true);
    fileFilter_->setMaximumWidth(260);
    treeModeButton_ = new QToolButton;
    treeModeButton_->setText(tr("Tree"));
    treeModeButton_->setCheckable(true);
    treeModeButton_->setToolTip(tr("Show the files as a directory tree"));
    toolRow->addWidget(treeModeButton_);
    toolRow->addStretch();
    toolRow->addWidget(fileFilter_);
    layout->addLayout(toolRow);

    auto *verticalSplitter = new QSplitter(Qt::Vertical);
    verticalSplitter->setHandleWidth(RepositorySplitterWidth);
    auto *topSplitter = new QSplitter(Qt::Horizontal);
    topSplitter->setHandleWidth(RepositorySplitterWidth);

    auto *filesPanel = new QWidget;
    auto *filesLayout = new QVBoxLayout(filesPanel);
    filesLayout->setContentsMargins(0, 0, 0, 0);
    filesLayout->setSpacing(4);

    auto *fileSplitter = new QSplitter(Qt::Vertical);
    fileSplitter->setHandleWidth(RepositorySplitterWidth);

    const auto buildFilePanel = [](const QString &caption, QLabel **captionLabel,
                                       QTreeWidget **tree, const QString &primaryText,
                                       const QString &secondaryText,
                                       QPushButton **primaryButton,
                                       QPushButton **secondaryButton) {
        auto *panel = new QWidget;
        auto *panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(0, 0, 0, 0);
        panelLayout->setSpacing(4);

        auto *headerRow = new QHBoxLayout;
        *captionLabel = new QLabel(caption);
        (*captionLabel)->setObjectName(QStringLiteral("sectionCaption"));
        headerRow->addWidget(*captionLabel);
        headerRow->addStretch();
        *primaryButton = new QPushButton(primaryText);
        *secondaryButton = new QPushButton(secondaryText);
        headerRow->addWidget(*secondaryButton);
        headerRow->addWidget(*primaryButton);
        panelLayout->addLayout(headerRow);

        *tree = new QTreeWidget;
        (*tree)->setHeaderHidden(true);
        (*tree)->setRootIsDecorated(false);
        (*tree)->setUniformRowHeights(true);
        (*tree)->setIconSize(QSize(15, 15));
        (*tree)->setSelectionMode(QAbstractItemView::ExtendedSelection);
        (*tree)->setContextMenuPolicy(Qt::CustomContextMenu);
        panelLayout->addWidget(*tree, 1);
        return panel;
    };

    QPushButton *stageSelectedButton = nullptr;
    QPushButton *stageAllButton = nullptr;
    QPushButton *unstageSelectedButton = nullptr;
    QPushButton *unstageAllButton = nullptr;

    auto *unstagedPanel = buildFilePanel(tr("UNSTAGED FILES"),
                                         &unstagedCaption_, &unstagedTree_,
                                         tr("Stage all"),
                                         tr("Stage"),
                                         &stageAllButton, &stageSelectedButton);
    auto *stagedPanel = buildFilePanel(tr("STAGED FILES"),
                                       &stagedCaption_, &stagedTree_,
                                       tr("Unstage all"),
                                       tr("Unstage"),
                                       &unstageAllButton, &unstageSelectedButton);
    fileSplitter->addWidget(stagedPanel);
    fileSplitter->addWidget(unstagedPanel);
    fileSplitter->setStretchFactor(0, 1);
    fileSplitter->setStretchFactor(1, 1);
    filesLayout->addWidget(fileSplitter, 1);

    auto *diffPanel = new QWidget;
    auto *diffLayout = new QVBoxLayout(diffPanel);
    diffLayout->setContentsMargins(0, 0, 0, 0);
    diffLayout->setSpacing(4);
    diffCaption_ = new QLabel(tr("CHANGES"));
    diffCaption_->setObjectName(QStringLiteral("sectionCaption"));
    diffLayout->addWidget(diffCaption_);
    diffView_ = new DiffView;
    diffView_->setPlaceholderMessage(tr("Select a file to see the changes"));
    diffLayout->addWidget(diffView_, 1);

    topSplitter->addWidget(filesPanel);
    topSplitter->addWidget(diffPanel);
    topSplitter->setStretchFactor(0, 2);
    topSplitter->setStretchFactor(1, 3);
    verticalSplitter->addWidget(topSplitter);

    auto *commitPanel = new QWidget;
    auto *commitLayout = new QVBoxLayout(commitPanel);
    commitLayout->setContentsMargins(0, 4, 0, 0);
    commitLayout->setSpacing(4);

    auto *commitHeader = new QHBoxLayout;
    authorLabel_ = new QLabel;
    authorLabel_->setObjectName(QStringLiteral("mutedText"));
    amendCheck_ = new QCheckBox(tr("Amend the previous commit"));
    commitHeader->addWidget(authorLabel_);
    commitHeader->addStretch();
    commitHeader->addWidget(amendCheck_);
    commitLayout->addLayout(commitHeader);

    commitMessage_ = new QPlainTextEdit;
    commitMessage_->setPlaceholderText(tr("Commit message"));
    commitMessage_->setMaximumHeight(96);
    commitLayout->addWidget(commitMessage_);

    auto *commitFooter = new QHBoxLayout;
    pushAfterCommitCheck_ = new QCheckBox(tr("Push right after the commit"));
    commitButton_ = new QPushButton(tr("Commit"));
    commitButton_->setProperty("accent", true);
    commitButton_->setIcon(Icons::icon(Icons::Glyph::Commit,
                                       Theme::instance()->palette().accentText));
    commitFooter->addWidget(pushAfterCommitCheck_);
    commitFooter->addStretch();
    commitFooter->addWidget(commitButton_);
    commitLayout->addLayout(commitFooter);

    verticalSplitter->addWidget(commitPanel);
    verticalSplitter->setStretchFactor(0, 5);
    verticalSplitter->setStretchFactor(1, 0);
    verticalSplitter->setCollapsible(1, false);
    layout->addWidget(verticalSplitter, 1);

    connect(stageSelectedButton, &QPushButton::clicked, this, [this] { stageSelected(); });
    connect(stageAllButton, &QPushButton::clicked, this, [this] { stageAll(); });
    connect(unstageSelectedButton, &QPushButton::clicked, this, [this] { unstageSelected(); });
    connect(unstageAllButton, &QPushButton::clicked, this, [this] { unstageAll(); });
    connect(commitButton_, &QPushButton::clicked, this, [this] { createCommit(); });
    connect(fileFilter_, &QLineEdit::textChanged, this, [this] { refreshStatus(); });
    connect(treeModeButton_, &QToolButton::toggled, this, [this](const bool checked) {
        treeMode_ = checked;
        refreshStatus();
    });
    connect(commitMessage_, &QPlainTextEdit::textChanged, this, [this] {
        commitButton_->setEnabled(!operationInProgress_
                                  && !commitMessage_->toPlainText().trimmed().isEmpty()
                                  && (hasStagedChanges_ || amendCheck_->isChecked()));
    });
    connect(amendCheck_, &QCheckBox::toggled, this, [this](const bool checked) {
        if (checked && commitMessage_->toPlainText().trimmed().isEmpty()) {
            commitMessage_->setPlainText(git_.lastCommitMessage());
        }
        commitButton_->setText(checked ? tr("Amend commit")
                                       : tr("Commit"));
        commitButton_->setEnabled(!operationInProgress_
                                  && !commitMessage_->toPlainText().trimmed().isEmpty()
                                  && (hasStagedChanges_ || checked));
    });
    connect(unstagedTree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (!unstagedTree_->selectedItems().isEmpty()) {
            const QSignalBlocker blocker(stagedTree_);
            stagedTree_->clearSelection();
        }
        refreshWorkingTreeDiff();
    });
    connect(stagedTree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (!stagedTree_->selectedItems().isEmpty()) {
            const QSignalBlocker blocker(unstagedTree_);
            unstagedTree_->clearSelection();
        }
        refreshWorkingTreeDiff();
    });
    connect(unstagedTree_, &QTreeWidget::itemDoubleClicked, this, [this] { stageSelected(); });
    connect(stagedTree_, &QTreeWidget::itemDoubleClicked, this, [this] { unstageSelected(); });
    connect(unstagedTree_, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &position) {
                showFileContextMenu(unstagedTree_, false, position);
            });
    connect(stagedTree_, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &position) {
                showFileContextMenu(stagedTree_, true, position);
            });
    connect(diffView_, &DiffView::patchRequested, this,
            [this](const QByteArray &patch, const DiffAction action) {
                applyPatchAction(patch, static_cast<int>(action));
            });

    return page;
}

QWidget *RepositoryView::buildHistoryPage() {
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("historyPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(0);

    auto *toolRow = new QHBoxLayout;
    toolRow->setContentsMargins(4, 0, 0, 2);
    toolRow->setSpacing(8);
    historyScope_ = new FlatComboBox;
    historyScope_->setObjectName(QStringLiteral("historyScope"));
    historyScope_->setFixedWidth(88);
    historyScope_->addItem(tr("All branches"),
                           static_cast<int>(GitHistoryScope::AllBranches));
    historyScope_->addItem(tr("Current branch"),
                           static_cast<int>(GitHistoryScope::CurrentBranch));
    showRemoteBranches_ = new QCheckBox(tr("Show remote branches"));
    showRemoteBranches_->setChecked(true);
    historyOrder_ = new FlatComboBox;
    historyOrder_->setObjectName(QStringLiteral("historyOrder"));
    historyOrder_->setFixedWidth(154);
    historyOrder_->addItem(tr("Sort by date"), true);
    historyOrder_->addItem(tr("Sort by ancestry"), false);
    historyFilter_ = new QLineEdit(page);
    historyFilter_->setObjectName(QStringLiteral("historyAuthorFilter"));
    historyFilter_->setPlaceholderText(tr("Author Name"));
    historyFilter_->setClearButtonEnabled(false);
    historyFilter_->setFixedWidth(150);
    historyFilter_->addAction(
        Icons::icon(Icons::Glyph::Search, Theme::instance()->palette().mutedText),
        QLineEdit::TrailingPosition);

    auto *jumpToButton = new QToolButton;
    jumpToButton->setObjectName(QStringLiteral("historyJumpButton"));
    jumpToButton->setText(tr("Go to:"));
    jumpToButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    jumpToButton->setPopupMode(QToolButton::InstantPopup);
    jumpToButton->setFixedWidth(78);
    auto *jumpMenu = new QMenu(jumpToButton);
    jumpMenu->setObjectName(QStringLiteral("historyJumpMenu"));
    jumpMenu->setMinimumWidth(198);
    jumpToButton->setMenu(jumpMenu);

    toolRow->addWidget(historyScope_);
    toolRow->addWidget(showRemoteBranches_);
    toolRow->addWidget(historyOrder_);
    toolRow->addStretch(1);
    toolRow->addWidget(historyFilter_);
    toolRow->addWidget(jumpToButton);
    layout->addLayout(toolRow);

    auto *verticalSplitter = new QSplitter(Qt::Vertical);
    verticalSplitter->setHandleWidth(RepositorySplitterWidth);

    commitModel_ = new CommitModel(this);
    historyProxy_ = new QSortFilterProxyModel(this);
    historyProxy_->setSourceModel(commitModel_);
    historyProxy_->setFilterKeyColumn(CommitModel::Author);
    historyProxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);

    historyView_ = new QTreeView;
    historyView_->setObjectName(QStringLiteral("historyTree"));
    historyView_->setModel(historyProxy_);
    historyView_->setRootIsDecorated(false);
    historyView_->setUniformRowHeights(true);
    historyView_->setAlternatingRowColors(true);
    historyView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyView_->setSelectionMode(QAbstractItemView::SingleSelection);
    historyView_->setContextMenuPolicy(Qt::CustomContextMenu);
    historyView_->setItemDelegate(new CommitGraphDelegate(historyView_));
    historyView_->header()->setSectionsMovable(false);
    historyView_->header()->setStretchLastSection(true);
    historyView_->header()->setMinimumSectionSize(36);
    historyView_->header()->setSectionResizeMode(QHeaderView::Interactive);

    // Restore interactive history column widths after the first layout pass.
    QTimer::singleShot(0, historyView_, [this] {
        QHeaderView *header = historyView_->header();
        {
            const QSignalBlocker blocker(header);
            const QByteArray savedState =
                QSettings().value(historyHeaderSettingsKey(git_.repositoryRoot())).toByteArray();
            if (savedState.isEmpty() || !header->restoreState(savedState)) {
                const int graphWidth = 80;
                const int dateWidth = 112;
                const int authorWidth = 114;
                const int commitWidth = 60;
                const int descriptionWidth = qMax(
                    240, historyView_->viewport()->width() - graphWidth - dateWidth
                             - authorWidth - commitWidth);
                header->resizeSection(0, graphWidth);
                header->resizeSection(1, descriptionWidth);
                header->resizeSection(2, dateWidth);
                header->resizeSection(3, authorWidth);
                header->resizeSection(4, commitWidth);
            }
            // Keep the Commit section anchored to the viewport edge.
            header->setStretchLastSection(true);
        }
        connect(header, &QHeaderView::sectionResized, historyView_, [this] {
            QSettings().setValue(historyHeaderSettingsKey(git_.repositoryRoot()),
                                 historyView_->header()->saveState());
        });
    });
    verticalSplitter->addWidget(historyView_);

    auto *detailsSplitter = new QSplitter(Qt::Horizontal);
    detailsSplitter->setHandleWidth(RepositorySplitterWidth);
    auto *leftSplitter = new QSplitter(Qt::Vertical);
    leftSplitter->setHandleWidth(RepositorySplitterWidth);

    commitDetails_ = new QTextBrowser;
    commitDetails_->setObjectName(QStringLiteral("commitDetails"));
    commitDetails_->setOpenExternalLinks(false);
    commitDetails_->setOpenLinks(false);
    commitDetails_->document()->setDocumentMargin(0.0);
    leftSplitter->addWidget(commitDetails_);

    commitFilesTree_ = new QTreeWidget;
    commitFilesTree_->setObjectName(QStringLiteral("commitFilesTree"));
    commitFilesTree_->setHeaderHidden(true);
    commitFilesTree_->setRootIsDecorated(false);
    commitFilesTree_->setUniformRowHeights(true);
    commitFilesTree_->setIconSize(QSize(15, 15));
    commitFilesTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(commitFilesTree_, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &position) {
                const QTreeWidgetItem *item = commitFilesTree_->itemAt(position);
                if (item == nullptr) {
                    return;
                }
                const QString path = item->data(0, PathRole).toString();
                const QString hash = selectedCommitHash();
                QMenu menu(this);
                menu.addAction(tr("File history"), this,
                               [this, path, hash] { showFileHistory(path, hash); });
                menu.exec(commitFilesTree_->viewport()->mapToGlobal(position));
            });
    leftSplitter->addWidget(commitFilesTree_);
    leftSplitter->setStretchFactor(0, 5);
    leftSplitter->setStretchFactor(1, 4);
    leftSplitter->setSizes({220, 175});

    commitDiffView_ = new DiffView;
    commitDiffView_->setMode(DiffView::Mode::ReadOnly);
    commitDiffView_->setPlaceholderMessage(tr("Select a file of the commit"));

    detailsSplitter->addWidget(leftSplitter);
    detailsSplitter->addWidget(commitDiffView_);
    detailsSplitter->setStretchFactor(0, 1);
    detailsSplitter->setStretchFactor(1, 1);
    detailsSplitter->setSizes({620, 620});

    verticalSplitter->addWidget(detailsSplitter);
    verticalSplitter->setStretchFactor(0, 3);
    verticalSplitter->setStretchFactor(1, 2);
    verticalSplitter->setSizes({255, 325});
    layout->addWidget(verticalSplitter, 1);

    connect(historyScope_, &QComboBox::currentIndexChanged, this, [this] {
        historyRevision_ = historyScope_->currentData(HistoryRevisionRole).toString();
        refreshHistory();
    });
    connect(historyOrder_, &QComboBox::currentIndexChanged, this, [this] { refreshHistory(); });
    connect(showRemoteBranches_, &QCheckBox::toggled, this, [this] { refreshHistory(); });
    connect(historyFilter_, &QLineEdit::textChanged, this,
            [this](const QString &text) { filterHistoryByAuthor(text); });
    connect(jumpMenu, &QMenu::aboutToShow, this, [this, jumpMenu] {
        jumpMenu->clear();

        QAction *commitAction = jumpMenu->addAction(tr("Commit…"));
        connect(commitAction, &QAction::triggered, this, [this] {
            bool accepted = false;
            const QString revision = QInputDialog::getText(
                this, tr("Go to commit"), tr("Commit SHA:"), QLineEdit::Normal,
                QString(), &accepted);
            if (accepted) {
                jumpToRevision(revision);
            }
        });
        jumpMenu->addSeparator();

        QStringList references;
        for (const GitBranchInfo &branch : git_.branches()) {
            references.append(branch.name);
        }
        for (const GitRemoteInfo &remote : git_.remotes()) {
            references.append(QStringLiteral("%1/HEAD").arg(remote.name));
            for (const QString &branch : remote.branches) {
                references.append(QStringLiteral("%1/%2").arg(remote.name, branch));
            }
        }
        for (const GitTagInfo &tag : git_.tags()) {
            references.append(tag.name);
        }
        if (references.isEmpty()) {
            return;
        }

        // Keep large reference sets scrollable.
        auto *list = new QListWidget;
        list->setObjectName(QStringLiteral("historyJumpList"));
        list->setFrameShape(QFrame::NoFrame);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->addItems(references);

        const int rowHeight = qMax(1, list->sizeHintForRow(0));
        const int visibleRows = qMin(static_cast<int>(references.size()),
                                     JumpListVisibleRows);
        list->setFixedHeight(visibleRows * rowHeight + 2);

        auto *listAction = new QWidgetAction(jumpMenu);
        listAction->setDefaultWidget(list);
        jumpMenu->addAction(listAction);

        connect(list, &QListWidget::itemClicked, this,
                [this, jumpMenu](const QListWidgetItem *item) {
                    const QString reference = item->text();
                    jumpMenu->close();
                    jumpToRevision(reference);
                });
    });
    connect(historyView_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this] { refreshCommitDetails(); });
    connect(historyView_, &QTreeView::customContextMenuRequested, this,
            [this](const QPoint &position) { showHistoryContextMenu(position); });
    connect(commitFilesTree_, &QTreeWidget::currentItemChanged, this, [this] {
        QTreeWidgetItem *fileItem = commitFilesTree_->currentItem();
        if (fileItem == nullptr) {
            commitDiffView_->setPlaceholderMessage(tr("Select a file of the commit"));
            return;
        }
        const QString path = fileItem->data(0, PathRole).toString();
        const QModelIndex commitIndex = currentCommitIndex();
        if (!commitIndex.isValid()) {
            return;
        }
        if (commitIndex.data(CommitRoles::IsUncommitted).toBool()) {
            const bool untracked = fileItem->data(0, IsUntrackedRole).toBool();
            const bool staged = fileItem->data(0, IndexStatusRole).toBool();
            commitDiffView_->setPatch(
                git_.diff(path, staged, untracked).outputText(),
                [this, path, staged, untracked] {
                    const GitCommandResult result =
                        git_.diff(path, staged, untracked, FullDiffContextLines);
                    return result.succeeded() ? result.outputText() : QString();
                });
        } else {
            const QString hash = commitIndex.data(CommitRoles::Hash).toString();
            commitDiffView_->setPatch(
                git_.commitDiff(hash, path).outputText(),
                [this, hash, path] {
                    const GitCommandResult result =
                        git_.commitDiff(hash, path, FullDiffContextLines);
                    return result.succeeded() ? result.outputText() : QString();
                });
        }
    });

    return page;
}

QWidget *RepositoryView::buildSearchPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto *toolRow = new QHBoxLayout;
    searchMode_ = new QComboBox;
    searchMode_->addItem(tr("Message"), static_cast<int>(GitSearchMode::Message));
    searchMode_->addItem(tr("Author"), static_cast<int>(GitSearchMode::Author));
    searchMode_->addItem(tr("File contents"),
                         static_cast<int>(GitSearchMode::FileContents));
    searchMode_->addItem(tr("File path"),
                         static_cast<int>(GitSearchMode::FilePath));
    searchMode_->addItem(tr("Commit SHA"), static_cast<int>(GitSearchMode::Hash));
    searchEdit_ = new QLineEdit;
    searchEdit_->setPlaceholderText(tr("What are we looking for?"));
    searchEdit_->setClearButtonEnabled(true);
    auto *searchButton = new QPushButton(tr("Search", "button"));
    searchButton->setProperty("accent", true);
    searchButton->setIcon(Icons::icon(Icons::Glyph::Search,
                                      Theme::instance()->palette().accentText));
    toolRow->addWidget(searchMode_);
    toolRow->addWidget(searchEdit_, 1);
    toolRow->addWidget(searchButton);
    layout->addLayout(toolRow);

    searchResults_ = new QTreeWidget;
    searchResults_->setHeaderLabels({
        tr("Message"),
        tr("Date"),
        tr("Author"),
        tr("Commit", "noun")
    });
    searchResults_->setRootIsDecorated(false);
    searchResults_->setUniformRowHeights(true);
    searchResults_->setAlternatingRowColors(true);
    searchResults_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(searchResults_, 1);

    auto *hintLabel = new QLabel(
        tr("A double click on a result opens the commit in the history."));
    hintLabel->setObjectName(QStringLiteral("mutedText"));
    layout->addWidget(hintLabel);

    connect(searchButton, &QPushButton::clicked, this, [this] { runSearch(); });
    connect(searchEdit_, &QLineEdit::returnPressed, this, [this] { runSearch(); });
    connect(searchResults_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item) {
                if (item == nullptr) {
                    return;
                }
                const QString hash = item->data(0, CommitRoles::Hash).toString();
                showPage(Page::History);
                const int row = commitModel_->rowForHash(hash);
                if (row >= 0) {
                    selectCommitRow(row);
                    return;
                }
                historyRevision_ = hash;
                refreshHistory();
            });

    return page;
}

GitHistoryOptions RepositoryView::currentHistoryOptions() const {
    GitHistoryOptions options;
    options.scope = static_cast<GitHistoryScope>(historyScope_->currentData().toInt());
    options.maximumCount = QSettings().value(QStringLiteral("historyLimit"), 500).toInt();
    options.revision = historyRevision_;
    options.includeRemotes = showRemoteBranches_->isChecked();
    options.dateOrder = historyOrder_->currentData().toBool();
    return options;
}

void RepositoryView::refreshAll() {
    if (!valid_ || operationInProgress_) {
        return;
    }

    // Repository reads run off the UI thread.
    if (snapshotWatcher_->isRunning()) {
        snapshotQueued_ = true;
        return;
    }

    ++snapshotGeneration_;
    snapshotWatcher_->setFuture(QtConcurrent::run(collectRepositorySnapshot, git_,
                                                  currentHistoryOptions(),
                                                  snapshotGeneration_));
}

void RepositoryView::applySnapshot(const RepositorySnapshot &snapshot) {
    if (snapshot.generation != snapshotGeneration_) {
        return;
    }

    headHash_ = snapshot.headHash;
    currentBranch_ = snapshot.currentBranch;
    state_ = snapshot.state;
    stashes_ = snapshot.stashes;
    branches_ = snapshot.branches;
    tags_ = snapshot.tags;
    remotes_ = snapshot.remotes;
    submodules_ = snapshot.submodules;
    files_ = snapshot.files;
    commits_ = snapshot.commits;
    userName_ = snapshot.userName;
    userEmail_ = snapshot.userEmail;
    historyError_ = snapshot.historyError;
    ahead_ = snapshot.ahead;
    behind_ = snapshot.behind;

    if (autoFetchReporting_ && snapshot.generation >= autoFetchReportGeneration_) {
        autoFetchReporting_ = false;
        if (behind_ > autoFetchBehind_) {
            Q_EMIT messagePosted(tr("%n new commit(s) on %1", "", behind_ - autoFetchBehind_)
                                     .arg(currentUpstream()),
                                 15'000);
        }
    }

    if (!snapshot.statusError.isEmpty()) {
        Q_EMIT messagePosted(snapshot.statusError, 8'000);
    }

    refreshHistoryScope(branches_);
    filesPage_->setReferences(branches_, tags_);
    refreshHeader();
    refreshNavigation();
    refreshStatus();
    refreshHistory();
    Q_EMIT repositoryChanged();

    if (snapshotQueued_) {
        snapshotQueued_ = false;
        correctingHistoryScope_ = false;
        refreshAll();
        return;
    }

    // Refresh once more if rebuilding the scope changed the requested revision.
    if (!correctingHistoryScope_ && currentHistoryOptions() != snapshot.historyOptions) {
        correctingHistoryScope_ = true;
        refreshAll();
        return;
    }
    correctingHistoryScope_ = false;
}

void RepositoryView::refreshHistoryScope(const QList<GitBranchInfo> &branches) {
    if (historyScope_ == nullptr) {
        return;
    }

    const int previousScope = historyScope_->currentData().toInt();
    const QString previousRevision = historyScope_->currentData(HistoryRevisionRole).toString();
    const QString wantedRevision = !historyRevision_.isEmpty() ? historyRevision_
                                                                : previousRevision;

    const QSignalBlocker blocker(historyScope_);
    historyScope_->clear();
    historyScope_->addItem(tr("All branches"),
                           static_cast<int>(GitHistoryScope::AllBranches));
    historyScope_->addItem(tr("Current branch"),
                           static_cast<int>(GitHistoryScope::CurrentBranch));
    for (const GitBranchInfo &branch : branches) {
        historyScope_->addItem(branch.name,
                               static_cast<int>(GitHistoryScope::CurrentBranch));
        historyScope_->setItemData(historyScope_->count() - 1, branch.name,
                                   HistoryRevisionRole);
    }

    int selectedIndex = -1;
    if (!wantedRevision.isEmpty()) {
        selectedIndex = historyScope_->findData(wantedRevision, HistoryRevisionRole);
    }
    if (selectedIndex < 0) {
        selectedIndex = previousScope == static_cast<int>(GitHistoryScope::CurrentBranch) ? 1 : 0;
    }
    historyScope_->setCurrentIndex(selectedIndex);
}

void RepositoryView::refreshHeader() {
    const QString stateText = state_.description();
    stateBanner_->setVisible(state_.isBusy());
    stateBadge_->setText(state_.isBusy()
                             ? tr("%1 — finish the operation or abort it")
                                   .arg(stateText)
                             : stateText);

    const QString &author = userName_;
    const QString &email = userEmail_;
    if (authorLabel_ != nullptr) {
        authorLabel_->setText(author.isEmpty()
                                  ? tr("The author is not configured — set it in the "
                                       "settings")
                                  : QStringLiteral("%1 <%2>").arg(author, email));
    }
}

void RepositoryView::refreshNavigation() {
    const int pageKind = currentPage_ == Page::History
                             ? NavigationHistory
                             : (currentPage_ == Page::Search ? NavigationSearch
                                                             : NavigationFileStatus);
    const int previousKind = navigationTree_->currentItem() != nullptr
                                 ? navigationTree_->currentItem()->data(0, NavigationKindRole).toInt()
                                 : pageKind;
    const QString previousValue = navigationTree_->currentItem() != nullptr
                                      ? navigationTree_->currentItem()->data(0, NavigationValueRole).toString()
                                      : QString();

    const QSignalBlocker blocker(navigationTree_);
    navigationTree_->clear();
    QTreeWidgetItem *itemToSelect = nullptr;

    const ThemePalette &palette = Theme::instance()->palette();
    const QColor sectionColor = palette.sectionText;
    const QColor sectionIconColor = Theme::instance()->mode() == Theme::Mode::Light
                                        ? QColor(QStringLiteral("#4F6B92"))
                                        : sectionColor;
    auto *branchesSection = addSection(navigationTree_, tr("Branches"),
                                       navigationSectionIcon(Icons::Glyph::Branch,
                                                             sectionIconColor),
                                       true);
    const QList<GitBranchInfo> &branches = branches_;
    QHash<QString, QTreeWidgetItem *> folders;

    for (const GitBranchInfo &branch : branches) {
        QTreeWidgetItem *parent = branchesSection;
        const QStringList components = branch.name.split(u'/');
        QString prefix;
        for (qsizetype index = 0; index + 1 < components.size(); ++index) {
            prefix += (prefix.isEmpty() ? QString() : QStringLiteral("/")) + components.at(index);
            if (!folders.contains(prefix)) {
                auto *folder = addNavigationItem(parent, components.at(index),
                                                 NavigationBranchFolder, prefix);
                folder->setExpanded(true);
                folder->setForeground(0, Theme::instance()->palette().mutedText);
                folders.insert(prefix, folder);
            }
            parent = folders.value(prefix);
        }

        auto *item = addNavigationItem(parent, components.constLast(), NavigationBranch,
                                       branch.name);
        item->setData(0, NavigationExtraRole, branch.upstream);
        item->setData(0, NavigationAheadRole, branch.ahead);
        item->setData(0, NavigationBehindRole, branch.behind);
        item->setToolTip(0, QStringLiteral("%1\n%2\n%3")
                                .arg(branch.name, branch.subject,
                                     branch.upstream.isEmpty()
                                         ? tr("Not tracking")
                                         : tr("Tracks %1").arg(branch.upstream)));
        if (branch.current) {
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
            item->setForeground(0, navigationTextColor());
        }
        if (previousKind == NavigationBranch && previousValue == branch.name) {
            itemToSelect = item;
        }
    }
    if (branches.isEmpty()) {
        addNavigationItem(branchesSection, tr("No branches"), NavigationPlaceholder)
            ->setDisabled(true);
    }

    const QList<GitTagInfo> &tags = tags_;
    auto *tagsSection = addSection(navigationTree_, tr("Tags"),
                                   navigationSectionIcon(Icons::Glyph::Tag,
                                                         sectionIconColor),
                                   false);
    for (const GitTagInfo &tag : tags) {
        auto *item = addNavigationItem(tagsSection, tag.name, NavigationTag, tag.name);
        item->setToolTip(0, tag.subject);
        if (previousKind == NavigationTag && previousValue == tag.name) {
            itemToSelect = item;
            tagsSection->setExpanded(true);
        }
    }
    if (tags.isEmpty()) {
        addNavigationItem(tagsSection, tr("No tags"), NavigationPlaceholder)->setDisabled(true);
    }

    auto *remotesSection = addSection(navigationTree_, tr("Remotes"),
                                      navigationSectionIcon(Icons::Glyph::Remote,
                                                            sectionIconColor),
                                      false);
    const QList<GitRemoteInfo> &remotes = remotes_;
    for (const GitRemoteInfo &remote : remotes) {
        auto *remoteItem = addNavigationItem(remotesSection, remote.name, NavigationRemote,
                                             remote.name);
        remoteItem->setToolTip(0, remote.url);
        remoteItem->setExpanded(false);
        for (const QString &branch : remote.branches) {
            const QString reference = QStringLiteral("%1/%2").arg(remote.name, branch);
            auto *item = addNavigationItem(remoteItem, branch, NavigationRemoteBranch, reference);
            if (previousKind == NavigationRemoteBranch && previousValue == reference) {
                itemToSelect = item;
                remotesSection->setExpanded(true);
                remoteItem->setExpanded(true);
            }
        }
    }
    if (remotes.isEmpty()) {
        addNavigationItem(remotesSection, tr("No remotes"), NavigationPlaceholder)
            ->setDisabled(true);
    }

    auto *stashSection = addSection(navigationTree_, tr("Stashes"),
                                    navigationSectionIcon(Icons::Glyph::Stash,
                                                          sectionIconColor),
                                    false);
    for (const GitStashInfo &stash : stashes_) {
        auto *item = addNavigationItem(
            stashSection,
            QStringLiteral("%1 — %2").arg(stash.reference,
                                          elideMiddle(stash.message, 40)),
            NavigationStash, QString::number(stash.index));
        item->setToolTip(0, QStringLiteral("%1\n%2")
                                .arg(stash.message, formatCommitTimestamp(stash.createdAt)));
        if (previousKind == NavigationStash
            && previousValue == QString::number(stash.index)) {
            itemToSelect = item;
            stashSection->setExpanded(true);
        }
    }
    if (stashes_.isEmpty()) {
        addNavigationItem(stashSection, tr("No stashes"), NavigationPlaceholder)
            ->setDisabled(true);
    }

    const QList<GitSubmoduleInfo> &submodules = submodules_;
    if (!submodules.isEmpty()) {
        auto *submoduleSection = addSection(navigationTree_, tr("Submodules"),
                                            navigationSectionIcon(Icons::Glyph::Submodule,
                                                                  sectionIconColor),
                                            false);
        for (const GitSubmoduleInfo &submodule : submodules) {
            addNavigationItem(submoduleSection, submodule.path, NavigationSubmodule,
                              submodule.path);
        }
    }

    navigationTree_->setCurrentItem(itemToSelect);
}

void RepositoryView::refreshStatus() {
    const QString filter = fileFilter_->text().trimmed();
    QList<GitFileStatus> unstaged;
    QList<GitFileStatus> staged;
    hasStagedChanges_ = false;

    for (const GitFileStatus &file : files_) {
        if (!filter.isEmpty() && !file.path.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        if (file.hasWorkingTreeChanges()) {
            unstaged.append(file);
        }
        if (file.hasStagedChanges()) {
            staged.append(file);
            hasStagedChanges_ = true;
        }
    }

    populateFileTree(unstagedTree_, unstaged, false);
    populateFileTree(stagedTree_, staged, true);

    unstagedCaption_->setText(tr("UNSTAGED FILES  (%1)")
                                  .arg(unstaged.size()));
    stagedCaption_->setText(tr("STAGED FILES  (%1)").arg(staged.size()));

    // Always preview something: fall back to the first changed file.
    if (unstagedTree_->selectedItems().isEmpty() && stagedTree_->selectedItems().isEmpty()) {
        QTreeWidget *target = unstagedTree_->topLevelItemCount() > 0 ? unstagedTree_
                                                                     : stagedTree_;
        if (target->topLevelItemCount() > 0) {
            if (QTreeWidgetItem *leaf = firstLeaf(target->topLevelItem(0)); leaf != nullptr) {
                const QSignalBlocker blocker(target);
                target->setCurrentItem(leaf);
                leaf->setSelected(true);
            }
        }
    }

    commitButton_->setEnabled(!operationInProgress_
                              && !commitMessage_->toPlainText().trimmed().isEmpty()
                              && (hasStagedChanges_ || amendCheck_->isChecked()));

    refreshHeader();
    refreshWorkingTreeDiff();
}

void RepositoryView::populateFileTree(QTreeWidget *tree, const QList<GitFileStatus> &files,
                                      const bool staged) {
    const QSignalBlocker blocker(tree);
    QStringList previousSelection;
    for (const QTreeWidgetItem *item : tree->selectedItems()) {
        previousSelection.append(item->data(0, PathRole).toString());
    }

    tree->clear();
    tree->setRootIsDecorated(treeMode_);
    QHash<QString, QTreeWidgetItem *> folders;
    QTreeWidgetItem *itemToSelect = nullptr;

    for (const GitFileStatus &file : files) {
        const QChar status = staged ? file.indexStatus : file.workTreeStatus;
        QTreeWidgetItem *parent = nullptr;
        QString label = file.path;

        if (treeMode_) {
            const QStringList components = file.path.split(u'/');
            QString prefix;
            for (qsizetype index = 0; index + 1 < components.size(); ++index) {
                prefix += (prefix.isEmpty() ? QString() : QStringLiteral("/"))
                          + components.at(index);
                if (!folders.contains(prefix)) {
                    auto *folder = parent == nullptr
                                       ? new QTreeWidgetItem(tree, {components.at(index)})
                                       : new QTreeWidgetItem(parent, {components.at(index)});
                    folder->setData(0, IsDirectoryRole, true);
                    folder->setExpanded(true);
                    folder->setForeground(0, Theme::instance()->palette().mutedText);
                    folders.insert(prefix, folder);
                }
                parent = folders.value(prefix);
            }
            label = components.constLast();
        }

        auto *item = parent == nullptr ? new QTreeWidgetItem(tree, {label})
                                       : new QTreeWidgetItem(parent, {label});
        item->setIcon(0, statusBadge(status));
        item->setData(0, PathRole, file.path);
        item->setData(0, OriginalPathRole, file.originalPath);
        item->setData(0, IndexStatusRole, staged);
        item->setData(0, WorkStatusRole, QString(status));
        item->setData(0, IsUntrackedRole, file.isUntracked());
        item->setData(0, IsConflictedRole, file.isConflicted());
        item->setData(0, IsDirectoryRole, false);

        QString tooltip = QStringLiteral("%1\n%2").arg(file.path, statusDescription(status));
        if (!file.originalPath.isEmpty()) {
            tooltip += tr("\nPreviously: %1").arg(file.originalPath);
        }
        item->setToolTip(0, tooltip);
        if (file.isConflicted()) {
            item->setForeground(0, Theme::instance()->palette().danger);
        }
        if (previousSelection.contains(file.path) && itemToSelect == nullptr) {
            itemToSelect = item;
        }
    }

    if (itemToSelect != nullptr) {
        tree->setCurrentItem(itemToSelect);
        itemToSelect->setSelected(true);
    }
}

QStringList RepositoryView::selectedPaths(QTreeWidget *tree) const {
    QStringList paths;
    const std::function<void(QTreeWidgetItem *)> collect = [&](QTreeWidgetItem *item) {
        if (item->data(0, IsDirectoryRole).toBool()) {
            for (int index = 0; index < item->childCount(); ++index) {
                collect(item->child(index));
            }
            return;
        }
        const QString path = item->data(0, PathRole).toString();
        if (!path.isEmpty()) {
            paths.append(path);
        }
    };

    for (QTreeWidgetItem *item : tree->selectedItems()) {
        collect(item);
    }
    paths.removeDuplicates();
    return paths;
}

void RepositoryView::refreshWorkingTreeDiff() {
    const bool staged = !stagedTree_->selectedItems().isEmpty();
    QTreeWidget *tree = staged ? stagedTree_ : unstagedTree_;
    QTreeWidgetItem *item = tree->currentItem();
    if (item == nullptr || !tree->selectedItems().contains(item)) {
        item = tree->selectedItems().isEmpty() ? nullptr : tree->selectedItems().constFirst();
    }

    if (item == nullptr || item->data(0, IsDirectoryRole).toBool()) {
        diffCaption_->setText(tr("CHANGES"));
        diffView_->setMode(DiffView::Mode::ReadOnly);
        diffView_->setPlaceholderMessage(
            files_.isEmpty() ? tr("The working tree is clean — there are no changes")
                             : tr("Select a file to see the changes"));
        return;
    }

    const QString path = item->data(0, PathRole).toString();
    const bool untracked = item->data(0, IsUntrackedRole).toBool();
    diffCaption_->setText(QStringLiteral("%1  —  %2")
                              .arg(staged ? tr("STAGED")
                                          : tr("WORKING TREE"),
                                   path.toUpper()));

    const GitCommandResult result = git_.diff(path, staged, untracked);
    if (!result.succeeded()) {
        diffView_->setMode(DiffView::Mode::ReadOnly);
        diffView_->setPlaceholderMessage(result.errorText());
        return;
    }

    diffView_->setMode(untracked ? DiffView::Mode::ReadOnly
                                 : (staged ? DiffView::Mode::Staged : DiffView::Mode::Unstaged));
    const QString patch = result.outputText();
    if (patch.trimmed().isEmpty()) {
        diffView_->setPlaceholderMessage(tr("Git returned no differences for this file."));
    } else {
        diffView_->setPatch(
            patch,
            [this, path, staged, untracked] {
                const GitCommandResult fullResult =
                    git_.diff(path, staged, untracked, FullDiffContextLines);
                return fullResult.succeeded() ? fullResult.outputText() : QString();
            });
    }
}

void RepositoryView::refreshHistory() {
    const QModelIndex previous = currentCommitIndex();
    const QString previousHash = previous.isValid()
                                     ? previous.data(CommitRoles::Hash).toString()
                                     : QString();
    const bool showUncommitted = !files_.isEmpty();

    {
        // A model reset drops the selection, which would otherwise reload the
        // commit details once for the empty state and once for the new row.
        const QSignalBlocker blocker(historyView_->selectionModel());
        commitModel_->setHistory(commits_, showUncommitted, headHash_);
        filterHistoryByAuthor(historyFilter_->text());

        const int row = commitModel_->rowForHash(previousHash);
        selectCommitRow(row >= 0 ? row : 0);
    }

    if (!historyError_.isEmpty()) {
        commitDetails_->setPlainText(historyError_);
    } else if (commits_.isEmpty() && !showUncommitted) {
        commitDetails_->setPlainText(tr("The repository has no commits yet."));
        commitFilesTree_->clear();
        commitDiffView_->setPlaceholderMessage(QString());
    } else {
        refreshCommitDetails();
    }
}

QModelIndex RepositoryView::currentCommitIndex() const {
    const QModelIndex proxyIndex = historyView_->currentIndex();
    return proxyIndex.isValid() ? historyProxy_->mapToSource(proxyIndex) : QModelIndex();
}

void RepositoryView::selectCommitRow(const int row) {
    if (row < 0 || row >= commitModel_->rowCount()) {
        return;
    }
    const QModelIndex proxyIndex = historyProxy_->mapFromSource(
        commitModel_->index(row, CommitModel::Message));
    if (!proxyIndex.isValid()) {
        // The row exists but the author filter is hiding it.
        return;
    }
    historyView_->setCurrentIndex(proxyIndex);
    historyView_->scrollTo(proxyIndex, QAbstractItemView::PositionAtCenter);
}

void RepositoryView::filterHistoryByAuthor(const QString &text) {
    historyProxy_->setFilterFixedString(text.trimmed());
}

void RepositoryView::jumpToRevision(const QString &revision) {
    const QString query = revision.trimmed();
    if (query.isEmpty()) {
        return;
    }
    historyFilter_->clear();

    const GitCommandResult resolved = git_.runCustom({
        QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("--quiet"),
        QStringLiteral("%1^{commit}").arg(query)
    });
    if (!resolved.succeeded()) {
        Q_EMIT messagePosted(tr("Commit “%1” was not found in the loaded history.")
                                 .arg(query), 5'000);
        return;
    }
    const QString hash = resolved.outputText().trimmed();

    const auto selectHash = [this, &hash] {
        const int row = commitModel_->rowForHash(hash);
        if (row < 0) {
            return false;
        }
        selectCommitRow(row);
        return true;
    };

    if (selectHash()) {
        return;
    }
    historyRevision_ = hash;
    refreshHistory();
    selectHash();
}

void RepositoryView::refreshCommitDetails() {
    const QModelIndex index = currentCommitIndex();
    commitFilesTree_->clear();
    commitDiffView_->setPlaceholderMessage(tr("Select a file"));

    if (!index.isValid()) {
        commitDetails_->clear();
        return;
    }

    if (index.data(CommitRoles::IsUncommitted).toBool()) {
        // Translate labels separately from the HTML layout.
        commitDetails_->setHtml(
            QStringLiteral("<p><b>%1</b></p>").arg(tr("Uncommitted changes"))
            + detailsTable(detailsRow(tr("Files:"), QString::number(files_.size()))
                           + detailsRow(tr("Branch:"), currentBranch_.toHtmlEscaped())));
        for (const GitFileStatus &file : files_) {
            const QChar status = file.hasStagedChanges() ? file.indexStatus : file.workTreeStatus;
            auto *fileItem = new QTreeWidgetItem(commitFilesTree_, {file.path});
            fileItem->setIcon(0, statusBadge(status));
            fileItem->setData(0, PathRole, file.path);
            fileItem->setData(0, IndexStatusRole, file.hasStagedChanges());
            fileItem->setData(0, IsUntrackedRole, file.isUntracked());
        }
        if (commitFilesTree_->topLevelItemCount() > 0) {
            commitFilesTree_->setCurrentItem(commitFilesTree_->topLevelItem(0));
        }
        return;
    }

    const QString hash = index.data(CommitRoles::Hash).toString();
    if (hash.isEmpty()) {
        commitDetails_->clear();
        return;
    }

    const GitCommitInfo *commit = nullptr;
    for (const GitCommitInfo &candidate : commits_) {
        if (candidate.hash == hash) {
            commit = &candidate;
            break;
        }
    }

    if (commit != nullptr) {
        // List metadata as a label/value block above the message.
        QStringList parents;
        for (const QString &parent : commit->parents) {
            parents.append(parent.left(10));
        }

        QString details;
        details += detailsRow(tr("Commit:"),
                              QStringLiteral("%1 [%2]").arg(commit->hash,
                                                            commit->shortHash));
        if (!parents.isEmpty()) {
            details += detailsRow(tr("Parents:"),
                                  parents.join(QStringLiteral("&nbsp; ")));
        }
        details += detailsRow(tr("Author:"),
                              QStringLiteral("%1 &lt;%2&gt;")
                                  .arg(commit->author.toHtmlEscaped(),
                                       commit->authorEmail.toHtmlEscaped()));
        details += detailsRow(tr("Date:"), formatCommitTimestamp(commit->authoredAt));
        if (commit->committer != commit->author) {
            details += detailsRow(tr("Committer:"), commit->committer.toHtmlEscaped());
        }
        if (!commit->references.isEmpty()) {
            details += detailsRow(tr("Refs:"), commit->references.toHtmlEscaped());
        }
        details = detailsTable(details);

        QString message = commit->subject.toHtmlEscaped();
        if (!commit->body.isEmpty()) {
            message += QStringLiteral("<br>") + commit->body.toHtmlEscaped()
                                                    .replace(u'\n', QStringLiteral("<br>"));
        }
        details += QStringLiteral("<p style='margin-top:8px'>%1</p>").arg(message);
        commitDetails_->setHtml(details);
    } else {
        commitDetails_->setPlainText(git_.showCommit(hash).outputText());
    }

    const QList<GitChangedFile> changedFiles = git_.commitFiles(hash);
    for (const GitChangedFile &file : changedFiles) {
        auto *fileItem = new QTreeWidgetItem(commitFilesTree_, {file.path});
        fileItem->setIcon(0, statusBadge(file.status));
        fileItem->setData(0, PathRole, file.path);
        QString tooltip = QStringLiteral("%1\n%2").arg(file.path, statusDescription(file.status));
        if (!file.originalPath.isEmpty()) {
            tooltip += tr("\nPreviously: %1").arg(file.originalPath);
        }
        if (file.additions > 0 || file.deletions > 0) {
            tooltip += QStringLiteral("\n+%1 / -%2").arg(file.additions).arg(file.deletions);
        }
        fileItem->setToolTip(0, tooltip);
    }
    if (commitFilesTree_->topLevelItemCount() > 0) {
        commitFilesTree_->setCurrentItem(commitFilesTree_->topLevelItem(0));
    } else {
        commitDiffView_->setPatch(
            git_.commitDiff(hash, QString()).outputText(),
            [this, hash] {
                const GitCommandResult result =
                    git_.commitDiff(hash, QString(), FullDiffContextLines);
                return result.succeeded() ? result.outputText() : QString();
            });
    }
}

void RepositoryView::showPage(const Page page) {
    currentPage_ = page;
    pages_->setCurrentIndex(static_cast<int>(page));

    QPushButton *button = fileStatusButton_;
    if (page == Page::History) {
        button = historyButton_;
    } else if (page == Page::Search) {
        button = searchButton_;
    } else if (page == Page::Files) {
        button = filesButton_;
    }
    if (button != nullptr && !button->isChecked()) {
        button->setChecked(true);
    }

    if (navigationTree_ != nullptr) {
        const int wantedKind = page == Page::History
                                   ? NavigationHistory
                                   : (page == Page::Search ? NavigationSearch
                                                           : NavigationFileStatus);
        const QSignalBlocker blocker(navigationTree_);
        const QList<QTreeWidgetItem *> items =
            navigationTree_->findItems(QStringLiteral("*"),
                                       Qt::MatchWildcard | Qt::MatchRecursive);
        for (QTreeWidgetItem *item : items) {
            if (item->data(0, NavigationKindRole).toInt() == wantedKind) {
                navigationTree_->setCurrentItem(item);
                break;
            }
        }
    }

    if (valid_) {
        QSettings().setValue(pageSettingsKey(git_.repositoryRoot()),
                             static_cast<int>(page));
    }
}

void RepositoryView::showFileStatusPage() {
    showPage(Page::FileStatus);
}

void RepositoryView::showHistoryPage() {
    showPage(Page::History);
}

void RepositoryView::showFilesPage() {
    showPage(Page::Files);
}

void RepositoryView::showFileHistory(const QString &path, const QString &revision) {
    if (path.isEmpty()) {
        return;
    }
    showPage(Page::Files);
    filesPage_->showFile(path, revision);
}

void RepositoryView::showSearchPage() {
    showPage(Page::Search);
    searchEdit_->setFocus();
}

void RepositoryView::activateNavigationItem(QTreeWidgetItem *item) {
    if (item == nullptr) {
        return;
    }

    const int kind = item->data(0, NavigationKindRole).toInt();
    const QString value = item->data(0, NavigationValueRole).toString();

    switch (kind) {
        case NavigationFileStatus:
            showPage(Page::FileStatus);
            break;
        case NavigationSearch:
            showPage(Page::Search);
            break;
        case NavigationHistory:
            historyRevision_.clear();
            {
                const QSignalBlocker blocker(historyScope_);
                historyScope_->setCurrentIndex(0);
            }
            showPage(Page::History);
            refreshHistory();
            break;
        case NavigationBranch:
        case NavigationRemoteBranch:
        case NavigationTag:
            historyRevision_ = value;
            {
                const QSignalBlocker blocker(historyScope_);
                const int branchIndex = kind == NavigationBranch
                                            ? historyScope_->findData(value,
                                                                      HistoryRevisionRole)
                                            : -1;
                historyScope_->setCurrentIndex(branchIndex >= 0 ? branchIndex : 0);
            }
            showPage(Page::History);
            refreshHistory();
            break;
        case NavigationStash: {
            showPage(Page::History);
            const int index = value.toInt();
            commitDetails_->setPlainText(git_.stashDiff(index, QString()).outputText());
            commitFilesTree_->clear();
            for (const GitChangedFile &file : git_.stashFiles(index)) {
                auto *fileItem = new QTreeWidgetItem(commitFilesTree_, {file.path});
                fileItem->setIcon(0, statusBadge(file.status));
                fileItem->setData(0, PathRole, file.path);
            }
            commitDiffView_->setPatch(
                git_.stashDiff(index, QString()).outputText(),
                [this, index] {
                    const GitCommandResult result =
                        git_.stashDiff(index, QString(), FullDiffContextLines);
                    return result.succeeded() ? result.outputText() : QString();
                });
            break;
        }
        default:
            break;
    }
}

void RepositoryView::handleNavigationDoubleClick(QTreeWidgetItem *item) {
    if (item == nullptr) {
        return;
    }
    const int kind = item->data(0, NavigationKindRole).toInt();
    const QString value = item->data(0, NavigationValueRole).toString();

    if (kind == NavigationBranch) {
        checkoutBranch(value);
    } else if (kind == NavigationRemoteBranch) {
        checkoutRemoteBranch(value);
    } else if (kind == NavigationSubmodule) {
        static_cast<void>(PlatformServices::instance().openPath(
            QDir(git_.repositoryRoot()).filePath(value)));
    }
}

void RepositoryView::showNavigationContextMenu(const QPoint &position) {
    QTreeWidgetItem *item = navigationTree_->itemAt(position);
    if (item == nullptr) {
        return;
    }

    const int kind = item->data(0, NavigationKindRole).toInt();
    const QString value = item->data(0, NavigationValueRole).toString();
    QMenu menu(this);

    if (kind == NavigationBranch) {
        const bool isCurrent = value == currentBranch_;
        if (!isCurrent) {
            menu.addAction(Icons::icon(Icons::Glyph::Checkout), tr("Switch"),
                           this, [this, value] { checkoutBranch(value); });
            menu.addAction(Icons::icon(Icons::Glyph::Merge),
                           tr("Merge into the current branch"), this, [this, value] {
                               MergeDialog dialog(value, currentBranch_, this);
                               if (dialog.exec() == QDialog::Accepted) {
                                   runOperation(tr("Merge"), [this, &dialog, value] {
                                       return git_.merge(value, dialog.noFastForward(),
                                                         dialog.squash(), dialog.commitResult());
                                   });
                               }
                           });
            menu.addAction(Icons::icon(Icons::Glyph::Rebase),
                           tr("Rebase the current branch here"), this,
                           [this, value] {
                               runOperation(QStringLiteral("Rebase"),
                                            [this, value] { return git_.rebase(value); });
                           });
        }
        menu.addSeparator();
        menu.addAction(tr("Rename…"), this, [this, value] {
            bool accepted = false;
            const QString name = QInputDialog::getText(
                this, tr("Rename branch"), tr("New name:"),
                QLineEdit::Normal, value, &accepted).trimmed();
            if (accepted && !name.isEmpty()) {
                runOperation(tr("Renaming the branch"),
                             [this, value, name] { return git_.renameBranch(value, name); });
            }
        });
        menu.addAction(Icons::icon(Icons::Glyph::Push), tr("Push the branch"), this,
                       [this, value] {
                           runRemoteOperation(QStringLiteral("Push"), [this, value] {
                               return git_.push(QStringLiteral("origin"), {value}, true, false,
                                                false);
                           });
                       });
        if (!isCurrent) {
            menu.addAction(Icons::icon(Icons::Glyph::Trash), tr("Delete branch"), this,
                           [this, value] {
                               const QMessageBox::StandardButton answer = QMessageBox::question(
                                   this, tr("Delete branch"),
                                   tr("Delete the branch “%1”?").arg(value));
                               if (answer != QMessageBox::Yes) {
                                   return;
                               }
                               if (!runOperation(tr("Deleting the branch"),
                                                 [this, value] {
                                                     return git_.deleteBranch(value, false);
                                                 })) {
                                   const QMessageBox::StandardButton force = QMessageBox::question(
                                       this, tr("The branch is not merged"),
                                       tr("Delete “%1” anyway?").arg(value));
                                   if (force == QMessageBox::Yes) {
                                       runOperation(tr("Deleting the branch"),
                                                    [this, value] {
                                                        return git_.deleteBranch(value, true);
                                                    });
                                   }
                               }
                           });
        }
    } else if (kind == NavigationRemoteBranch) {
        menu.addAction(Icons::icon(Icons::Glyph::Checkout),
                       tr("Create a local branch and switch to it"), this,
                       [this, value] { checkoutRemoteBranch(value); });
        menu.addAction(Icons::icon(Icons::Glyph::Merge), tr("Merge into the current branch"),
                       this, [this, value] {
                           runOperation(tr("Merge"), [this, value] {
                               return git_.merge(value, false, false, true);
                           });
                       });
        menu.addSeparator();
        menu.addAction(Icons::icon(Icons::Glyph::Trash),
                       tr("Delete the branch in the remote repository"), this,
                       [this, value] {
                           const qsizetype separator = value.indexOf(u'/');
                           if (separator <= 0) {
                               return;
                           }
                           const QString remote = value.left(separator);
                           const QString branch = value.mid(separator + 1);
                           const QMessageBox::StandardButton answer = QMessageBox::question(
                               this, tr("Delete remote branch"),
                               tr("Delete “%1” in “%2”?").arg(branch, remote));
                           if (answer == QMessageBox::Yes) {
                               runRemoteOperation(tr("Deleting the branch"),
                                                  [this, remote, branch] {
                                                      return git_.deleteRemoteBranch(remote,
                                                                                     branch);
                                                  });
                           }
                       });
    } else if (kind == NavigationTag) {
        menu.addAction(Icons::icon(Icons::Glyph::Checkout), tr("Go to the tag"), this,
                       [this, value] {
                           runOperation(QStringLiteral("Checkout"),
                                        [this, value] { return git_.checkoutRevision(value); });
                       });
        menu.addAction(Icons::icon(Icons::Glyph::Push), tr("Push the tag"), this,
                       [this, value] {
                           runRemoteOperation(tr("Pushing the tag"), [this, value] {
                               return git_.push(QStringLiteral("origin"), {value}, false, false,
                                                false);
                           });
                       });
        menu.addAction(Icons::icon(Icons::Glyph::Trash), tr("Delete tag"), this,
                       [this, value] {
                           runOperation(tr("Deleting the tag"),
                                        [this, value] { return git_.deleteTag(value); });
                       });
    } else if (kind == NavigationStash) {
        const int index = value.toInt();
        menu.addAction(Icons::icon(Icons::Glyph::StashPop), tr("Apply and drop"),
                       this, [this, index] {
                           runOperation(QStringLiteral("Stash pop"),
                                        [this, index] { return git_.stashApply(index, true); });
                       });
        menu.addAction(tr("Apply and keep in the list"), this, [this, index] {
            runOperation(QStringLiteral("Stash apply"),
                         [this, index] { return git_.stashApply(index, false); });
        });
        menu.addAction(Icons::icon(Icons::Glyph::Trash), tr("Delete"), this,
                       [this, index] {
                           runOperation(QStringLiteral("Stash drop"),
                                        [this, index] { return git_.stashDrop(index); });
                       });
    } else if (kind == NavigationRemote) {
        menu.addAction(Icons::icon(Icons::Glyph::Fetch), tr("Fetch"),
                       this, [this, value] {
                           runRemoteOperation(QStringLiteral("Fetch"), [this, value] {
                               return git_.fetch(value, true, true);
                           });
                       });
        menu.addAction(Icons::icon(Icons::Glyph::Trash), tr("Delete the remote"),
                       this, [this, value] {
                           runOperation(tr("Deleting the remote"),
                                        [this, value] { return git_.removeRemote(value); });
                       });
    } else if (kind == NavigationSection || kind == NavigationPlaceholder) {
        menu.addAction(Icons::icon(Icons::Glyph::Branch), tr("New branch…"), this,
                       [this] { createBranchInteractive(); });
        menu.addAction(Icons::icon(Icons::Glyph::Remote), tr("Add remote…"),
                       this, [this] { addRemoteInteractive(); });
    }

    if (!menu.isEmpty()) {
        menu.exec(navigationTree_->viewport()->mapToGlobal(position));
    }
}

void RepositoryView::showFileContextMenu(QTreeWidget *tree, const bool staged,
                                         const QPoint &position) {
    const QStringList paths = selectedPaths(tree);
    if (paths.isEmpty()) {
        return;
    }

    QMenu menu(this);
    if (staged) {
        menu.addAction(tr("Unstage", "context menu"), this, [this] { unstageSelected(); });
    } else {
        menu.addAction(tr("Stage", "context menu"), this, [this] { stageSelected(); });
        menu.addAction(Icons::icon(Icons::Glyph::Discard), tr("Discard the changes"),
                       this, [this] { discardSelectedFiles(); });
    }

    QTreeWidgetItem *item = tree->itemAt(position);
    if (item != nullptr && item->data(0, IsConflictedRole).toBool()) {
        menu.addSeparator();
        menu.addAction(tr("Resolve: keep our changes"), this,
                       [this, paths] {
                           runOperation(tr("Resolving the conflict"), [this, paths] {
                               return git_.resolveWith(paths, true);
                           });
                       });
        menu.addAction(tr("Resolve: take their changes"), this,
                       [this, paths] {
                           runOperation(tr("Resolving the conflict"), [this, paths] {
                               return git_.resolveWith(paths, false);
                           });
                       });
    }

    menu.addSeparator();
    menu.addAction(tr("File history"), this, [this, paths] {
        showFileHistory(paths.constFirst());
    });
    menu.addAction(tr("Open the file"), this, [this, tree] { openSelectedFile(tree); });
    menu.addAction(tr("Show in the folder"), this, [this, paths] {
        static_cast<void>(PlatformServices::instance().revealInFileManager(
            QDir(git_.repositoryRoot()).filePath(paths.constFirst())));
    });
    menu.addAction(tr("Copy the path"), this, [paths] {
        QGuiApplication::clipboard()->setText(paths.join(u'\n'));
    });
    menu.addSeparator();
    menu.addAction(tr("Add to .gitignore"), this, [this, paths] {
        runOperation(tr("Updating .gitignore"),
                     [this, paths] { return git_.ignore(paths); });
    });

    menu.exec(tree->viewport()->mapToGlobal(position));
}

void RepositoryView::openSelectedFile(QTreeWidget *tree) {
    const QStringList paths = selectedPaths(tree);
    if (paths.isEmpty()) {
        return;
    }
    static_cast<void>(PlatformServices::instance().openPath(
        QDir(git_.repositoryRoot()).filePath(paths.constFirst())));
}

void RepositoryView::stageSelected() {
    const QStringList paths = selectedPaths(unstagedTree_);
    if (paths.isEmpty()) {
        return;
    }
    runOperation(tr("Staging"), [this, paths] { return git_.stage(paths); });
}

void RepositoryView::unstageSelected() {
    const QStringList paths = selectedPaths(stagedTree_);
    if (paths.isEmpty()) {
        return;
    }
    runOperation(tr("Unstaging"),
                 [this, paths] { return git_.unstage(paths); });
}

void RepositoryView::stageAll() {
    runOperation(tr("Staging all files"), [this] { return git_.stageAll(); });
}

void RepositoryView::unstageAll() {
    runOperation(tr("Clearing the index"), [this] { return git_.unstageAll(); });
}

void RepositoryView::discardSelectedFiles() {
    QStringList tracked;
    QStringList untracked;
    for (QTreeWidgetItem *item : unstagedTree_->selectedItems()) {
        const QString path = item->data(0, PathRole).toString();
        if (path.isEmpty()) {
            continue;
        }
        if (item->data(0, IsUntrackedRole).toBool()) {
            untracked.append(path);
        } else {
            tracked.append(path);
        }
    }

    if (tracked.isEmpty() && untracked.isEmpty()) {
        Q_EMIT messagePosted(tr("Select files in the list of unstaged files."),
                             4'000);
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this, tr("Discard the changes"),
        tr("The changes in %1 file(s) will be lost irreversibly. Continue?")
            .arg(tracked.size() + untracked.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    if (!tracked.isEmpty()) {
        runOperation(tr("Discarding the changes"),
                     [this, tracked] { return git_.discard(tracked, false); }, untracked.isEmpty());
    }
    if (!untracked.isEmpty()) {
        runOperation(tr("Deleting the new files"),
                     [this, untracked] { return git_.discard(untracked, true); });
    }
}

void RepositoryView::createCommit() {
    const QString message = commitMessage_->toPlainText().trimmed();
    if (message.isEmpty()) {
        return;
    }
    const bool amend = amendCheck_->isChecked();
    if (!hasStagedChanges_ && !amend) {
        Q_EMIT messagePosted(tr("There are no staged changes."), 4'000);
        return;
    }

    pushAfterCommitPending_ = pushAfterCommitCheck_->isChecked();
    if (!runOperation(tr("Commit", "noun"),
                      [this, message, amend] { return git_.commit(message, amend); })) {
        pushAfterCommitPending_ = false;
        return;
    }

    commitMessage_->clear();
    amendCheck_->setChecked(false);
    Q_EMIT messagePosted(tr("The commit was created"), 4'000);

    if (pushAfterCommitPending_) {
        pushAfterCommitPending_ = false;
        startPush();
    }
}

void RepositoryView::applyPatchAction(const QByteArray &patch, const int action) {
    const auto diffAction = static_cast<DiffAction>(action);
    if (diffAction == DiffAction::Discard) {
        const QMessageBox::StandardButton answer = QMessageBox::warning(
            this, tr("Discard the changes"),
            tr("The selected lines will be removed from the working tree. Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    const QString title = diffAction == DiffAction::Stage
                              ? tr("Staging the fragment")
                              : (diffAction == DiffAction::Unstage
                                     ? tr("Unstaging the fragment")
                                     : tr("Discarding the fragment"));

    runOperation(title, [this, patch, diffAction] {
        switch (diffAction) {
            case DiffAction::Stage: return git_.applyPatch(patch, true, false);
            case DiffAction::Unstage: return git_.applyPatch(patch, true, true);
            case DiffAction::Discard: return git_.applyPatch(patch, false, true);
        }
        return GitCommandResult();
    });
}

void RepositoryView::showHistoryContextMenu(const QPoint &position) {
    const QModelIndex clicked = historyView_->indexAt(position);
    if (!clicked.isValid()) {
        return;
    }
    historyView_->setCurrentIndex(clicked);
    const QModelIndex index = historyProxy_->mapToSource(clicked);

    if (index.data(CommitRoles::IsUncommitted).toBool()) {
        QMenu menu(this);
        menu.addAction(Icons::icon(Icons::Glyph::Commit), tr("Go to the commit"),
                       this, [this] { focusCommitMessage(); });
        menu.addAction(Icons::icon(Icons::Glyph::Stash), tr("Stash the changes…"),
                       this, [this] { createStash(); });
        menu.exec(historyView_->viewport()->mapToGlobal(position));
        return;
    }

    const QString hash = index.data(CommitRoles::Hash).toString();
    if (hash.isEmpty()) {
        return;
    }
    const QString shortHash =
        index.sibling(index.row(), CommitModel::Commit).data().toString();

    QMenu menu(this);
    menu.addAction(Icons::icon(Icons::Glyph::Checkout), tr("Go to this commit"),
                   this, [this, hash] {
                       const QMessageBox::StandardButton answer = QMessageBox::question(
                           this, QStringLiteral("Checkout"),
                           tr("Switch to a detached HEAD state?"));
                       if (answer == QMessageBox::Yes) {
                           runOperation(QStringLiteral("Checkout"),
                                        [this, hash] { return git_.checkoutRevision(hash); });
                       }
                   });
    menu.addAction(Icons::icon(Icons::Glyph::Branch), tr("Create a branch from here…"),
                   this, [this, hash, shortHash] {
                       BranchDialog dialog(tr("commit %1").arg(shortHash), this);
                       if (dialog.exec() == QDialog::Accepted) {
                           const QString name = dialog.branchName();
                           const bool checkout = dialog.checkoutAfterCreate();
                           runOperation(tr("Creating the branch"), [this, name, hash,
                                                                          checkout] {
                               return git_.createBranch(name, hash, checkout);
                           });
                       }
                   });
    menu.addAction(Icons::icon(Icons::Glyph::Tag), tr("Create a tag…"), this,
                   [this, hash, shortHash] {
                       TagDialog dialog(shortHash, this);
                       if (dialog.exec() == QDialog::Accepted) {
                           const QString name = dialog.tagName();
                           const QString message = dialog.tagMessage();
                           runOperation(tr("Creating the tag"), [this, name, hash,
                                                                         message] {
                               return git_.createTag(name, hash, message);
                           });
                       }
                   });
    menu.addSeparator();
    menu.addAction(Icons::icon(Icons::Glyph::Merge), tr("Merge into the current branch"),
                   this, [this, hash] {
                       runOperation(tr("Merge"),
                                    [this, hash] { return git_.merge(hash, false, false, true); });
                   });
    menu.addAction(Icons::icon(Icons::Glyph::CherryPick), QStringLiteral("Cherry-pick"), this,
                   [this, hash] {
                       runOperation(QStringLiteral("Cherry-pick"),
                                    [this, hash] { return git_.cherryPick(hash); });
                   });
    menu.addAction(Icons::icon(Icons::Glyph::Discard), tr("Revert the commit"),
                   this, [this, hash] {
                       runOperation(QStringLiteral("Revert"),
                                    [this, hash] { return git_.revert(hash); });
                   });
    menu.addAction(Icons::icon(Icons::Glyph::Reset),
                   tr("Reset the current branch here…"), this,
                   [this, hash, shortHash] {
                       ResetDialog dialog(shortHash, this);
                       if (dialog.exec() == QDialog::Accepted) {
                           const GitResetMode mode = dialog.resetMode();
                           runOperation(QStringLiteral("Reset"),
                                        [this, hash, mode] { return git_.reset(hash, mode); });
                       }
                   });
    menu.addSeparator();
    menu.addAction(tr("Copy the SHA"), this, [hash] {
        QGuiApplication::clipboard()->setText(hash);
    });
    const QString subject = index.sibling(index.row(), CommitModel::Message).data().toString();
    menu.addAction(tr("Copy the message"), this, [subject] {
        QGuiApplication::clipboard()->setText(subject);
    });

    menu.exec(historyView_->viewport()->mapToGlobal(position));
}

QString RepositoryView::selectedCommitHash() const {
    return currentCommitIndex().data(CommitRoles::Hash).toString();
}

void RepositoryView::runSearch() {
    const QString query = searchEdit_->text().trimmed();
    searchResults_->clear();
    if (query.isEmpty()) {
        return;
    }

    const auto mode = static_cast<GitSearchMode>(searchMode_->currentData().toInt());
    QString errorMessage;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QList<GitCommitInfo> results = git_.search(mode, query, 500, &errorMessage);
    QApplication::restoreOverrideCursor();

    if (!errorMessage.isEmpty()) {
        Q_EMIT messagePosted(errorMessage, 6'000);
    }

    for (const GitCommitInfo &commit : results) {
        auto *item = new QTreeWidgetItem(searchResults_);
        item->setText(0, commit.subject);
        item->setText(1, formatCommitTimestamp(commit.authoredAt));
        item->setText(2, commit.author);
        item->setText(3, commit.shortHash);
        item->setData(0, CommitRoles::Hash, commit.hash);
    }

    Q_EMIT messagePosted(tr("Commits found: %1").arg(results.size()), 5'000);
}

void RepositoryView::focusCommitMessage() {
    showPage(Page::FileStatus);
    commitMessage_->setFocus();
}

void RepositoryView::checkoutInteractive() {
    QStringList options;
    for (const GitBranchInfo &branch : git_.branches()) {
        if (!branch.current) {
            options.append(branch.name);
        }
    }
    for (const GitTagInfo &tag : git_.tags()) {
        options.append(QStringLiteral("tag: %1").arg(tag.name));
    }
    if (options.isEmpty()) {
        Q_EMIT messagePosted(tr("There are no branches or tags available."), 4'000);
        return;
    }

    bool accepted = false;
    const QString choice = QInputDialog::getItem(
        this, tr("Switch"), tr("Branch or tag:"), options, 0,
        false, &accepted);
    if (!accepted || choice.isEmpty()) {
        return;
    }

    if (choice.startsWith(QStringLiteral("tag: "))) {
        const QString tag = choice.mid(5);
        runOperation(QStringLiteral("Checkout"),
                     [this, tag] { return git_.checkoutRevision(tag); });
    } else {
        checkoutBranch(choice);
    }
}

void RepositoryView::checkoutBranch(const QString &name) {
    if (name == currentBranch_) {
        return;
    }
    runOperation(tr("Switching the branch"),
                 [this, name] { return git_.checkoutBranch(name); });
}

void RepositoryView::checkoutRemoteBranch(const QString &remoteBranch) {
    const qsizetype separator = remoteBranch.indexOf(u'/');
    if (separator <= 0) {
        return;
    }
    const QString localName = remoteBranch.mid(separator + 1);

    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, tr("Local branch"),
        tr("Name of the local branch for %1:").arg(remoteBranch),
        QLineEdit::Normal, localName, &accepted).trimmed();
    if (!accepted || name.isEmpty()) {
        return;
    }

    runOperation(QStringLiteral("Checkout"), [this, remoteBranch, name] {
        return git_.checkoutRemoteBranch(remoteBranch, name);
    });
}

void RepositoryView::createBranchInteractive() {
    const QString startPoint = currentBranch_.isEmpty()
                                   ? QStringLiteral("HEAD")
                                   : tr("branch %1").arg(currentBranch_);
    BranchDialog dialog(startPoint, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString name = dialog.branchName();
    const bool checkout = dialog.checkoutAfterCreate();
    runOperation(tr("Creating the branch"),
                 [this, name, checkout] { return git_.createBranch(name, {}, checkout); });
}

void RepositoryView::mergeInteractive() {
    QStringList options;
    for (const GitBranchInfo &branch : git_.branches()) {
        if (!branch.current) {
            options.append(branch.name);
        }
    }
    for (const GitRemoteInfo &remote : git_.remotes()) {
        for (const QString &branch : remote.branches) {
            options.append(QStringLiteral("%1/%2").arg(remote.name, branch));
        }
    }
    if (options.isEmpty()) {
        Q_EMIT messagePosted(tr("There are no branches to merge."), 4'000);
        return;
    }

    bool accepted = false;
    const QString source = QInputDialog::getItem(
        this, tr("Merge"), tr("Merge branch:"), options, 0, false,
        &accepted);
    if (!accepted || source.isEmpty()) {
        return;
    }

    MergeDialog dialog(source, currentBranch_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const bool noFastForward = dialog.noFastForward();
    const bool squash = dialog.squash();
    const bool commitResult = dialog.commitResult();
    runOperation(tr("Merge"), [this, source, noFastForward, squash, commitResult] {
        return git_.merge(source, noFastForward, squash, commitResult);
    });
}

void RepositoryView::createTagInteractive() {
    const QString hash = pages_->currentIndex() == static_cast<int>(Page::History)
                             ? selectedCommitHash()
                             : QString();
    const QString description = hash.isEmpty() ? QStringLiteral("HEAD") : hash.left(8);

    TagDialog dialog(description, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString name = dialog.tagName();
    const QString message = dialog.tagMessage();
    runOperation(tr("Creating the tag"),
                 [this, name, hash, message] { return git_.createTag(name, hash, message); });
}

void RepositoryView::createStash() {
    if (files_.isEmpty()) {
        Q_EMIT messagePosted(tr("There are no changes to stash."), 4'000);
        return;
    }

    StashDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString message = dialog.message();
    const bool keepStaged = dialog.keepStaged();
    const bool untracked = dialog.includeUntracked();
    runOperation(QStringLiteral("Stash"), [this, message, keepStaged, untracked] {
        return git_.stashSave(message, keepStaged, untracked);
    });
}

void RepositoryView::popLatestStash() {
    if (stashes_.isEmpty()) {
        Q_EMIT messagePosted(tr("The stash list is empty."), 4'000);
        return;
    }
    runOperation(QStringLiteral("Stash pop"), [this] { return git_.stashApply(0, true); });
}

void RepositoryView::startFetch() {
    runRemoteOperation(QStringLiteral("Fetch"), [this] { return git_.fetch({}, true, true); });
}

void RepositoryView::startPull() {
    runRemoteOperation(QStringLiteral("Pull"), [this] { return git_.pull({}, {}, false); });
}

void RepositoryView::startPush() {
    const QList<GitRemoteInfo> remotes = git_.remotes();
    if (remotes.isEmpty()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("There are no remote repositories"),
            tr("Add a remote repository now?"));
        if (answer == QMessageBox::Yes) {
            addRemoteInteractive();
        }
        return;
    }

    PushDialog dialog(remotes, git_.branches(), currentBranch_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString remote = dialog.remote();
    const QStringList branches = dialog.selectedBranches();
    const bool upstream = dialog.setUpstream();
    const bool tags = dialog.pushTags();
    const bool force = dialog.forcePush();
    if (branches.isEmpty() && !tags) {
        Q_EMIT messagePosted(tr("No branch is selected."), 4'000);
        return;
    }

    runRemoteOperation(QStringLiteral("Push"), [this, remote, branches, upstream, tags, force] {
        return git_.push(remote, branches, upstream, tags, force);
    });
}

void RepositoryView::addRemoteInteractive() {
    RemoteDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString name = dialog.remoteName();
    const QString url = dialog.remoteUrl();
    if (name.isEmpty() || url.isEmpty()) {
        return;
    }
    runOperation(tr("Adding the remote"),
                 [this, name, url] { return git_.addRemote(name, url); });
}

void RepositoryView::openTerminal() {
    if (!PlatformServices::instance().openTerminal(git_.repositoryRoot())) {
        Q_EMIT messagePosted(tr("The terminal could not be started."), 5'000);
    }
}

void RepositoryView::openFileManager() {
    static_cast<void>(PlatformServices::instance().openPath(git_.repositoryRoot()));
}

void RepositoryView::showPreferences() {
    PreferencesDialog dialog(git_.userName(), git_.userEmail(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (!dialog.userName().isEmpty()) {
        static_cast<void>(git_.runCustom({QStringLiteral("config"), QStringLiteral("user.name"),
                                          dialog.userName()}));
    }
    if (!dialog.userEmail().isEmpty()) {
        static_cast<void>(git_.runCustom({QStringLiteral("config"), QStringLiteral("user.email"),
                                          dialog.userEmail()}));
    }
    QSettings settings;
    settings.setValue(QStringLiteral("historyLimit"), dialog.historyLimit());
    settings.setValue(QStringLiteral("autoFetch/enabled"), dialog.autoFetchEnabled());
    settings.setValue(QStringLiteral("autoFetch/intervalMinutes"),
                      dialog.autoFetchIntervalMinutes());
    Theme::instance()->setMode(dialog.darkTheme() ? Theme::Mode::Dark : Theme::Mode::Light);
    scheduleAutoFetch();
    refreshAll();
}

bool RepositoryView::runOperation(const QString &title,
                                  const std::function<GitCommandResult()> &operation,
                                  const bool refresh) {
    if (operationInProgress_) {
        return false;
    }

    // The explicit refresh below covers metadata written by this operation.
    blockingOperation_ = true;
    updateWatcherSuspension();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const GitCommandResult result = operation();
    QApplication::restoreOverrideCursor();
    blockingOperation_ = false;
    updateWatcherSuspension();

    if (!result.succeeded()) {
        reportError(title, result);
        if (refresh) {
            refreshAll();
        }
        return false;
    }

    const QString report = result.reportText().trimmed();
    Q_EMIT messagePosted(report.isEmpty() ? tr("%1: done").arg(title)
                                          : QStringLiteral("%1: %2").arg(title,
                                                                         report.section(u'\n', 0, 0)),
                         5'000);
    if (refresh) {
        refreshAll();
    }
    return true;
}

void RepositoryView::runRemoteOperation(const QString &title,
                                        const std::function<GitCommandResult()> &operation) {
    if (operationInProgress_) {
        return;
    }

    operationInProgress_ = true;
    operationTitle_ = title;
    updateWatcherSuspension();
    Q_EMIT busyChanged(true);
    Q_EMIT messagePosted(tr("%1 is running…").arg(title), 0);
    operationWatcher_->setFuture(QtConcurrent::run(operation));
}

void RepositoryView::finishRemoteOperation() {
    const GitCommandResult result = operationWatcher_->result();
    operationInProgress_ = false;
    updateWatcherSuspension();
    Q_EMIT busyChanged(false);

    if (!result.succeeded()) {
        reportError(operationTitle_, result);
    } else {
        const QString report = result.reportText().trimmed();
        Q_EMIT messagePosted(report.isEmpty()
                                 ? tr("%1: done").arg(operationTitle_)
                                 : QStringLiteral("%1: %2").arg(operationTitle_,
                                                                report.section(u'\n', -1)),
                             8'000);
    }

    refreshAll();
}

QString RepositoryView::currentUpstream() const {
    for (const GitBranchInfo &branch : branches_) {
        if (branch.current && !branch.upstream.isEmpty()) {
            return branch.upstream;
        }
    }
    return currentBranch_;
}

void RepositoryView::scheduleAutoFetch() {
    if (!valid_) {
        return;
    }
    // Poll settings even while disabled so changes made in another tab apply.
    autoFetchTimer_->start(autoFetchIntervalMinutes() * 60'000);
}

void RepositoryView::runAutoFetch() {
    scheduleAutoFetch();

    if (!valid_ || !autoFetchEnabled() || autoFetchRunning_ || operationInProgress_
        || blockingOperation_) {
        return;
    }

    autoFetchRunning_ = true;
    autoFetchBehind_ = behind_;
    updateWatcherSuspension();

    GitClient git = git_;
    autoFetchWatcher_->setFuture(QtConcurrent::run(autoFetchPool(), [git] {
        return git.fetch({}, true, true, AutoFetchTimeoutMs);
    }));
}

void RepositoryView::finishAutoFetch() {
    autoFetchReporting_ = autoFetchWatcher_->result().succeeded();
    autoFetchReportGeneration_ = snapshotGeneration_ + 1;
    autoFetchRunning_ = false;
    updateWatcherSuspension();
    refreshAll();
}

void RepositoryView::updateWatcherSuspension() {
    diskWatcher_->setSuspended(operationInProgress_ || blockingOperation_
                               || autoFetchRunning_);
}

void RepositoryView::reportError(const QString &title, const GitCommandResult &result) {
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(title);
    box.setText(tr("The git command failed."));
    box.setInformativeText(result.errorText().section(u'\n', 0, 4));
    box.setDetailedText(result.reportText());
    box.exec();
}
