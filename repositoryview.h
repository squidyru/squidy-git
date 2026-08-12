// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include "gitclient.h"

#include <QFutureWatcher>
#include <QWidget>

#include <functional>

class DiffView;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTextBrowser;
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

Q_SIGNALS:
    void repositoryChanged();
    void messagePosted(const QString &message, int timeoutMs);
    void busyChanged(bool busy);

private:
    enum class Page {
        FileStatus = 0,
        History = 1,
        Search = 2
    };

    // --- Construction -----------------------------------------------------
    QWidget *buildStateBanner();
    QWidget *buildSidebar();
    QWidget *buildViewSwitcher();
    QWidget *buildFileStatusPage();
    QWidget *buildHistoryPage();
    QWidget *buildSearchPage();

    // --- Refreshing -------------------------------------------------------
    void refreshHeader();
    void refreshNavigation();
    void refreshStatus();
    void refreshHistory();
    void refreshWorkingTreeDiff();
    void refreshCommitDetails();
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

    // --- History ----------------------------------------------------------
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
    QLineEdit *jumpToEdit_ = nullptr;
    QTreeWidget *historyTree_ = nullptr;
    QTextBrowser *commitDetails_ = nullptr;
    QTreeWidget *commitFilesTree_ = nullptr;
    DiffView *commitDiffView_ = nullptr;

    QLineEdit *searchEdit_ = nullptr;
    QComboBox *searchMode_ = nullptr;
    QTreeWidget *searchResults_ = nullptr;

    QList<GitFileStatus> files_;
    QList<GitCommitInfo> commits_;
    QList<GitStashInfo> stashes_;
    QString currentBranch_;
    QString headHash_;
    QString historyRevision_;
    GitRepositoryState state_;
    Page currentPage_ = Page::FileStatus;
    int ahead_ = 0;
    int behind_ = 0;
    bool hasStagedChanges_ = false;
    bool treeMode_ = false;
    bool operationInProgress_ = false;
    bool pushAfterCommitPending_ = false;
    QString operationTitle_;
    QFutureWatcher<GitCommandResult> *operationWatcher_ = nullptr;
};
