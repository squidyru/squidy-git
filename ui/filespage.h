// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include "core/gitclient.h"

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QWidget>

class DiffView;
class ImagePreview;
class PdfPreview;
class SourceView;
class SyntaxHighlighter;
class QComboBox;
class QLabel;
class QLineEdit;
class QTimer;
class QPlainTextEdit;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

/// One directory level read in the background.
struct FileTreeLoad {
    quint64 generation = 0;
    QString revision;
    QString directory;
    QList<GitTreeEntry> entries;
    QString error;
};

/// Every tracked file of a revision, read once so that typing in the filter
/// does not start a Git process on each keystroke.
struct FileListingLoad {
    quint64 generation = 0;
    QString revision;
    QList<GitTreeEntry> entries;
    QString error;
};

/// The timeline of one file, newest revision first.
struct FileHistoryLoad {
    quint64 generation = 0;
    QString path;
    QList<GitFileRevision> revisions;
    QString error;
};

/// What the viewer ended up with for the selected revision.
enum class FileViewerContent {
    Placeholder,
    Patch,
    Text,
    Image,
    Pdf
};

/// Contents or a patch prepared for the viewer.
struct FileViewerLoad {
    quint64 generation = 0;
    /// The path at the shown revision, which picks the highlighting rules.
    QString path;
    QString patch;
    QString contents;
    QImage image;
    /// The blob itself, kept for the viewers that read bytes.
    QByteArray bytes;
    QString placeholder;
    /// Appended to the caption: the format and size of a picture, or the cut
    /// of a hex dump.
    QString note;
    FileViewerContent content = FileViewerContent::Placeholder;
    /// A hex dump goes through the source view but has no language.
    bool highlight = true;
};

/// The Files workspace: the repository tree at a revision, the timeline of the
/// selected file and a read-only viewer. Nothing here writes to the repository.
class FilesPage final : public QWidget {
    Q_OBJECT

public:
    explicit FilesPage(const QString &repositoryRoot, QWidget *parent = nullptr);

    /// Refills the revision selector, keeping the current choice when it survives.
    void setReferences(const QList<GitBranchInfo> &branches, const QList<GitTagInfo> &tags);
    /// Rereads the current level and timeline.
    void reload();
    /// Selects @p path at @p revision, expanding the tree down to it. An empty
    /// revision means the working copy.
    void showFile(const QString &path, const QString &revision);

Q_SIGNALS:
    void commitActivated(const QString &hash);
    void messagePosted(const QString &message, int timeoutMs);

private:
    // --- Construction -----------------------------------------------------
    QWidget *buildToolRow();
    QWidget *buildTree();
    QWidget *buildTimeline();
    QWidget *buildViewer();

    // --- Tree -------------------------------------------------------------
    [[nodiscard]] QString currentRevision() const;
    void reloadTree();
    void requestDirectory(const QString &directory);
    /// Runs one directory read at a time so quick expansions are not dropped.
    void startNextDirectory();

    // --- Filter -----------------------------------------------------------
    /// Reads the whole listing of the revision when it is not at hand yet.
    void runFilter();
    void applyListing(const FileListingLoad &load);
    /// Rebuilds the tree from the paths whose file name matches @p query, with
    /// every level above a match expanded.
    void showMatches(const QString &query);
    void applyDirectory(const FileTreeLoad &load);
    void expandDirectory(QTreeWidgetItem *item);
    void selectTreeFile(QTreeWidgetItem *item);
    void showTreeContextMenu(const QPoint &position);
    /// Walks pendingPath_ one level further once its parent arrived.
    void followPendingPath(const QString &directory);

    // --- Timeline ---------------------------------------------------------
    void requestHistory(const QString &path);
    void applyHistory(const FileHistoryLoad &load);
    void showTimelineContextMenu(const QPoint &position);
    [[nodiscard]] const GitFileRevision *selectedRevision() const;

    // --- Viewer -----------------------------------------------------------
    void requestViewer();
    void applyViewer(const FileViewerLoad &load);
    void clearFileSelection();

    GitClient git_;

    QComboBox *revision_ = nullptr;
    QLineEdit *fileFilter_ = nullptr;
    QTimer *filterTimer_ = nullptr;
    QLabel *treeCaption_ = nullptr;
    QLabel *timelineCaption_ = nullptr;
    QLabel *viewerCaption_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    QTreeWidget *timeline_ = nullptr;
    QComboBox *viewerMode_ = nullptr;
    QStackedWidget *viewer_ = nullptr;
    DiffView *diffView_ = nullptr;
    SourceView *contentsView_ = nullptr;
    ImagePreview *imageView_ = nullptr;
    PdfPreview *pdfView_ = nullptr;
    SyntaxHighlighter *highlighter_ = nullptr;

    QFutureWatcher<FileTreeLoad> *treeWatcher_ = nullptr;
    QFutureWatcher<FileListingLoad> *listingWatcher_ = nullptr;
    QFutureWatcher<FileHistoryLoad> *historyWatcher_ = nullptr;
    QFutureWatcher<FileViewerLoad> *viewerWatcher_ = nullptr;

    /// Directory items of the levels that are currently displayed.
    QHash<QString, QTreeWidgetItem *> directoryItems_;
    QStringList directoryQueue_;
    /// Flat listing of the current revision, filled on the first search.
    QList<GitTreeEntry> listing_;
    QString listingRevision_;
    QList<GitFileRevision> revisions_;
    QString currentPath_;
    /// Path the tree is still expanding towards after showFile().
    QString pendingPath_;
    quint64 treeGeneration_ = 0;
    quint64 listingGeneration_ = 0;
    quint64 historyGeneration_ = 0;
    quint64 viewerGeneration_ = 0;
};
