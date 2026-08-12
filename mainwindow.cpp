// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "mainwindow.h"

#include "dialogs.h"
#include "gitclient.h"
#include "icons.h"
#include "repositoryview.h"
#include "theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
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
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

namespace {
constexpr int MaximumBookmarks = 40;
constexpr int MaximumLogLines = 500;
/// Height of the combined menu/title row.
constexpr int TitleBarHeight = 44;
/// Transparent space reserved around the custom frame for its compositor-like
/// drop shadow. It is also a convenient resize target.
constexpr int WindowShadowMargin = 14;
/// Width of the invisible band along the window edges used for resizing,
/// which a frameless window has to provide itself.
constexpr int ResizeMargin = WindowShadowMargin;
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
    setContentsMargins(WindowShadowMargin, WindowShadowMargin,
                       WindowShadowMargin, WindowShadowMargin);
    setMouseTracking(true);

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
    updateWindowButtons();
    updateWindowFrame();
    updateActions();
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
    auto *tabStripLayout = new QHBoxLayout(tabStrip);
    tabStripLayout->setContentsMargins(8, 0, 4, 0);
    tabStripLayout->setSpacing(0);

    tabs_ = new QTabBar;
    tabs_->setObjectName(QStringLiteral("repositoryTabBar"));
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
    tabs_->addTab(QString());
    auto *homeTabTitle = new QLabel(QStringLiteral("Репозитории"));
    homeTabTitle->setObjectName(QStringLiteral("repositoryTabTitle"));
    homeTabTitle->setProperty("fullTitle", QStringLiteral("Репозитории"));
    homeTabTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    tabs_->setTabButton(0, QTabBar::LeftSide, homeTabTitle);
    tabs_->setTabButton(0, QTabBar::RightSide, nullptr);

    tabPages_ = new QStackedWidget;
    tabPages_->setObjectName(QStringLiteral("repositoryPages"));
    tabPages_->addWidget(buildWelcomePage());

    addTabButton_ = new QToolButton;
    addTabButton_->setObjectName(QStringLiteral("addTabButton"));
    addTabButton_->setIcon(Icons::icon(Icons::Glyph::Add, Qt::white));
    addTabButton_->setIconSize(QSize(24, 24));
    addTabButton_->setToolTip(QStringLiteral("Открыть репозиторий в новой вкладке"));
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

    statusLabel_ = new QLabel(QStringLiteral("Откройте или клонируйте репозиторий"));
    statusBar()->addWidget(statusLabel_, 1);
    busyIndicator_ = new QProgressBar;
    busyIndicator_->setRange(0, 0);
    busyIndicator_->setFixedWidth(120);
    busyIndicator_->setTextVisible(false);
    busyIndicator_->hide();
    statusBar()->addPermanentWidget(busyIndicator_);
}

void MainWindow::buildToolbar() {
    mainToolbar_ = new QToolBar(QStringLiteral("Действия"), this);
    mainToolbar_->setObjectName(QStringLiteral("mainToolbar"));
    mainToolbar_->setMovable(false);
    mainToolbar_->setFloatable(false);
    mainToolbar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    mainToolbar_->setIconSize(QSize(34, 34));
    mainToolbar_->setFixedHeight(84);

    const auto addAction = [this](const Icons::Glyph glyph, const QString &text,
                                  const QString &tip) {
        QAction *action = mainToolbar_->addAction(Icons::icon(glyph), text);
        action->setToolTip(tip);
        return action;
    };

    // Keep the frequent commands in this exact order.
    commitAction_ = addAction(Icons::Glyph::Commit, QStringLiteral("Commit"),
                              QStringLiteral("Зафиксировать проиндексированные изменения"));
    mainToolbar_->addSeparator();
    pushAction_ = addAction(Icons::Glyph::Push, QStringLiteral("Push"),
                            QStringLiteral("Отправить изменения"));
    pullAction_ = addAction(Icons::Glyph::Pull, QStringLiteral("Pull"),
                            QStringLiteral("Забрать и влить изменения"));
    fetchAction_ = addAction(Icons::Glyph::Fetch, QStringLiteral("Fetch"),
                             QStringLiteral("Получить изменения из всех удалённых репозиториев"));
    mainToolbar_->addSeparator();
    branchAction_ = addAction(Icons::Glyph::Branch, QStringLiteral("Branch"),
                              QStringLiteral("Создать ветку"));
    mergeAction_ = addAction(Icons::Glyph::Merge, QStringLiteral("Merge"),
                             QStringLiteral("Влить ветку в текущую"));
    mainToolbar_->addSeparator();
    stashAction_ = addAction(Icons::Glyph::Stash, QStringLiteral("Stash"),
                             QStringLiteral("Спрятать текущие изменения"));
    mainToolbar_->addSeparator();
    discardAction_ = addAction(Icons::Glyph::Discard, QStringLiteral("Discard"),
                               QStringLiteral("Откатить выбранные файлы"));
    tagAction_ = addAction(Icons::Glyph::Tag, QStringLiteral("Tag"),
                           QStringLiteral("Создать тег"));

    // Less frequent operations stay available in Repository/Actions menus.
    checkoutAction_ = new QAction(Icons::icon(Icons::Glyph::Checkout),
                                  QStringLiteral("Checkout"), this);
    checkoutAction_->setToolTip(QStringLiteral("Переключиться на ветку или тег"));
    stashPopAction_ = new QAction(Icons::icon(Icons::Glyph::StashPop),
                                  QStringLiteral("Применить последний stash"), this);
    stashPopAction_->setToolTip(QStringLiteral("Вернуть последний stash"));

    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    spacer->setAttribute(Qt::WA_TranslucentBackground);
    mainToolbar_->addWidget(spacer);

    terminalAction_ = addAction(Icons::Glyph::Terminal, QStringLiteral("Терминал"),
                                QStringLiteral("Открыть терминал в папке репозитория"));
    explorerAction_ = addAction(Icons::Glyph::Explorer, QStringLiteral("Папка"),
                                QStringLiteral("Открыть папку репозитория"));
    settingsAction_ = addAction(Icons::Glyph::Settings, QStringLiteral("Настройки"),
                                QStringLiteral("Настройки репозитория и приложения"));

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
    logo->setPixmap(Icons::pixmap(Icons::Glyph::Ghost, 33, Qt::white));
    logo->setFixedWidth(45);
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
        button->setIcon(Icons::icon(glyph, QColor()));
        button->setIconSize(QSize(24, 24));
        button->setFixedSize(QSize(64, TitleBarHeight));
        button->setToolTip(tip);
        layout->addWidget(button);
        return button;
    };

    QToolButton *minimizeButton = addWindowButton(Icons::Glyph::WindowMinimize,
                                                  QStringLiteral("Свернуть"), false);
    maximizeButton_ = addWindowButton(Icons::Glyph::WindowMaximize,
                                      QStringLiteral("Развернуть"), false);
    QToolButton *closeButton = addWindowButton(Icons::Glyph::WindowClose,
                                               QStringLiteral("Закрыть"), true);

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
                                             QColor()));
        maximizeButton_->setToolTip(maximized ? QStringLiteral("Восстановить")
                                              : QStringLiteral("Развернуть"));
    }
    // A maximized window has no edges to drag, so the resize band is dropped.
    const int margin = maximized || isFullScreen() ? 0 : WindowShadowMargin;
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
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::buildMenus() {
    menuBar_ = new QMenuBar;
    menuBar_->setNativeMenuBar(false);
    menuBar_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    auto *fileMenu = menuBar_->addMenu(QStringLiteral("&Файл"));
    openAction_ = fileMenu->addAction(Icons::icon(Icons::Glyph::OpenFolder),
                                      QStringLiteral("Открыть репозиторий…"));
    openAction_->setShortcut(QKeySequence::Open);
    cloneAction_ = fileMenu->addAction(Icons::icon(Icons::Glyph::Clone),
                                       QStringLiteral("Клонировать…"));
    cloneAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    createAction_ = fileMenu->addAction(Icons::icon(Icons::Glyph::Create),
                                        QStringLiteral("Создать репозиторий…"));
    fileMenu->addSeparator();
    closeTabAction_ = fileMenu->addAction(QStringLiteral("Закрыть вкладку"));
    closeTabAction_->setShortcut(QKeySequence::Close);
    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(QStringLiteral("Выход"));
    quitAction->setShortcut(QKeySequence::Quit);

    auto *editMenu = menuBar_->addMenu(QStringLiteral("&Правка"));
    editMenu->addAction(checkoutAction_);
    editMenu->addSeparator();
    editMenu->addAction(settingsAction_);

    auto *viewMenu = menuBar_->addMenu(QStringLiteral("&Вид"));
    fileStatusPageAction_ = viewMenu->addAction(Icons::icon(Icons::Glyph::FileStatus),
                                                QStringLiteral("Состояние файлов"));
    fileStatusPageAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    historyPageAction_ = viewMenu->addAction(Icons::icon(Icons::Glyph::History),
                                             QStringLiteral("История"));
    historyPageAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    searchPageAction_ = viewMenu->addAction(Icons::icon(Icons::Glyph::Search),
                                            QStringLiteral("Поиск"));
    searchPageAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
    viewMenu->addSeparator();
    themeAction_ = viewMenu->addAction(QStringLiteral("Тёмная тема"));
    themeAction_->setCheckable(true);
    themeAction_->setChecked(Theme::instance()->mode() == Theme::Mode::Dark);

    auto *repositoryMenu = menuBar_->addMenu(QStringLiteral("&Репозиторий"));
    refreshAction_ = repositoryMenu->addAction(Icons::icon(Icons::Glyph::Refresh),
                                               QStringLiteral("Обновить"));
    refreshAction_->setShortcut(QKeySequence::Refresh);
    repositoryMenu->addSeparator();
    repositoryMenu->addAction(checkoutAction_);
    repositoryMenu->addAction(branchAction_);
    repositoryMenu->addAction(mergeAction_);
    repositoryMenu->addAction(tagAction_);
    repositoryMenu->addSeparator();
    QAction *addRemoteAction = repositoryMenu->addAction(Icons::icon(Icons::Glyph::Remote),
                                                         QStringLiteral("Добавить remote…"));

    auto *actionsMenu = menuBar_->addMenu(QStringLiteral("&Действия"));
    actionsMenu->addAction(commitAction_);
    actionsMenu->addAction(discardAction_);
    actionsMenu->addSeparator();
    actionsMenu->addAction(stashAction_);
    actionsMenu->addAction(stashPopAction_);
    actionsMenu->addSeparator();
    actionsMenu->addAction(fetchAction_);
    actionsMenu->addAction(pullAction_);
    actionsMenu->addAction(pushAction_);

    auto *toolsMenu = menuBar_->addMenu(QStringLiteral("&Инструменты"));
    toolsMenu->addAction(terminalAction_);
    toolsMenu->addAction(explorerAction_);
    toolsMenu->addSeparator();
    logAction_ = toolsMenu->addAction(QStringLiteral("Журнал команд git"));
    logAction_->setCheckable(true);
    logAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+`")));

    auto *helpMenu = menuBar_->addMenu(QStringLiteral("&Справка"));
    QAction *aboutAction = helpMenu->addAction(QStringLiteral("О программе"));

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
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, QStringLiteral("О SquidyGit"),
                           QStringLiteral(
                               "<h3>SquidyGit</h3>"
                               "<p>Настольный Git-клиент: вкладки репозиториев, "
                               "граф коммитов, индексация по ханкам и строкам, ветки, теги, "
                               "stash и работа с удалёнными репозиториями.</p>"
                               "<p>Собран на Qt %1, использует системный git.</p>")
                               .arg(QStringLiteral(QT_VERSION_STR)));
    });
}

QWidget *MainWindow::buildWelcomePage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("Локальные репозитории"));
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);

    auto *subtitle = new QLabel(QStringLiteral(
        "Закладки сохраняются между запусками. Двойной щелчок открывает репозиторий во вкладке."));
    subtitle->setObjectName(QStringLiteral("mutedText"));
    layout->addWidget(subtitle);

    auto *buttonRow = new QHBoxLayout;
    auto *cloneButton = new QPushButton(Icons::icon(Icons::Glyph::Clone, Qt::white),
                                        QStringLiteral("Клонировать"));
    cloneButton->setProperty("accent", true);
    auto *openButton = new QPushButton(Icons::icon(Icons::Glyph::OpenFolder),
                                       QStringLiteral("Добавить существующий"));
    auto *createButton = new QPushButton(Icons::icon(Icons::Glyph::Create),
                                         QStringLiteral("Создать новый"));
    auto *removeButton = new QPushButton(Icons::icon(Icons::Glyph::Remove),
                                         QStringLiteral("Убрать из списка"));
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
    auto *dock = new QDockWidget(QStringLiteral("Журнал команд git"), this);
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
        this, QStringLiteral("Открыть Git-репозиторий"), QDir::homePath());
    if (directory.isEmpty()) {
        return;
    }
    if (!GitClient::isRepository(directory)) {
        QMessageBox::warning(this, QStringLiteral("Не Git-репозиторий"),
                             QStringLiteral("В папке %1 нет репозитория Git.")
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
        statusLabel_->setText(QStringLiteral("Не удалось открыть репозиторий: %1")
                                  .arg(QDir::toNativeSeparators(path)));
        return false;
    }

    const int index = tabs_->addTab(QString());
    tabPages_->insertWidget(index, view);
    tabs_->setTabToolTip(index, QDir::toNativeSeparators(view->repositoryRoot()));

    auto *tabTitle = new QLabel(view->repositoryName());
    tabTitle->setObjectName(QStringLiteral("repositoryTabTitle"));
    tabTitle->setProperty("fullTitle", view->repositoryName());
    tabTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    tabs_->setTabButton(index, QTabBar::LeftSide, tabTitle);

    // QTabBar anchors its right-side button directly to the tab edge. A small
    // The wrapper reserves visual breathing room without enlarging the
    // square hover target itself.
    auto *closeArea = new QWidget;
    closeArea->setObjectName(QStringLiteral("tabCloseArea"));
    closeArea->setFixedSize(QSize(32, 24));
    auto *closeLayout = new QHBoxLayout(closeArea);
    closeLayout->setContentsMargins(0, 0, 8, 0);
    closeLayout->setSpacing(0);

    auto *closeButton = new QToolButton;
    closeButton->setObjectName(QStringLiteral("tabCloseButton"));
    closeButton->setIcon(Icons::icon(Icons::Glyph::TabClose,
                                     Theme::instance()->palette().text));
    closeButton->setIconSize(QSize(22, 22));
    closeButton->setFixedSize(QSize(24, 24));
    closeButton->setToolTip(QStringLiteral("Закрыть вкладку"));
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
        this, QStringLiteral("Папка для нового репозитория"), QDir::homePath());
    if (directory.isEmpty()) {
        return;
    }

    if (GitClient::isRepository(directory)) {
        openRepository(directory);
        return;
    }

    const GitCommandResult result = GitClient::initRepository(directory, false);
    if (!result.succeeded()) {
        QMessageBox::warning(this, QStringLiteral("Не удалось создать репозиторий"),
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
    QAction *closeAll = menu.addAction(QStringLiteral("Закрыть все вкладки"));
    QAction *closeLeft = menu.addAction(QStringLiteral("Закрыть вкладки слева"));
    QAction *closeRight = menu.addAction(QStringLiteral("Закрыть вкладки справа"));

    closeAll->setEnabled(tabs_->count() > 1);
    closeLeft->setEnabled(target > 1);
    closeRight->setEnabled(target < tabs_->count() - 1);

    QAction *selected = menu.exec(tabs_->mapToGlobal(position));
    if (selected == closeAll) {
        for (int index = tabs_->count() - 1; index >= 1; --index) {
            closeTab(index);
        }
    } else if (selected == closeLeft) {
        for (int index = target - 1; index >= 1; --index) {
            closeTab(index);
        }
    } else if (selected == closeRight) {
        for (int index = tabs_->count() - 1; index > target; --index) {
            closeTab(index);
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

    QString title = view->repositoryName();
    if (view->changeCount() > 0) {
        title += QStringLiteral(" •");
    }
    if (auto *label = qobject_cast<QLabel *>(
            tabs_->tabButton(index, QTabBar::LeftSide))) {
        label->setProperty("fullTitle", title);
    }
    tabs_->setTabToolTip(index, QStringLiteral("%1\nВетка: %2")
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
    const int outerWidth = qBound(40, available / tabs_->count(), 282);
    const int horizontalPadding = qBound(1, (outerWidth - 37) / 2, 12);
    const int contentWidth = qMax(32, outerWidth - horizontalPadding * 2 - 5);
    tabs_->setStyleSheet(
        QStringLiteral("QTabBar#repositoryTabBar::tab { min-width: %1px; max-width: %1px; "
                       "padding-left: %2px; padding-right: %2px; }")
            .arg(contentWidth)
            .arg(horizontalPadding));

    for (int index = 0; index < tabs_->count(); ++index) {
        auto *label = qobject_cast<QLabel *>(tabs_->tabButton(index, QTabBar::LeftSide));
        if (label == nullptr) {
            continue;
        }

        const int reservedForClose = index == 0 ? 0 : 32;
        const int labelWidth = qMax(0, contentWidth - reservedForClose);
        label->setFixedWidth(labelWidth);
        const QString fullTitle = label->property("fullTitle").toString();
        const int textWidth = qMax(0, labelWidth - 17); // label stylesheet padding
        label->setText(textWidth == 0
                           ? QString()
                           : label->fontMetrics().elidedText(fullTitle, Qt::ElideRight,
                                                             textWidth));
    }
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
    for (int index = 1; index < tabs_->count(); ++index) {
        QWidget *area = tabs_->tabButton(index, QTabBar::RightSide);
        if (auto *button = area != nullptr ? area->findChild<QToolButton *>() : nullptr) {
            button->setIcon(Icons::icon(Icons::Glyph::TabClose, palette.text));
            area->setVisible(index == tabs_->currentIndex());
        }
    }
    pullAction_->setIcon(Icons::badgedIcon(Icons::Glyph::Pull, QColor(),
                                           hasRepository ? view->behindCount() : 0,
                                           palette.danger));
    pushAction_->setIcon(Icons::badgedIcon(Icons::Glyph::Push, QColor(),
                                           hasRepository ? view->aheadCount() : 0,
                                           palette.success));

    if (hasRepository) {
        setWindowTitle(QStringLiteral("%1 — %2 — SquidyGit")
                           .arg(view->repositoryName(), view->currentBranchName()));
        statusLabel_->setText(QStringLiteral("%1  ·  ветка %2  ·  изменений: %3  ·  ↑%4 ↓%5")
                                  .arg(QDir::toNativeSeparators(view->repositoryRoot()),
                                       view->currentBranchName())
                                  .arg(view->changeCount())
                                  .arg(view->aheadCount())
                                  .arg(view->behindCount()));
    } else {
        setWindowTitle(QStringLiteral("SquidyGit"));
        statusLabel_->setText(QStringLiteral("Откройте или клонируйте репозиторий"));
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
            item->setToolTip(QStringLiteral("Папка больше не существует"));
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
