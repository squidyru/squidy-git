// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include "core/gitclient.h"
#include "core/repositorysnapshot.h"

#include <QFutureWatcher>
#include <QModelIndex>
#include <QWidget>

#include <functional>

class CommitModel;
class DiffView;
class FilesPage;
class RepositoryWatcher;
class QCheckBox;
class QSortFilterProxyModel;
class QTreeView;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTextBrowser;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

/// A single repository tab: sidebar, File Status, History and Search views.
class RepositoryView final : public QWidget {
    Q_OBJECT

public:
    explicit RepositoryView(const QString &path, QWidget *parent = nullptr);

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] QString repositoryRoot() const;
    [[nodiscard]] QString repositoryName() const;
    [[nodiscard]] QString currentBranchName() const;
    [[nodiscard]] int aheadCount() const;
    [[nodiscard]] int behindCount() const;
    [[nodiscard]] int changeCount() const;
    [[nodiscard]] bool isBusy() const;
    [[nodiscard]] bool hasStagedChanges() const;

public Q_SLOTS:
    void refreshAll();
    void focusCommitMessage();
    void checkoutInteractive();
    void discardSelectedFiles();
    void createStash();
    void popLatestStash();
    void startFetch();
    void startPull();
    void startPush();
    void createBranchInteractive();
    void mergeInteractive();
    void createTagInteractive();
    void openTerminal();
    void openFileManager();
    void addRemoteInteractive();
    void showPreferences();
    void showFileStatusPage();
    void showHistoryPage();
    void showSearchPage();
    void showFilesPage();

Q_SIGNALS:
    void repositoryChanged();
    void messagePosted(const QString &message, int timeoutMs);
    void busyChanged(bool busy);

private:
    enum class Page {
        FileStatus = 0,
        History = 1,
        Search = 2,
        Files = 3
    };

    // --- Construction -----------------------------------------------------
    QWidget *buildStateBanner();
    QWidget *buildSidebar();
    QWidget *buildViewSwitcher();
    QWidget *buildFileStatusPage();
    QWidget *buildHistoryPage();
    QWidget *buildSearchPage();

    // --- Refreshing -------------------------------------------------------
    /// Copies history controls into a value safe to pass to a worker.
    [[nodiscard]] GitHistoryOptions currentHistoryOptions() const;
    void applySnapshot(const RepositorySnapshot &snapshot);
    void refreshHeader();
    void refreshHistoryScope(const QList<GitBranchInfo> &branches);
    void refreshNavigation();
    void refreshStatus();
    void refreshHistory();
    void refreshWorkingTreeDiff();
    void refreshCommitDetails();
    void filterHistoryByAuthor(const QString &text);
    void jumpToRevision(const QString &revision);
    void showPage(Page page);

    // --- File status ------------------------------------------------------
    void populateFileTree(QTreeWidget *tree, const QList<GitFileStatus> &files, bool staged);
    [[nodiscard]] QStringList selectedPaths(QTreeWidget *tree) const;
    void stageSelected();
    void unstageSelected();
    void stageAll();
    void unstageAll();
    void createCommit();
    void applyPatchAction(const QByteArray &patch, int action);
    void showFileContextMenu(QTreeWidget *tree, bool staged, const QPoint &position);
    void openSelectedFile(QTreeWidget *tree);
    /// Opens the Files page on @p path, at @p revision or at the working copy.
    void showFileHistory(const QString &path, const QString &revision = {});

    // --- History ----------------------------------------------------------
    /// Returns the current row mapped to the source model.
    [[nodiscard]] QModelIndex currentCommitIndex() const;
    void selectCommitRow(int row);
    void showHistoryContextMenu(const QPoint &position);
    void showNavigationContextMenu(const QPoint &position);
    void activateNavigationItem(QTreeWidgetItem *item);
    void handleNavigationDoubleClick(QTreeWidgetItem *item);
    void runSearch();

    // --- Operations -------------------------------------------------------
    bool runOperation(const QString &title, const std::function<GitCommandResult()> &operation,
                      bool refresh = true);
    void runRemoteOperation(const QString &title,
                            const std::function<GitCommandResult()> &operation);
    void finishRemoteOperation();

    // --- Periodic remote check --------------------------------------------
    [[nodiscard]] QString currentUpstream() const;
    void scheduleAutoFetch();
    void runAutoFetch();
    void finishAutoFetch();
    void updateWatcherSuspension();

    void reportError(const QString &title, const GitCommandResult &result);
    [[nodiscard]] QString selectedCommitHash() const;
    void checkoutBranch(const QString &name);
    void checkoutRemoteBranch(const QString &remoteBranch);

    GitClient git_;
    bool valid_ = false;

    QWidget *stateBanner_ = nullptr;
    QLabel *stateBadge_ = nullptr;
    QPushButton *continueButton_ = nullptr;
    QPushButton *abortButton_ = nullptr;

    QTreeWidget *navigationTree_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QSplitter *workspaceSplitter_ = nullptr;
    QPushButton *fileStatusButton_ = nullptr;
    QPushButton *historyButton_ = nullptr;
    QPushButton *searchButton_ = nullptr;
    QPushButton *filesButton_ = nullptr;

    QLineEdit *fileFilter_ = nullptr;
    QToolButton *treeModeButton_ = nullptr;
    QTreeWidget *unstagedTree_ = nullptr;
    QTreeWidget *stagedTree_ = nullptr;
    QLabel *unstagedCaption_ = nullptr;
    QLabel *stagedCaption_ = nullptr;
    QLabel *diffCaption_ = nullptr;
    DiffView *diffView_ = nullptr;
    QPlainTextEdit *commitMessage_ = nullptr;
    QCheckBox *amendCheck_ = nullptr;
    QCheckBox *pushAfterCommitCheck_ = nullptr;
    QPushButton *commitButton_ = nullptr;
    QLabel *authorLabel_ = nullptr;

    QComboBox *historyScope_ = nullptr;
    QComboBox *historyOrder_ = nullptr;
    QCheckBox *showRemoteBranches_ = nullptr;
    QLineEdit *historyFilter_ = nullptr;
    QTreeView *historyView_ = nullptr;
    CommitModel *commitModel_ = nullptr;
    QSortFilterProxyModel *historyProxy_ = nullptr;
    QTextBrowser *commitDetails_ = nullptr;
    QTreeWidget *commitFilesTree_ = nullptr;
    DiffView *commitDiffView_ = nullptr;

    FilesPage *filesPage_ = nullptr;

    QLineEdit *searchEdit_ = nullptr;
    QComboBox *searchMode_ = nullptr;
    QTreeWidget *searchResults_ = nullptr;

    // Values from the latest completed refresh.
    QList<GitFileStatus> files_;
    QList<GitCommitInfo> commits_;
    QList<GitStashInfo> stashes_;
    QList<GitBranchInfo> branches_;
    QList<GitTagInfo> tags_;
    QList<GitRemoteInfo> remotes_;
    QList<GitSubmoduleInfo> submodules_;
    QString currentBranch_;
    QString headHash_;
    QString historyRevision_;
    QString userName_;
    QString userEmail_;
    QString historyError_;
    GitRepositoryState state_;
    Page currentPage_ = Page::FileStatus;
    int ahead_ = 0;
    int behind_ = 0;
    bool hasStagedChanges_ = false;
    bool treeMode_ = false;
    bool operationInProgress_ = false;
    bool blockingOperation_ = false;
    bool pushAfterCommitPending_ = false;
    QString operationTitle_;
    QFutureWatcher<GitCommandResult> *operationWatcher_ = nullptr;

    QTimer *autoFetchTimer_ = nullptr;
    QFutureWatcher<GitCommandResult> *autoFetchWatcher_ = nullptr;
    bool autoFetchRunning_ = false;
    bool autoFetchReporting_ = false;
    quint64 autoFetchReportGeneration_ = 0;
    int autoFetchBehind_ = 0;

    RepositoryWatcher *diskWatcher_ = nullptr;
    QFutureWatcher<RepositorySnapshot> *snapshotWatcher_ = nullptr;
    quint64 snapshotGeneration_ = 0;
    /// Coalesces refresh requests received while a refresh is running.
    bool snapshotQueued_ = false;
    /// Prevents repeated correction when history controls change during refresh.
    bool correctingHistoryScope_ = false;
};
