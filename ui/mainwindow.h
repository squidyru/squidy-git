// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QMainWindow>
#include <QStringList>

class RepositoryView;
class UpdateChecker;
class QAction;
class QDockWidget;
class QGraphicsDropShadowEffect;
class QLabel;
class QListWidget;
class QMenu;
class QMenuBar;
class QPlainTextEdit;
class QProgressBar;
class QResizeEvent;
class QShowEvent;
class QStackedWidget;
class QTabBar;
class QToolBar;
class QToolButton;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void buildInterface();
    void buildToolbar();
    void buildMenus();
    void buildLanguageMenu(QMenu *menu);
    void selectLanguage(const QString &language);
    QWidget *buildTitleBar();
    QWidget *buildWelcomePage();
    QDockWidget *buildCommandLog();

    void toggleMaximized();
    void updateWindowButtons();
    void updateWindowFrame();
    void updateResizeCursor(const QPoint &position);
    [[nodiscard]] Qt::Edges resizeEdgesAt(const QPoint &position) const;

    void openRepositoryDialog();
    bool openRepository(const QString &path, bool activate = true);
    void cloneRepository();
    void createRepository();
    void showAboutDialog();
    void closeTab(int index);
    void closeCurrentTab();
    void showTabContextMenu(const QPoint &position);

    [[nodiscard]] RepositoryView *currentRepository() const;
    [[nodiscard]] RepositoryView *repositoryAt(int index) const;
    void updateActions();
    [[nodiscard]] QLabel *addToolbarBadge(QAction *action, const QString &name);
    static void updateToolbarBadge(QLabel *badge, int count);
    static void positionToolbarBadge(QLabel *badge);
    static void setTabBadge(QLabel *badge, int count, const QString &arrow);
    void updateTabTitle(RepositoryView *view);
    void updateTabMetrics();
    void updateTabCloseButtons(int hoveredTab = -1);

    void addBookmark(const QString &path);
    void removeSelectedBookmark();
    void refreshBookmarks();
    void restoreSession();
    void saveSession();

    void runOnCurrent(void (RepositoryView::*slot)());

    // False on macOS, which keeps its native frame and traffic lights while
    // expanding the Qt client area into the title bar.
    bool useCustomChrome_ = true;
    bool useExpandedMacChrome_ = false;
    QWidget *titleBar_ = nullptr;
    QWidget *windowFrame_ = nullptr;
    QGraphicsDropShadowEffect *windowShadow_ = nullptr;
    QMenuBar *menuBar_ = nullptr;
    QToolButton *maximizeButton_ = nullptr;
    QToolButton *addTabButton_ = nullptr;
    QTabBar *tabs_ = nullptr;
    QStackedWidget *tabPages_ = nullptr;
    QToolBar *mainToolbar_ = nullptr;
    QListWidget *bookmarksList_ = nullptr;
    QProgressBar *busyIndicator_ = nullptr;
    QToolButton *cancelOperationButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *commitBadge_ = nullptr;
    QLabel *pushBadge_ = nullptr;
    QLabel *pullBadge_ = nullptr;
    QDockWidget *logDock_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    UpdateChecker *updateChecker_ = nullptr;

    QAction *openAction_ = nullptr;
    QAction *cloneAction_ = nullptr;
    QAction *createAction_ = nullptr;
    QAction *closeTabAction_ = nullptr;
    QAction *commitAction_ = nullptr;
    QAction *checkoutAction_ = nullptr;
    QAction *discardAction_ = nullptr;
    QAction *stashAction_ = nullptr;
    QAction *stashPopAction_ = nullptr;
    QAction *fetchAction_ = nullptr;
    QAction *pullAction_ = nullptr;
    QAction *pushAction_ = nullptr;
    QAction *branchAction_ = nullptr;
    QAction *mergeAction_ = nullptr;
    QAction *tagAction_ = nullptr;
    QAction *terminalAction_ = nullptr;
    QAction *explorerAction_ = nullptr;
    QAction *settingsAction_ = nullptr;
    QAction *refreshAction_ = nullptr;
    QAction *fileStatusPageAction_ = nullptr;
    QAction *historyPageAction_ = nullptr;
    QAction *searchPageAction_ = nullptr;
    QAction *themeAction_ = nullptr;
    QAction *logAction_ = nullptr;

    QStringList bookmarks_;
};
