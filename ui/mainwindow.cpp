// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "mainwindow.h"

#include "dialogs.h"
#include "core/gitclient.h"
#include "core/gitprocess.h"
#include "icons.h"
#include "repositoryview.h"
#include "theme.h"
#include "updatechecker.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace {
constexpr int MaximumBookmarks = 40;
constexpr int MaximumLogLines = 500;
/// Height of the combined menu/title row.
constexpr int TitleBarHeight = Icons::WindowControlButtonHeight;
/// Transparent space reserved around the custom frame for its compositor-like
/// drop shadow. It is also a convenient resize target.
constexpr int WindowShadowMargin = 14;
constexpr int WindowFrameBorderWidth = 1;
/// Width of the invisible band along the window edges used for resizing,
/// which a frameless window has to provide itself.
constexpr int ResizeMargin = WindowShadowMargin;
constexpr int AboutFrameWidth = 550;
constexpr int AboutFrameHeight = 351;
constexpr int AboutShadowMargin = 10;

QFont topBarFont() {
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(12);
    font.setWeight(QFont::Normal);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

class RepositoryTabBar final : public QTabBar {
public:
    using QTabBar::QTabBar;

    void setCalculatedWidths(const QList<int> &widths) {
        widths_ = widths;
        updateGeometry();
    }

    QSize tabSizeHint(const int index) const override {
        QSize size = QTabBar::tabSizeHint(index);
        if (index >= 0 && index < widths_.size()) {
            size.setWidth(widths_.at(index));
        }
        return size;
    }

private:
    QList<int> widths_;
};

class AboutDialogWindow final : public QDialog {
public:
    explicit AboutDialogWindow(QWidget *parent)
        : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint) {
        setObjectName(QStringLiteral("aboutDialog"));
        setAttribute(Qt::WA_TranslucentBackground, true);
        setModal(true);
        setFixedSize(AboutFrameWidth + AboutShadowMargin * 2,
                     AboutFrameHeight + AboutShadowMargin * 2);
    }

    void setMoveHandle(QWidget *handle) {
        moveHandle_ = handle;
        moveHandle_->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == moveHandle_ && event->type() == QEvent::MouseButtonPress) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && windowHandle() != nullptr) {
                windowHandle()->startSystemMove();
                return true;
            }
        }
        return QDialog::eventFilter(watched, event);
    }

private:
    QWidget *moveHandle_ = nullptr;
};
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("SquidyGit"));
    resize(1360, 860);
    setMinimumSize(960, 620);

    // Draw a custom title bar: the menu and the window buttons share
    // one band, with the repository tabs directly underneath.
    setWindowFlag(Qt::FramelessWindowHint, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
    const int framedMargin = WindowShadowMargin + WindowFrameBorderWidth;
    setContentsMargins(framedMargin, framedMargin, framedMargin, framedMargin);
    setMouseTracking(true);

    updateChecker_ = new UpdateChecker(this);

    windowFrame_ = new QWidget(this);
    windowFrame_->setObjectName(QStringLiteral("windowShadowFrame"));
    windowFrame_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    windowShadow_ = new QGraphicsDropShadowEffect(windowFrame_);
    windowShadow_->setBlurRadius(26.0);
    windowShadow_->setOffset(0.0, 3.0);
    windowShadow_->setColor(QColor(0, 0, 0, 145));
    windowFrame_->setGraphicsEffect(windowShadow_);
    windowFrame_->lower();

    buildInterface();
    // A frameless window owns its resize cursor. Watching application-wide
    // pointer transitions ensures it is reset even when the pointer moves
    // from the resize margin onto a child widget.
    qApp->installEventFilter(this);
    restoreSession();
    QTimer::singleShot(0, this, [this] { updateTabMetrics(); });
    updateWindowButtons();
    updateWindowFrame();
    updateActions();

    QTimer::singleShot(1500, this, [this] {
        const QSettings settings;
        if (settings.value(QStringLiteral("updates/automaticCheck"), true).toBool()) {
            updateChecker_->checkForUpdates(false);
        }
    });
}

void MainWindow::buildInterface() {
    buildToolbar();
    buildMenus();
    setMenuWidget(buildTitleBar());

    auto *workspace = new QWidget;
    workspace->setObjectName(QStringLiteral("workspace"));
    auto *workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);

    auto *tabStrip = new QWidget;
    tabStrip->setObjectName(QStringLiteral("repositoryTabStrip"));
    tabStrip->setFixedHeight(26);
    auto *tabStripLayout = new QHBoxLayout(tabStrip);
    tabStripLayout->setContentsMargins(8, 0, 4, 0);
    tabStripLayout->setSpacing(0);

    tabs_ = new RepositoryTabBar;
    tabs_->setObjectName(QStringLiteral("repositoryTabBar"));
    tabs_->setFont(topBarFont());
    tabs_->setFixedHeight(26);
    tabs_->setDocumentMode(true);
    tabs_->setMovable(true);
    tabs_->setTabsClosable(true);
    tabs_->setExpanding(false);
    tabs_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    tabs_->setElideMode(Qt::ElideRight);
    // Repository names may collapse down to the close-button width. Native
    // scroll arrows would otherwise separate the + button from the last tab.
    tabs_->setUsesScrollButtons(false);
    tabs_->setContextMenuPolicy(Qt::CustomContextMenu);
    tabs_->setMouseTracking(true);
    tabs_->installEventFilter(this);
    tabs_->addTab(QString());
    auto *homeTabTitle = new QLabel(tr("Repositories"));
    homeTabTitle->setObjectName(QStringLiteral("repositoryTabTitle"));
    homeTabTitle->setFont(topBarFont());
    homeTabTitle->setProperty("fullTitle", tr("Repositories"));
    homeTabTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    tabs_->setTabButton(0, QTabBar::LeftSide, homeTabTitle);
    tabs_->setTabButton(0, QTabBar::RightSide, nullptr);

    tabPages_ = new QStackedWidget;
    tabPages_->setObjectName(QStringLiteral("repositoryPages"));
    tabPages_->addWidget(buildWelcomePage());

    addTabButton_ = new QToolButton;
    addTabButton_->setObjectName(QStringLiteral("addTabButton"));
    addTabButton_->setFixedHeight(26);
    addTabButton_->setIcon(Icons::icon(Icons::Glyph::Add, Qt::white));
    addTabButton_->setIconSize(QSize(18, 18));
    addTabButton_->setFixedWidth(28);
    addTabButton_->setToolTip(tr("Open a repository in a new tab"));
    connect(addTabButton_, &QToolButton::clicked, this, [this] { openRepositoryDialog(); });
    // Keep the add button attached to the last visible tab instead of pinning
    // it to the far edge of the window.
    tabStripLayout->addWidget(tabs_);
    tabStripLayout->addWidget(addTabButton_);
    tabStripLayout->addStretch(1);

    // The vertical order is title/menu, repository tabs, actions,
    // repository content. Keeping these as explicit widgets avoids the native
    // QMainWindow toolbar area placing the actions above the tabs.
    workspaceLayout->addWidget(tabStrip);
    workspaceLayout->addWidget(mainToolbar_);
    workspaceLayout->addWidget(tabPages_, 1);
    setCentralWidget(workspace);

    connect(tabs_, &QTabBar::tabCloseRequested, this,
            [this](const int index) { closeTab(index); });
    connect(tabs_, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &position) { showTabContextMenu(position); });
    connect(tabs_, &QTabBar::currentChanged, this, [this](const int index) {
        tabPages_->setCurrentIndex(index);
        for (int tab = 1; tab < tabs_->count(); ++tab) {
            if (QWidget *button = tabs_->tabButton(tab, QTabBar::RightSide)) {
                button->setVisible(tab == index);
            }
        }
        updateTabMetrics();
        updateActions();
    });
    connect(tabs_, &QTabBar::tabMoved, this, [this](const int from, const int to) {
        // The repositories overview is an anchored home tab.
        if (from == 0 || to == 0) {
            const QSignalBlocker blocker(tabs_);
            tabs_->moveTab(to, from);
            tabPages_->setCurrentIndex(tabs_->currentIndex());
            return;
        }
        QWidget *page = tabPages_->widget(from);
        if (page == nullptr) {
            return;
        }
        tabPages_->removeWidget(page);
        tabPages_->insertWidget(to, page);
        tabPages_->setCurrentIndex(tabs_->currentIndex());
    });
    updateTabMetrics();

    logDock_ = buildCommandLog();
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);
    logDock_->hide();

    statusLabel_ = new QLabel(tr("Open or clone a repository"));
    statusBar()->addWidget(statusLabel_, 1);
    busyIndicator_ = new QProgressBar;
    busyIndicator_->setRange(0, 0);
    busyIndicator_->setFixedWidth(120);
    busyIndicator_->setTextVisible(false);
    busyIndicator_->hide();
    statusBar()->addPermanentWidget(busyIndicator_);
}

void MainWindow::buildToolbar() {
    mainToolbar_ = new QToolBar(tr("Actions"), this);
    mainToolbar_->setObjectName(QStringLiteral("mainToolbar"));
    mainToolbar_->setMovable(false);
    mainToolbar_->setFloatable(false);
    mainToolbar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    mainToolbar_->setIconSize(QSize(24, 24));
    mainToolbar_->setFixedHeight(60);

    const auto addAction = [this](const Icons::Glyph glyph, const QString &text,
                                  const QString &tip) {
        QAction *action = mainToolbar_->addAction(Icons::icon(glyph), text);
        action->setToolTip(tip);
        return action;
    };

    // Keep the frequent commands in this exact order.
    commitAction_ = addAction(Icons::Glyph::Commit, tr("Commit"),
                              tr("Commit the staged changes"));
    if (auto *commitButton = qobject_cast<QToolButton *>(
            mainToolbar_->widgetForAction(commitAction_))) {
        commitBadge_ = new QLabel(commitButton);
        commitBadge_->setObjectName(QStringLiteral("commitBadge"));
        commitBadge_->setAlignment(Qt::AlignCenter);
        commitBadge_->setGeometry(34, 1, 18, 13);
        commitBadge_->raise();
        commitBadge_->hide();
    }
    mainToolbar_->addSeparator();
    pushAction_ = addAction(Icons::Glyph::Push, tr("Push"),
                            tr("Push the changes"));
    pullAction_ = addAction(Icons::Glyph::Pull, tr("Pull"),
                            tr("Pull and merge the changes"));
    fetchAction_ = addAction(Icons::Glyph::Fetch, tr("Fetch"),
                             tr("Fetch from all remote repositories"));
    mainToolbar_->addSeparator();
    branchAction_ = addAction(Icons::Glyph::Branch, tr("Branch"),
                              tr("Create a branch"));
    mergeAction_ = addAction(Icons::Glyph::Merge, tr("Merge"),
                             tr("Merge a branch into the current one"));
    mainToolbar_->addSeparator();
    stashAction_ = addAction(Icons::Glyph::Stash, tr("Stash"),
                             tr("Stash the current changes"));
    mainToolbar_->addSeparator();
    discardAction_ = addAction(Icons::Glyph::Discard, tr("Discard"),
                               tr("Discard the selected files"));
    tagAction_ = addAction(Icons::Glyph::Tag, tr("Tag"),
                           tr("Create a tag"));

    // Less frequent operations stay available in Repository/Actions menus.
    checkoutAction_ = new QAction(Icons::icon(Icons::Glyph::Checkout),
                                  tr("Checkout"), this);
    checkoutAction_->setToolTip(tr("Switch to a branch or tag"));
    stashPopAction_ = new QAction(Icons::icon(Icons::Glyph::StashPop),
                                  tr("Apply the latest stash"), this);
    stashPopAction_->setToolTip(tr("Pop the latest stash"));

    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    spacer->setAttribute(Qt::WA_TranslucentBackground);
    mainToolbar_->addWidget(spacer);

    terminalAction_ = addAction(Icons::Glyph::Terminal, tr("Terminal"),
                                tr("Open a terminal in the repository folder"));
    explorerAction_ = addAction(Icons::Glyph::Explorer, tr("Folder"),
                                tr("Open the repository folder"));
    settingsAction_ = addAction(Icons::Glyph::Settings, tr("Settings"),
                                tr("Repository and application settings"));

    commitAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    fetchAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    pullAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    pushAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    branchAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+B")));

    connect(commitAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::focusCommitMessage); });
    connect(checkoutAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::checkoutInteractive); });
    connect(discardAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::discardSelectedFiles); });
    connect(stashAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::createStash); });
    connect(stashPopAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::popLatestStash); });
    connect(fetchAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::startFetch); });
    connect(pullAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::startPull); });
    connect(pushAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::startPush); });
    connect(branchAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::createBranchInteractive); });
    connect(mergeAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::mergeInteractive); });
    connect(tagAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::createTagInteractive); });
    connect(terminalAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::openTerminal); });
    connect(explorerAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::openFileManager); });
    connect(settingsAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::showPreferences); });
}

QWidget *MainWindow::buildTitleBar() {
    titleBar_ = new QWidget;
    titleBar_->setObjectName(QStringLiteral("titleBar"));
    titleBar_->setFixedHeight(TitleBarHeight);

    auto *layout = new QHBoxLayout(titleBar_);
    layout->setContentsMargins(8, 0, 0, 0);
    layout->setSpacing(0);

    auto *logo = new QLabel;
    logo->setPixmap(Icons::pixmap(Icons::Glyph::Ghost, 20, Qt::white));
    logo->setFixedWidth(34);
    layout->addWidget(logo);

    // Keep the menu at its natural height and centre it in the taller title
    // row. Letting the layout stretch it pins its actions to the top edge.
    layout->addWidget(menuBar_, 0, Qt::AlignVCenter);
    layout->addStretch(1);

    const auto addWindowButton = [this, layout](const Icons::Glyph glyph, const QString &tip,
                                                const bool danger) {
        auto *button = new QToolButton;
        button->setObjectName(danger ? QStringLiteral("windowCloseButton")
                                     : QStringLiteral("windowButton"));
        button->setIcon(Icons::icon(glyph, Qt::white));
        button->setIconSize(QSize(Icons::WindowControlIconSize,
                                  Icons::WindowControlIconSize));
        button->setFixedSize(QSize(Icons::WindowControlButtonWidth,
                                   Icons::WindowControlButtonHeight));
        button->setToolTip(tip);
        layout->addWidget(button);
        return button;
    };

    QToolButton *minimizeButton = addWindowButton(Icons::Glyph::WindowMinimize,
                                                  tr("Minimize"), false);
    maximizeButton_ = addWindowButton(Icons::Glyph::WindowMaximize,
                                      tr("Maximize"), false);
    QToolButton *closeButton = addWindowButton(Icons::Glyph::WindowClose,
                                               tr("Close"), true);

    connect(minimizeButton, &QToolButton::clicked, this, [this] { showMinimized(); });
    connect(maximizeButton_, &QToolButton::clicked, this, [this] { toggleMaximized(); });
    connect(closeButton, &QToolButton::clicked, this, [this] { close(); });

    return titleBar_;
}

void MainWindow::toggleMaximized() {
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
}

void MainWindow::updateWindowButtons() {
    const bool maximized = isMaximized();
    if (maximizeButton_ != nullptr) {
        maximizeButton_->setIcon(Icons::icon(maximized ? Icons::Glyph::WindowRestore
                                                       : Icons::Glyph::WindowMaximize,
                                             Qt::white));
        maximizeButton_->setToolTip(maximized ? tr("Restore")
                                              : tr("Maximize"));
    }
    // A maximized window has no edges to drag, so the resize band is dropped.
    const int margin = maximized || isFullScreen()
                           ? 0
                           : WindowShadowMargin + WindowFrameBorderWidth;
    setContentsMargins(margin, margin, margin, margin);
    updateWindowFrame();
}

void MainWindow::updateWindowFrame() {
    if (windowFrame_ == nullptr) {
        return;
    }
    const bool flush = isMaximized() || isFullScreen();
    const int margin = flush ? 0 : WindowShadowMargin;
    windowFrame_->setGeometry(rect().adjusted(margin, margin, -margin, -margin));
    windowFrame_->setVisible(true);
    windowFrame_->lower();
    if (windowShadow_ != nullptr) {
        windowShadow_->setEnabled(!flush);
    }
}

Qt::Edges MainWindow::resizeEdgesAt(const QPoint &position) const {
    if (isMaximized() || isFullScreen()) {
        return {};
    }

    Qt::Edges edges;
    if (position.x() <= ResizeMargin) edges |= Qt::LeftEdge;
    if (position.x() >= width() - ResizeMargin) edges |= Qt::RightEdge;
    if (position.y() <= ResizeMargin) edges |= Qt::TopEdge;
    if (position.y() >= height() - ResizeMargin) edges |= Qt::BottomEdge;
    return edges;
}

void MainWindow::mouseMoveEvent(QMouseEvent *event) {
    updateResizeCursor(event->position().toPoint());
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::updateResizeCursor(const QPoint &position) {
    const Qt::Edges edges = resizeEdgesAt(position);
    if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge)) {
        setCursor(Qt::SizeFDiagCursor);
    } else if (edges == (Qt::RightEdge | Qt::TopEdge)
               || edges == (Qt::LeftEdge | Qt::BottomEdge)) {
        setCursor(Qt::SizeBDiagCursor);
    } else if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge)) {
        setCursor(Qt::SizeHorCursor);
    } else if (edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge)) {
        setCursor(Qt::SizeVerCursor);
    } else {
        unsetCursor();
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
    const Qt::Edges edges = resizeEdgesAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && edges != Qt::Edges() && windowHandle() != nullptr) {
        windowHandle()->startSystemResize(edges);
        event->accept();
        return;
    }
    QMainWindow::mousePressEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateWindowFrame();
    updateTabMetrics();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseMove) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        updateResizeCursor(mapFromGlobal(mouseEvent->globalPosition().toPoint()));
    } else if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
        updateResizeCursor(mapFromGlobal(QCursor::pos()));
    } else if (event->type() == QEvent::MouseButtonRelease
               || event->type() == QEvent::NonClientAreaMouseButtonRelease) {
        // The native resize loop may finish without another move event.
        unsetCursor();
    }

    if (watched == titleBar_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && windowHandle() != nullptr) {
                windowHandle()->startSystemMove();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            toggleMaximized();
            return true;
        }
    }

    if (watched == tabs_) {
        if (event->type() == QEvent::MouseMove) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            updateTabCloseButtons(tabs_->tabAt(mouseEvent->position().toPoint()));
        } else if (event->type() == QEvent::Leave) {
            updateTabCloseButtons();
        }
    } else if (auto *closeButton = qobject_cast<QToolButton *>(watched);
               closeButton != nullptr
               && closeButton->objectName() == QStringLiteral("tabCloseButton")) {
        if (event->type() == QEvent::Enter) {
            closeButton->setIcon(Icons::icon(Icons::Glyph::TabClose, Qt::white));
        } else if (event->type() == QEvent::Leave) {
            closeButton->setIcon(Icons::icon(Icons::Glyph::TabClose,
                                             Theme::instance()->palette().text));
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    updateTabMetrics();
    // On Windows the final client geometry is committed after the show event,
    // especially when restoreGeometry also restores a maximized window.
    QTimer::singleShot(0, this, [this] { updateTabMetrics(); });
}

void MainWindow::updateTabCloseButtons(const int hoveredTab) {
    if (tabs_ == nullptr) {
        return;
    }
    for (int index = 1; index < tabs_->count(); ++index) {
        if (QWidget *area = tabs_->tabButton(index, QTabBar::RightSide)) {
            auto *button = area->findChild<QToolButton *>(QStringLiteral("tabCloseButton"));
            if (button != nullptr) {
                button->setVisible(index == hoveredTab);
            }
            area->setFixedWidth(24);
            area->setVisible(index == hoveredTab);
        }
    }
}

void MainWindow::buildMenus() {
    menuBar_ = new QMenuBar;
    menuBar_->setNativeMenuBar(false);
    menuBar_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    menuBar_->setFont(topBarFont());

    auto *fileMenu = menuBar_->addMenu(tr("&File"));
    openAction_ = fileMenu->addAction(Icons::icon(Icons::Glyph::OpenFolder),
                                      tr("Open Repository…"));
    openAction_->setShortcut(QKeySequence::Open);
    cloneAction_ = fileMenu->addAction(Icons::icon(Icons::Glyph::Clone),
                                       tr("Clone…"));
    cloneAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    createAction_ = fileMenu->addAction(Icons::icon(Icons::Glyph::Create),
                                        tr("Create Repository…"));
    fileMenu->addSeparator();
    closeTabAction_ = fileMenu->addAction(tr("Close Tab"));
    closeTabAction_->setShortcut(QKeySequence::Close);
    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(tr("Quit"));
    quitAction->setShortcut(QKeySequence::Quit);

    auto *editMenu = menuBar_->addMenu(tr("&Edit"));
    editMenu->addAction(checkoutAction_);
    editMenu->addSeparator();
    editMenu->addAction(settingsAction_);

    auto *viewMenu = menuBar_->addMenu(tr("&View"));
    fileStatusPageAction_ = viewMenu->addAction(Icons::icon(Icons::Glyph::FileStatus),
                                                tr("File Status"));
    fileStatusPageAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    historyPageAction_ = viewMenu->addAction(Icons::icon(Icons::Glyph::History),
                                             tr("History"));
    historyPageAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    searchPageAction_ = viewMenu->addAction(Icons::icon(Icons::Glyph::Search),
                                            tr("Search"));
    searchPageAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
    viewMenu->addSeparator();
    themeAction_ = viewMenu->addAction(tr("Dark Theme"));
    themeAction_->setCheckable(true);
    themeAction_->setChecked(Theme::instance()->mode() == Theme::Mode::Dark);
    viewMenu->addSeparator();
    buildLanguageMenu(viewMenu->addMenu(tr("Language")));

    auto *repositoryMenu = menuBar_->addMenu(tr("&Repository"));
    refreshAction_ = repositoryMenu->addAction(Icons::icon(Icons::Glyph::Refresh),
                                               tr("Refresh"));
    refreshAction_->setShortcut(QKeySequence::Refresh);
    repositoryMenu->addSeparator();
    repositoryMenu->addAction(checkoutAction_);
    repositoryMenu->addAction(branchAction_);
    repositoryMenu->addAction(mergeAction_);
    repositoryMenu->addAction(tagAction_);
    repositoryMenu->addSeparator();
    QAction *addRemoteAction = repositoryMenu->addAction(Icons::icon(Icons::Glyph::Remote),
                                                         tr("Add Remote…"));

    auto *actionsMenu = menuBar_->addMenu(tr("&Actions"));
    actionsMenu->addAction(commitAction_);
    actionsMenu->addAction(discardAction_);
    actionsMenu->addSeparator();
    actionsMenu->addAction(stashAction_);
    actionsMenu->addAction(stashPopAction_);
    actionsMenu->addSeparator();
    actionsMenu->addAction(fetchAction_);
    actionsMenu->addAction(pullAction_);
    actionsMenu->addAction(pushAction_);

    auto *toolsMenu = menuBar_->addMenu(tr("&Tools"));
    toolsMenu->addAction(terminalAction_);
    toolsMenu->addAction(explorerAction_);
    toolsMenu->addSeparator();
    logAction_ = toolsMenu->addAction(tr("Git Command Log"));
    logAction_->setCheckable(true);
    logAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+`")));

    auto *helpMenu = menuBar_->addMenu(tr("&Help"));
    QAction *checkUpdatesAction = helpMenu->addAction(
        tr("Check for Updates…"));
    QAction *automaticUpdatesAction = helpMenu->addAction(
        tr("Check for Updates Automatically"));
    automaticUpdatesAction->setCheckable(true);
    automaticUpdatesAction->setChecked(
        QSettings().value(QStringLiteral("updates/automaticCheck"), true).toBool());
    helpMenu->addSeparator();
    QAction *aboutAction = helpMenu->addAction(tr("About"));

    connect(openAction_, &QAction::triggered, this, [this] { openRepositoryDialog(); });
    connect(cloneAction_, &QAction::triggered, this, [this] { cloneRepository(); });
    connect(createAction_, &QAction::triggered, this, [this] { createRepository(); });
    connect(closeTabAction_, &QAction::triggered, this, [this] { closeCurrentTab(); });
    connect(quitAction, &QAction::triggered, this, [this] { close(); });
    connect(refreshAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::refreshAll); });
    connect(addRemoteAction, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::addRemoteInteractive); });
    connect(fileStatusPageAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::showFileStatusPage); });
    connect(historyPageAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::showHistoryPage); });
    connect(searchPageAction_, &QAction::triggered, this,
            [this] { runOnCurrent(&RepositoryView::showSearchPage); });
    connect(themeAction_, &QAction::toggled, this, [this](const bool checked) {
        Theme::instance()->setMode(checked ? Theme::Mode::Dark : Theme::Mode::Light);
        updateActions();
    });
    connect(logAction_, &QAction::toggled, this,
            [this](const bool checked) { logDock_->setVisible(checked); });
    connect(checkUpdatesAction, &QAction::triggered, this,
            [this] { updateChecker_->checkForUpdates(true); });
    connect(automaticUpdatesAction, &QAction::toggled, this, [](const bool checked) {
        QSettings().setValue(QStringLiteral("updates/automaticCheck"), checked);
    });
    connect(aboutAction, &QAction::triggered, this,
            [this] { showAboutDialog(); });
}

void MainWindow::showAboutDialog() {
    AboutDialogWindow dialog(this);
    dialog.setWindowTitle(tr("About SquidyGit"));
    dialog.setWindowIcon(Icons::applicationIcon());

    auto *outerLayout = new QVBoxLayout(&dialog);
    outerLayout->setContentsMargins(AboutShadowMargin, AboutShadowMargin,
                                    AboutShadowMargin, AboutShadowMargin);
    outerLayout->setSpacing(0);

    auto *windowFrame = new QFrame;
    windowFrame->setObjectName(QStringLiteral("aboutWindow"));
    auto *shadow = new QGraphicsDropShadowEffect(windowFrame);
    shadow->setBlurRadius(22.0);
    shadow->setOffset(0.0, 2.0);
    shadow->setColor(QColor(0, 0, 0, 145));
    windowFrame->setGraphicsEffect(shadow);
    outerLayout->addWidget(windowFrame);

    auto *frameLayout = new QVBoxLayout(windowFrame);
    frameLayout->setContentsMargins(0, 0, 0, 0);
    frameLayout->setSpacing(0);

    auto *titleBar = new QWidget;
    titleBar->setObjectName(QStringLiteral("aboutTitleBar"));
    titleBar->setFixedHeight(TitleBarHeight);
    dialog.setMoveHandle(titleBar);

    auto *titleBarLayout = new QHBoxLayout(titleBar);
    titleBarLayout->setContentsMargins(8, 0, 0, 0);
    titleBarLayout->setSpacing(0);

    auto *smallLogo = new QLabel;
    smallLogo->setPixmap(Icons::pixmap(Icons::Glyph::Ghost, 16, Qt::white));
    smallLogo->setFixedWidth(28);
    smallLogo->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    titleBarLayout->addWidget(smallLogo);

    auto *windowTitle = new QLabel(tr("About"));
    windowTitle->setObjectName(QStringLiteral("aboutTitleBarText"));
    windowTitle->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    titleBarLayout->addWidget(windowTitle);
    titleBarLayout->addStretch(1);

    auto *closeButton = new QToolButton;
    closeButton->setObjectName(QStringLiteral("aboutCloseButton"));
    closeButton->setIcon(Icons::icon(Icons::Glyph::WindowClose, Qt::white));
    closeButton->setIconSize(QSize(Icons::WindowControlIconSize,
                                   Icons::WindowControlIconSize));
    closeButton->setFixedSize(Icons::WindowControlButtonWidth,
                              Icons::WindowControlButtonHeight);
    closeButton->setToolTip(tr("Close"));
    titleBarLayout->addWidget(closeButton);
    frameLayout->addWidget(titleBar);

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("aboutContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(16, 10, 10, 9);
    contentLayout->setSpacing(0);

    auto *hero = new QWidget;
    hero->setObjectName(QStringLiteral("aboutHero"));
    hero->setFixedHeight(105);
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(0, 0, 0, 0);
    heroLayout->setSpacing(6);

    auto *logo = new QLabel;
    logo->setPixmap(Icons::applicationPixmap(64));
    logo->setFixedSize(64, 64);
    heroLayout->addWidget(logo, 0, Qt::AlignTop);

    auto *applicationInfo = new QWidget;
    applicationInfo->setObjectName(QStringLiteral("aboutApplicationInfo"));
    auto *infoLayout = new QVBoxLayout(applicationInfo);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(0);
    infoLayout->addSpacing(24);

    auto *applicationTitle = new QLabel(tr("About SquidyGit"));
    applicationTitle->setObjectName(QStringLiteral("aboutTitle"));
    applicationTitle->setAlignment(Qt::AlignHCenter);
    infoLayout->addWidget(applicationTitle);
    infoLayout->addSpacing(20);

    auto *copyright = new QLabel(
        tr("Copyright © 2026 Sergey Yakunin. All rights reserved."));
    copyright->setObjectName(QStringLiteral("aboutMeta"));
    copyright->setAlignment(Qt::AlignHCenter);
    infoLayout->addWidget(copyright);

    auto *version = new QLabel(
        tr("Version %1").arg(QApplication::applicationVersion()));
    version->setObjectName(QStringLiteral("aboutMeta"));
    version->setAlignment(Qt::AlignHCenter);
    infoLayout->addWidget(version);
    infoLayout->addStretch(1);
    heroLayout->addWidget(applicationInfo, 1);
    contentLayout->addWidget(hero);

    auto *components = new QTextBrowser;
    components->setObjectName(QStringLiteral("aboutComponents"));
    components->setOpenExternalLinks(true);
    components->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const ThemePalette &palette = Theme::instance()->palette();
    components->setHtml(QStringLiteral(R"(
        <html><head><style>
        body { margin: 3px 0 0 0; color: %1; font-family: Arial; font-size: 12px; }
        a { color: %2; text-decoration: underline; }
        .heading { font-weight: 600; margin-bottom: 2px; }
        .component { margin: 0; }
        .license-title { font-weight: 600; margin-top: 12px; }
        .license { margin: 2px 0 0 0; }
        </style></head><body>
        <div class="heading">Open Source Components / Libraries / Code</div>
        <div class="component"><a href="https://github.com/squidyru/squidy-git">SquidyGit</a>
        © 2026 Sergey Yakunin (<a href="https://github.com/squidyru/squidy-git/blob/main/LICENSE">MIT</a>)</div>
        <div class="component"><a href="https://www.qt.io/">Qt %3</a>
        © The Qt Company and Qt contributors (<a href="https://www.gnu.org/licenses/lgpl-3.0.html">LGPL v3</a>)</div>
        <div class="component"><a href="https://git-scm.com/">Git</a>
        by the Git project contributors (<a href="https://www.gnu.org/licenses/old-licenses/gpl-2.0.html">GPL v2</a>)</div>
        <div class="license-title">SquidyGit — MIT License</div>
        <div class="license">Permission is hereby granted, free of charge, to any person obtaining a copy
        of this software and associated documentation files (the “Software”), to deal in the Software
        without restriction, including without limitation the rights to use, copy, modify, merge,
        publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to
        whom the Software is furnished to do so, subject to the following conditions:<br><br>
        The above copyright notice and this permission notice shall be included in all copies or
        substantial portions of the Software.<br><br>
        THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
        BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
        NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
        DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
        OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.</div>
        </body></html>)")
                            .arg(palette.text.name(QColor::HexRgb),
                                 palette.accent.name(QColor::HexRgb),
                                 QString::fromLatin1(qVersion())));
    contentLayout->addWidget(components, 1);
    frameLayout->addWidget(content, 1);

    connect(closeButton, &QToolButton::clicked, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::buildLanguageMenu(QMenu *menu) {
    // The names of the languages are left untranslated on purpose: everybody
    // should find their own language in the list, whatever the interface shows.
    const QList<QPair<QString, QString>> languages = {
        {QString(), tr("System")},
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("ru"), QStringLiteral("Русский")},
        {QStringLiteral("zh_CN"), QStringLiteral("中文")},
    };

    const QString selected =
        QSettings().value(QStringLiteral("interface/language")).toString();
    auto *group = new QActionGroup(menu);
    for (const auto &[code, title] : languages) {
        QAction *action = menu->addAction(title);
        action->setCheckable(true);
        action->setChecked(code == selected);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, code] { selectLanguage(code); });
    }
}

void MainWindow::selectLanguage(const QString &language) {
    QSettings settings;
    if (settings.value(QStringLiteral("interface/language")).toString() == language) {
        return;
    }
    settings.setValue(QStringLiteral("interface/language"), language);

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Language"),
        tr("The interface language changes after a restart. Restart SquidyGit now?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        return;
    }

    // The session is stored before the new process starts, so that it opens the
    // repositories that are on screen right now.
    saveSession();
    settings.sync();
    QStringList arguments = QCoreApplication::arguments();
    if (!arguments.isEmpty()) {
        arguments.removeFirst();
    }
    if (QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments,
                                QDir::currentPath())) {
        QApplication::quit();
    } else {
        QMessageBox::warning(this, tr("Language"),
                             tr("SquidyGit could not be restarted. Start it manually to "
                                "apply the new language."));
    }
}

QWidget *MainWindow::buildWelcomePage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);

    auto *title = new QLabel(tr("Local repositories"));
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);

    auto *subtitle = new QLabel(tr("Bookmarks are kept between sessions. A double click "
                                   "opens the repository in a tab."));
    subtitle->setObjectName(QStringLiteral("mutedText"));
    layout->addWidget(subtitle);

    auto *buttonRow = new QHBoxLayout;
    auto *cloneButton = new QPushButton(
        Icons::icon(Icons::Glyph::Clone, Theme::instance()->palette().accentText),
        tr("Clone"));
    cloneButton->setProperty("accent", true);
    auto *openButton = new QPushButton(Icons::icon(Icons::Glyph::OpenFolder),
                                       tr("Add existing"));
    auto *createButton = new QPushButton(Icons::icon(Icons::Glyph::Create),
                                         tr("Create new"));
    auto *removeButton = new QPushButton(Icons::icon(Icons::Glyph::Remove),
                                         tr("Remove from the list"));
    buttonRow->addWidget(cloneButton);
    buttonRow->addWidget(openButton);
    buttonRow->addWidget(createButton);
    buttonRow->addStretch();
    buttonRow->addWidget(removeButton);
    layout->addLayout(buttonRow);

    bookmarksList_ = new QListWidget;
    bookmarksList_->setAlternatingRowColors(true);
    bookmarksList_->setIconSize(QSize(20, 20));
    layout->addWidget(bookmarksList_, 1);

    connect(cloneButton, &QPushButton::clicked, this, [this] { cloneRepository(); });
    connect(openButton, &QPushButton::clicked, this, [this] { openRepositoryDialog(); });
    connect(createButton, &QPushButton::clicked, this, [this] { createRepository(); });
    connect(removeButton, &QPushButton::clicked, this, [this] { removeSelectedBookmark(); });
    connect(bookmarksList_, &QListWidget::itemDoubleClicked, this,
            [this](const QListWidgetItem *item) {
                if (item != nullptr) {
                    openRepository(item->data(Qt::UserRole).toString());
                }
            });

    return page;
}

QDockWidget *MainWindow::buildCommandLog() {
    auto *dock = new QDockWidget(tr("Git Command Log"), this);
    dock->setObjectName(QStringLiteral("commandLogDock"));
    dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(MaximumLogLines);
    logView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    logView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    dock->setWidget(logView_);

    connect(GitLog::instance(), &GitLog::commandRecorded, this,
            [this](const QString &workingDirectory, const QString &command,
                   const QString &output, const bool succeeded) {
                const QString directory = QFileInfo(workingDirectory).fileName();
                logView_->appendPlainText(QStringLiteral("%1 [%2] %3")
                                              .arg(succeeded ? QStringLiteral("✓")
                                                             : QStringLiteral("✗"),
                                                   directory, command));
                const QString trimmed = output.trimmed();
                if (!succeeded && !trimmed.isEmpty()) {
                    logView_->appendPlainText(QStringLiteral("    %1")
                                                  .arg(trimmed.section(u'\n', 0, 3)));
                }
            });
    connect(dock, &QDockWidget::visibilityChanged, this, [this](const bool visible) {
        if (logAction_ != nullptr && logAction_->isChecked() != visible) {
            const QSignalBlocker blocker(logAction_);
            logAction_->setChecked(visible);
        }
    });

    return dock;
}

void MainWindow::openRepositoryDialog() {
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("Open a Git repository"), QDir::homePath());
    if (directory.isEmpty()) {
        return;
    }
    if (!GitClient::isRepository(directory)) {
        QMessageBox::warning(this, tr("Not a Git repository"),
                             tr("The folder %1 does not contain a Git repository.")
                                 .arg(QDir::toNativeSeparators(directory)));
        return;
    }
    openRepository(directory);
}

bool MainWindow::openRepository(const QString &path, const bool activate) {
    if (path.isEmpty()) {
        return false;
    }

    for (int index = 1; index < tabs_->count(); ++index) {
        RepositoryView *view = repositoryAt(index);
        if (view != nullptr && view->repositoryRoot() == QDir::cleanPath(path)) {
            if (activate) {
                tabs_->setCurrentIndex(index);
            }
            return true;
        }
    }

    auto *view = new RepositoryView(path);
    if (!view->isValid()) {
        delete view;
        statusLabel_->setText(tr("The repository could not be opened: %1")
                                  .arg(QDir::toNativeSeparators(path)));
        return false;
    }

    const int index = tabs_->addTab(QString());
    tabPages_->insertWidget(index, view);
    tabs_->setTabToolTip(index, QDir::toNativeSeparators(view->repositoryRoot()));

    auto *tabTitle = new QLabel(view->repositoryName());
    tabTitle->setObjectName(QStringLiteral("repositoryTabTitle"));
    tabTitle->setFont(topBarFont());
    tabTitle->setProperty("fullTitle", view->repositoryName());
    tabTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    tabs_->setTabButton(index, QTabBar::LeftSide, tabTitle);

    // QTabBar anchors its right-side button directly to the tab edge. A small
    // The wrapper reserves visual breathing room without enlarging the
    // square hover target itself.
    auto *closeArea = new QWidget;
    closeArea->setObjectName(QStringLiteral("tabCloseArea"));
    closeArea->setFixedSize(QSize(24, 20));
    auto *closeLayout = new QHBoxLayout(closeArea);
    closeLayout->setContentsMargins(0, 0, 5, 0);
    closeLayout->setSpacing(0);
    closeLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *closeButton = new QToolButton;
    closeButton->setObjectName(QStringLiteral("tabCloseButton"));
    closeButton->setIcon(Icons::icon(Icons::Glyph::TabClose,
                                     Theme::instance()->palette().text));
    closeButton->setIconSize(QSize(14, 14));
    closeButton->setFixedSize(QSize(18, 18));
    closeButton->setToolTip(tr("Close Tab"));
    closeButton->installEventFilter(this);
    closeLayout->addWidget(closeButton);
    connect(closeButton, &QToolButton::clicked, this,
            [this, view] { closeTab(tabPages_->indexOf(view)); });
    tabs_->setTabButton(index, QTabBar::RightSide, closeArea);
    closeArea->hide();
    updateTabMetrics();
    if (activate) {
        tabs_->setCurrentIndex(index);
    }

    connect(view, &RepositoryView::repositoryChanged, this, [this, view] {
        updateTabTitle(view);
        if (view == currentRepository()) {
            updateActions();
        }
    });
    connect(view, &RepositoryView::messagePosted, this,
            [this, view](const QString &message) {
                if (view == currentRepository()) {
                    statusLabel_->setText(message);
                }
            });
    connect(view, &RepositoryView::busyChanged, this, [this, view](const bool busy) {
        if (view == currentRepository()) {
            busyIndicator_->setVisible(busy);
            updateActions();
        }
    });

    addBookmark(view->repositoryRoot());
    updateTabTitle(view);
    updateActions();
    return true;
}

void MainWindow::cloneRepository() {
    CloneDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted && !dialog.clonedPath().isEmpty()) {
        openRepository(dialog.clonedPath());
    }
}

void MainWindow::createRepository() {
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("Folder for the new repository"), QDir::homePath());
    if (directory.isEmpty()) {
        return;
    }

    if (GitClient::isRepository(directory)) {
        openRepository(directory);
        return;
    }

    const GitCommandResult result = GitClient::initRepository(directory, false);
    if (!result.succeeded()) {
        QMessageBox::warning(this, tr("The repository could not be created"),
                             result.errorText());
        return;
    }
    openRepository(directory);
}

void MainWindow::closeTab(const int index) {
    if (index <= 0 || index >= tabs_->count()) {
        return;
    }
    QWidget *widget = tabPages_->widget(index);
    tabs_->removeTab(index);
    tabPages_->removeWidget(widget);
    widget->deleteLater();
    updateTabMetrics();
    updateActions();
}

void MainWindow::closeCurrentTab() {
    closeTab(tabs_->currentIndex());
}

void MainWindow::showTabContextMenu(const QPoint &position) {
    const int target = tabs_->tabAt(position);
    if (target < 0) {
        return;
    }

    QMenu menu(this);
    menu.setObjectName(QStringLiteral("tabContextMenu"));
    QAction *closeCurrent = menu.addAction(tr("Close tab"));
    QAction *closeAll = menu.addAction(tr("Close all tabs"));
    QAction *closeOthers = menu.addAction(tr("Close all tabs except this one"));

    closeCurrent->setEnabled(target > 0);
    closeAll->setEnabled(tabs_->count() > 1);
    closeOthers->setEnabled(target > 0 && tabs_->count() > 2);

    QAction *selected = menu.exec(tabs_->mapToGlobal(position));
    if (selected == closeCurrent) {
        closeTab(target);
    } else if (selected == closeAll) {
        for (int index = tabs_->count() - 1; index >= 1; --index) {
            closeTab(index);
        }
    } else if (selected == closeOthers) {
        for (int index = tabs_->count() - 1; index >= 1; --index) {
            if (index != target) {
                closeTab(index);
            }
        }
    }
}

RepositoryView *MainWindow::currentRepository() const {
    return repositoryAt(tabs_->currentIndex());
}

RepositoryView *MainWindow::repositoryAt(const int index) const {
    if (index <= 0 || index >= tabs_->count()) {
        return nullptr;
    }
    return qobject_cast<RepositoryView *>(tabPages_->widget(index));
}

void MainWindow::runOnCurrent(void (RepositoryView::*slot)()) {
    RepositoryView *view = currentRepository();
    if (view != nullptr) {
        (view->*slot)();
    }
}

void MainWindow::updateTabTitle(RepositoryView *view) {
    const int index = tabPages_->indexOf(view);
    if (index < 0) {
        return;
    }

    QString indicators;
    if (view->changeCount() > 0) {
        indicators += QStringLiteral("↑");
    }
    if (view->behindCount() > 0) {
        indicators += QStringLiteral("↓");
    }
    const QString title = indicators.isEmpty()
                              ? view->repositoryName()
                              : QStringLiteral("%1 %2").arg(indicators,
                                                             view->repositoryName());
    if (auto *label = qobject_cast<QLabel *>(
            tabs_->tabButton(index, QTabBar::LeftSide))) {
        label->setProperty("fullTitle", title);
    }
    tabs_->setTabToolTip(index, tr("%1\nBranch: %2")
                                    .arg(QDir::toNativeSeparators(view->repositoryRoot()),
                                         view->currentBranchName()));
    updateTabMetrics();
}

void MainWindow::updateTabMetrics() {
    if (tabs_ == nullptr || tabs_->count() == 0) {
        return;
    }

    // Fit every tab before the + button. At the narrowest size a repository
    // title disappears completely, leaving only its close-button area.
    const QWidget *strip = tabs_->parentWidget();
    const QMargins margins = strip != nullptr && strip->layout() != nullptr
                                 ? strip->layout()->contentsMargins()
                                 : QMargins();
    const int stripWidth = strip != nullptr ? strip->width() : width();
    const int addButtonWidth = addTabButton_ != nullptr
                                   ? addTabButton_->sizeHint().width()
                                   : 48;
    const int available = qMax(1, stripWidth - margins.left() - margins.right()
                                      - addButtonWidth);
    QList<int> tabWidths;
    tabWidths.reserve(tabs_->count());
    int desiredTotal = 0;
    for (int index = 0; index < tabs_->count(); ++index) {
        const auto *label = qobject_cast<QLabel *>(tabs_->tabButton(index, QTabBar::LeftSide));
        const QString title = label != nullptr ? label->property("fullTitle").toString()
                                               : QString();
        // Leave a small reserve beyond the measured text. QTabBar applies its
        // own subcontrol margins and rounds widths at fractional DPI scales.
        const int controls = index == 0 ? 52 : 62;
        const int desired = qBound(index == 0 ? 128 : 112,
                                   (label != nullptr
                                        ? label->fontMetrics().horizontalAdvance(title)
                                        : 70) + controls,
                                   260);
        tabWidths.append(desired);
        desiredTotal += desired;
    }
    const int homeWidth = tabWidths.constFirst();
    const int repositoryCount = tabs_->count() - 1;
    int repositoryWidthLimit = 260;
    if (repositoryCount > 0 && desiredTotal > available) {
        // The overview is pinned and remains readable. Repository tabs share
        // everything that remains and may collapse to their close-button slot.
        repositoryWidthLimit = qBound(40,
                                      (available - homeWidth) / repositoryCount,
                                      260);
    }
    QList<int> calculatedWidths;
    calculatedWidths.reserve(tabWidths.size());
    calculatedWidths.append(homeWidth);
    int effectiveTotal = homeWidth;
    for (int index = 1; index < tabWidths.size(); ++index) {
        const int calculated = qMin(tabWidths.at(index), repositoryWidthLimit);
        calculatedWidths.append(calculated);
        effectiveTotal += calculated;
    }
    static_cast<RepositoryTabBar *>(tabs_)->setCalculatedWidths(calculatedWidths);
    tabs_->setFixedWidth(qMin(available, effectiveTotal));

    for (int index = 0; index < tabs_->count(); ++index) {
        auto *label = qobject_cast<QLabel *>(tabs_->tabButton(index, QTabBar::LeftSide));
        if (label == nullptr) {
            continue;
        }

        const QString fullTitle = label->property("fullTitle").toString();
        const int tabWidth = index == 0
                                 ? homeWidth
                                 : qMin(tabWidths.at(index), repositoryWidthLimit);
        const int labelWidth = qMax(0, tabWidth - (index == 0 ? 22 : 38));
        label->setFixedWidth(labelWidth);
        const int textWidth = qMax(0, labelWidth - 2);
        const QString visibleTitle = textWidth == 0
                                         ? QString()
                                         : label->fontMetrics().elidedText(
                                               fullTitle, Qt::ElideRight, textWidth);
        label->setText(visibleTitle);

    }
    updateTabCloseButtons();
    tabs_->updateGeometry();
}

void MainWindow::updateActions() {
    RepositoryView *view = currentRepository();
    const bool hasRepository = view != nullptr;
    const bool busy = hasRepository && view->isBusy();
    const bool enabled = hasRepository && !busy;

    for (QAction *action : {commitAction_, checkoutAction_, discardAction_, stashAction_,
                            stashPopAction_, fetchAction_, pullAction_, pushAction_,
                            branchAction_, mergeAction_, tagAction_, terminalAction_,
                            explorerAction_, settingsAction_, refreshAction_,
                            fileStatusPageAction_, historyPageAction_, searchPageAction_}) {
        action->setEnabled(enabled);
    }
    closeTabAction_->setEnabled(hasRepository);
    busyIndicator_->setVisible(busy);

    // A clean working tree cannot be stashed or discarded. The interface makes
    // this state immediately visible by dimming both toolbar commands.
    const bool hasChanges = hasRepository && view->changeCount() > 0;
    stashAction_->setEnabled(enabled && hasChanges);
    discardAction_->setEnabled(enabled && hasChanges);

    const ThemePalette &palette = Theme::instance()->palette();
    commitAction_->setIcon(Icons::icon(Icons::Glyph::Commit));
    if (commitBadge_ != nullptr) {
        const int count = hasRepository ? view->changeCount() : 0;
        commitBadge_->setText(count > 99 ? QStringLiteral("99+") : QString::number(count));
        commitBadge_->setVisible(count > 0);
    }
    for (int index = 1; index < tabs_->count(); ++index) {
        QWidget *area = tabs_->tabButton(index, QTabBar::RightSide);
        if (auto *button = area != nullptr ? area->findChild<QToolButton *>() : nullptr) {
            button->setIcon(Icons::icon(Icons::Glyph::TabClose, palette.text));
        }
    }
    updateTabCloseButtons();
    pullAction_->setIcon(Icons::badgedIcon(Icons::Glyph::Pull, QColor(),
                                           hasRepository ? view->behindCount() : 0,
                                           palette.danger));
    pushAction_->setIcon(Icons::badgedIcon(Icons::Glyph::Push, QColor(),
                                           hasRepository ? view->aheadCount() : 0,
                                           palette.success));

    if (hasRepository) {
        setWindowTitle(QStringLiteral("%1 — %2 — SquidyGit")
                           .arg(view->repositoryName(), view->currentBranchName()));
        statusLabel_->setText(tr("%1  ·  branch %2  ·  changes: %3  ·  ↑%4 ↓%5")
                                  .arg(QDir::toNativeSeparators(view->repositoryRoot()),
                                       view->currentBranchName())
                                  .arg(view->changeCount())
                                  .arg(view->aheadCount())
                                  .arg(view->behindCount()));
    } else {
        setWindowTitle(QStringLiteral("SquidyGit"));
        statusLabel_->setText(tr("Open or clone a repository"));
    }
}

void MainWindow::addBookmark(const QString &path) {
    const QString cleaned = QDir::cleanPath(path);
    bookmarks_.removeAll(cleaned);
    bookmarks_.prepend(cleaned);
    while (bookmarks_.size() > MaximumBookmarks) {
        bookmarks_.removeLast();
    }
    QSettings().setValue(QStringLiteral("bookmarks"), bookmarks_);
    refreshBookmarks();
}

void MainWindow::removeSelectedBookmark() {
    QListWidgetItem *item = bookmarksList_->currentItem();
    if (item == nullptr) {
        return;
    }
    bookmarks_.removeAll(item->data(Qt::UserRole).toString());
    QSettings().setValue(QStringLiteral("bookmarks"), bookmarks_);
    refreshBookmarks();
}

void MainWindow::refreshBookmarks() {
    bookmarksList_->clear();
    const QIcon icon = Icons::icon(Icons::Glyph::Repository);
    for (const QString &path : bookmarks_) {
        const QFileInfo info(path);
        auto *item = new QListWidgetItem(icon,
                                         QStringLiteral("%1\n%2")
                                             .arg(info.fileName().isEmpty() ? path
                                                                            : info.fileName(),
                                                  QDir::toNativeSeparators(path)),
                                         bookmarksList_);
        item->setData(Qt::UserRole, path);
        if (!info.exists()) {
            item->setForeground(Theme::instance()->palette().danger);
            item->setToolTip(tr("The folder no longer exists"));
        }
    }
}

void MainWindow::restoreSession() {
    QSettings settings;
    bookmarks_ = settings.value(QStringLiteral("bookmarks")).toStringList();
    refreshBookmarks();

    restoreGeometry(settings.value(QStringLiteral("windowGeometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("windowState")).toByteArray());
    logDock_->hide();
    logAction_->setChecked(false);

    const QStringList openTabs = settings.value(QStringLiteral("openTabs")).toStringList();
    for (const QString &path : openTabs) {
        if (GitClient::isRepository(path)) {
            openRepository(path, false);
        }
    }

    const int activeTab = settings.value(QStringLiteral("activeTab"), 0).toInt();
    if (activeTab > 0 && activeTab < tabs_->count()) {
        tabs_->setCurrentIndex(activeTab);
    }
    updateTabMetrics();
}

void MainWindow::saveSession() {
    QStringList openTabs;
    for (int index = 1; index < tabs_->count(); ++index) {
        RepositoryView *view = repositoryAt(index);
        if (view != nullptr) {
            openTabs.append(view->repositoryRoot());
        }
    }

    QSettings settings;
    settings.setValue(QStringLiteral("openTabs"), openTabs);
    settings.setValue(QStringLiteral("activeTab"), tabs_->currentIndex());
    settings.setValue(QStringLiteral("bookmarks"), bookmarks_);
    settings.setValue(QStringLiteral("windowGeometry"), saveGeometry());
    settings.setValue(QStringLiteral("windowState"), saveState());
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveSession();
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateWindowButtons();
    }
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
        // Rescan the working copy whenever the window regains focus.
        RepositoryView *view = currentRepository();
        if (view != nullptr && !view->isBusy()) {
            view->refreshAll();
        }
    }
}
