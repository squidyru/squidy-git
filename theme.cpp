// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>

namespace {

ThemePalette lightPalette() {
    ThemePalette palette;
    palette.window = QColor(QStringLiteral("#F5F5F5"));
    palette.chrome = QColor(QStringLiteral("#0D4FA3"));
    palette.chromeText = QColor(QStringLiteral("#FFFFFF"));
    palette.tabInactive = QColor(QStringLiteral("#E9E9E9"));
    palette.sidebarSelection = QColor(QStringLiteral("#3399FF"));
    palette.toolbar = QColor(QStringLiteral("#F5F5F5"));
    palette.sidebar = QColor(QStringLiteral("#F5F5F5"));
    palette.surface = QColor(QStringLiteral("#FFFFFF"));
    palette.surfaceAlternate = QColor(QStringLiteral("#FAFBFC"));
    palette.border = QColor(QStringLiteral("#CCCCCC"));
    palette.text = QColor(QStringLiteral("#232323"));
    palette.mutedText = QColor(QStringLiteral("#737373"));
    palette.sectionText = QColor(QStringLiteral("#737373"));
    palette.accent = QColor(QStringLiteral("#0052CC"));
    palette.accentHover = QColor(QStringLiteral("#0065FF"));
    palette.selection = QColor(QStringLiteral("#3399FF"));
    palette.selectionText = QColor(QStringLiteral("#FFFFFF"));
    palette.hover = QColor(QStringLiteral("#EBECF0"));
    palette.success = QColor(QStringLiteral("#00875A"));
    palette.warning = QColor(QStringLiteral("#FF8B00"));
    palette.danger = QColor(QStringLiteral("#DE350B"));
    palette.staged = QColor(QStringLiteral("#00875A"));
    palette.addedBackground = QColor(QStringLiteral("#E3FCEF"));
    palette.addedText = QColor(QStringLiteral("#006644"));
    palette.removedBackground = QColor(QStringLiteral("#FFEBE6"));
    palette.removedText = QColor(QStringLiteral("#BF2600"));
    palette.hunkBackground = QColor(QStringLiteral("#DEEBFF"));
    palette.hunkText = QColor(QStringLiteral("#0747A6"));
    palette.graphNodeBorder = QColor(QStringLiteral("#FFFFFF"));
    palette.laneColors = {
        QColor(QStringLiteral("#337AB7")),
        QColor(QStringLiteral("#00875A")),
        QColor(QStringLiteral("#FF8B00")),
        QColor(QStringLiteral("#6554C0")),
        QColor(QStringLiteral("#DE350B")),
        QColor(QStringLiteral("#00A3BF")),
        QColor(QStringLiteral("#C1539C"))
    };
    return palette;
}

ThemePalette darkPalette() {
    ThemePalette palette;
    palette.window = QColor(QStringLiteral("#0D1422"));
    palette.chrome = QColor(QStringLiteral("#10243D"));
    palette.chromeText = QColor(QStringLiteral("#DCE6F4"));
    palette.tabInactive = QColor(QStringLiteral("#16243A"));
    palette.sidebarSelection = QColor(QStringLiteral("#23324A"));
    palette.toolbar = QColor(QStringLiteral("#111A2B"));
    palette.sidebar = QColor(QStringLiteral("#101929"));
    palette.surface = QColor(QStringLiteral("#0B1220"));
    palette.surfaceAlternate = QColor(QStringLiteral("#0F192A"));
    palette.border = QColor(QStringLiteral("#26344A"));
    palette.text = QColor(QStringLiteral("#DCE6F4"));
    palette.mutedText = QColor(QStringLiteral("#8191A9"));
    palette.sectionText = QColor(QStringLiteral("#71829B"));
    palette.accent = QColor(QStringLiteral("#2F81F7"));
    palette.accentHover = QColor(QStringLiteral("#4C9AFF"));
    palette.selection = QColor(QStringLiteral("#1D4F8F"));
    palette.selectionText = QColor(QStringLiteral("#FFFFFF"));
    palette.hover = QColor(QStringLiteral("#18263B"));
    palette.success = QColor(QStringLiteral("#3FB950"));
    palette.warning = QColor(QStringLiteral("#E3B341"));
    palette.danger = QColor(QStringLiteral("#F85149"));
    palette.staged = QColor(QStringLiteral("#7EE787"));
    palette.addedBackground = QColor(QStringLiteral("#102B20"));
    palette.addedText = QColor(QStringLiteral("#7EE787"));
    palette.removedBackground = QColor(QStringLiteral("#331C24"));
    palette.removedText = QColor(QStringLiteral("#FFA198"));
    palette.hunkBackground = QColor(QStringLiteral("#13233A"));
    palette.hunkText = QColor(QStringLiteral("#79C0FF"));
    palette.graphNodeBorder = QColor(QStringLiteral("#0B1220"));
    palette.laneColors = {
        QColor(QStringLiteral("#58A6FF")),
        QColor(QStringLiteral("#3FB950")),
        QColor(QStringLiteral("#E3B341")),
        QColor(QStringLiteral("#BC8CFF")),
        QColor(QStringLiteral("#F85149")),
        QColor(QStringLiteral("#39C5CF")),
        QColor(QStringLiteral("#F778BA"))
    };
    return palette;
}

QString name(const QColor &color) {
    return color.name(QColor::HexRgb);
}

}

Theme::Theme(QObject *parent)
    : QObject(parent) {
    const QString stored = QSettings().value(QStringLiteral("theme"),
                                             QStringLiteral("light")).toString();
    mode_ = stored == QStringLiteral("dark") ? Mode::Dark : Mode::Light;
    palette_ = mode_ == Mode::Dark ? darkPalette() : lightPalette();
}

Theme *Theme::instance() {
    static Theme theme;
    return &theme;
}

Theme::Mode Theme::mode() const {
    return mode_;
}

void Theme::setMode(const Mode mode) {
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    palette_ = mode_ == Mode::Dark ? darkPalette() : lightPalette();
    QSettings().setValue(QStringLiteral("theme"),
                         mode_ == Mode::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
    applyToApplication();
    Q_EMIT changed();
}

const ThemePalette &Theme::palette() const {
    return palette_;
}

void Theme::applyToApplication() const {
    QPalette applicationPalette;
    applicationPalette.setColor(QPalette::Window, palette_.window);
    applicationPalette.setColor(QPalette::WindowText, palette_.text);
    applicationPalette.setColor(QPalette::Base, palette_.surface);
    applicationPalette.setColor(QPalette::AlternateBase, palette_.surfaceAlternate);
    applicationPalette.setColor(QPalette::Text, palette_.text);
    applicationPalette.setColor(QPalette::Button, palette_.window);
    applicationPalette.setColor(QPalette::ButtonText, palette_.text);
    applicationPalette.setColor(QPalette::Highlight, palette_.selection);
    applicationPalette.setColor(QPalette::HighlightedText, palette_.selectionText);
    applicationPalette.setColor(QPalette::ToolTipBase, palette_.surface);
    applicationPalette.setColor(QPalette::ToolTipText, palette_.text);
    applicationPalette.setColor(QPalette::PlaceholderText, palette_.mutedText);
    applicationPalette.setColor(QPalette::Link, palette_.accent);
    QApplication::setPalette(applicationPalette);

    if (auto *application = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        application->setStyleSheet(styleSheet());
    }
}

QString Theme::styleSheet() const {
    const ThemePalette &p = palette_;
    const QColor sidebarText = mode_ == Mode::Light
                                   ? QColor(QStringLiteral("#454545"))
                                   : p.text;
    const QColor historyControl = mode_ == Mode::Light
                                      ? QColor(QStringLiteral("#E5E5E5"))
                                      : p.surfaceAlternate;
    const QColor historyControlBorder = mode_ == Mode::Light
                                            ? QColor(QStringLiteral("#A8A8A8"))
                                            : p.border;
    const QColor historyScrollTrack = mode_ == Mode::Light
                                          ? QColor(QStringLiteral("#F0F0F0"))
                                          : p.window;
    const QColor historyHeaderText = mode_ == Mode::Light
                                         ? QColor(QStringLiteral("#000000"))
                                         : p.text;
    const QColor windowFrameBorder(
        (p.border.red() + p.chrome.red()) / 2,
        (p.border.green() + p.chrome.green()) / 2,
        (p.border.blue() + p.chrome.blue()) / 2);
    return QStringLiteral(R"(
        QWidget {
            background: %(window);
            color: %(text);
            font-size: 13px;
        }
        QMainWindow::separator { background: %(border); width: 1px; height: 1px; }
        QToolBar#mainToolbar {
            background: %(toolbar);
            border: none;
            border-bottom: 1px solid %(border);
            spacing: 0px;
            padding: 3px 8px 4px 8px;
        }
        QToolBar#mainToolbar QToolButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 2px;
            color: #232323;
            font-family: "Arial";
            font-size: 12px;
            font-weight: 400;
            min-width: 52px;
            padding: 2px 5px;
        }
        QToolBar#mainToolbar QToolButton:hover {
            background: %(hover);
            border-color: %(border);
        }
        QToolBar#mainToolbar QToolButton:pressed { background: %(selection); color: %(selectionText); }
        QToolBar#mainToolbar QToolButton:disabled { color: %(mutedText); }
        QLabel#commitBadge {
            background: %(accent);
            color: white;
            border-radius: 6px;
            font-size: 9px;
            font-weight: 700;
            padding: 0 2px;
        }
        QToolBar#mainToolbar::separator {
            background: transparent;
            width: 1px;
            margin: 4px 10px;
        }

        /* Paint the title bar, menu and tab strip as one dark blue
           band; the window itself is frameless, so QMainWindow's own background
           forms the thin resize border. */
        QMainWindow { background: transparent; }
        QWidget#windowShadowFrame {
            background: %(chrome);
            border: 1px solid %(windowFrameBorder);
        }
        QWidget#titleBar { background: %(chrome); }
        QToolButton#windowButton, QToolButton#windowCloseButton {
            background: transparent;
            border: none;
            border-radius: 0;
            padding: 0;
        }
        QToolButton#windowButton:hover { background: rgba(255, 255, 255, 45); }
        QToolButton#windowCloseButton:hover { background: #E81123; }
        QWidget#tabCloseArea { background: transparent; }
        QToolButton#tabCloseButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 0;
            padding: 0;
        }
        QToolButton#tabCloseButton:hover {
            background: #F20D0D;
            border-color: #F20D0D;
        }

        QMenuBar {
            background: transparent;
            color: %(chromeText);
            border: none;
            padding: 0 2px;
            font-family: "Arial";
            font-size: 12px;
            font-weight: 400;
        }
        QMenuBar::item { padding: 4px 8px; background: transparent; color: %(chromeText); }
        QMenuBar::item:selected { background: rgba(255, 255, 255, 40); border-radius: 3px; }
        QMenuBar::item:pressed { background: rgba(255, 255, 255, 70); border-radius: 3px; }

        QWidget#workspace { background: %(window); }
        QWidget#repositoryTabStrip { background: %(chrome); }
        QStackedWidget#repositoryPages { background: %(window); }
        QTabBar#repositoryTabBar { background: %(chrome); qproperty-drawBase: 0; }
        QTabBar#repositoryTabBar::tab {
            background: %(tabInactive);
            color: %(text);
            border: none;
            border-left: 1px solid %(chrome);
            border-bottom: 1px solid #B8B8B8;
            border-top-left-radius: 1px;
            border-top-right-radius: 1px;
            min-height: 24px;
            max-height: 24px;
            padding: 1px 8px 0 8px;
            margin: 0;
            min-width: 120px;
            max-width: 280px;
        }
        QTabBar#repositoryTabBar::tab:first { border-left: none; }
        QTabBar#repositoryTabBar::tab:selected {
            background: %(toolbar);
            border-left: 1px solid %(chrome);
            /* Preserve the separator when the selected tab overlaps its neighbour. */
            border-right: 1px solid %(chrome);
            border-top: none;
            border-bottom: none;
        }
        QTabBar#repositoryTabBar::tab:first:selected { border-left: none; }
        QTabBar#repositoryTabBar::tab:hover { background: %(toolbar); }
        QTabBar#repositoryTabBar::close-button { subcontrol-position: right; }
        QLabel#repositoryTabTitle {
            background: transparent;
            color: #232323;
            font-family: "Arial";
            font-size: 12px;
            font-weight: 400;
            padding: 0 2px 0 11px;
        }
        QToolButton#addTabButton {
            background: transparent;
            border: none;
            color: %(chromeText);
            min-width: 28px;
            max-width: 28px;
            padding: 0;
            margin: 0;
        }
        QToolButton#addTabButton:hover { background: rgba(0, 0, 0, 32); border-radius: 0; }
        QToolButton#addTabButton:pressed { background: rgba(0, 0, 0, 55); }

        QPushButton#viewSwitchButton {
            background: transparent;
            border: none;
            border-radius: 0;
            min-height: 25px;
            padding: 0 4px 0 45px;
            text-align: left;
            color: %(text);
        }
        QPushButton#viewSwitchButton:hover { background: %(hover); }
        QPushButton#viewSwitchButton:checked {
            background: %(sidebarSelection);
            color: #FFFFFF;
            font-weight: 400;
        }

        /* Checkboxes are intentionally left unstyled: touching them in a
           stylesheet drops the native indicator glyph. */
        QLabel, QGroupBox::title, QSplitter { background: transparent; }
        QWidget#stateBanner {
            background: %(removedBackground);
            border-bottom: 1px solid %(border);
        }
        QLabel#stateBadge { color: %(removedText); font-weight: 600; }
        QWidget#viewSwitcher { background: %(window); border: none; }
        QWidget#workspaceHeader { min-height: 30px; background: transparent; }
        QLabel#workspaceTitle { min-height: 30px; color: %(sectionText); font-size: 12px; font-weight: 600; }
        QFrame#sidebarTopSeparator { color: %(border); margin: 5px 0 10px 0; }
        QFrame#sidebarSeparator { color: %(border); margin: 7px 0 0 0; }
        QLabel#mutedText { color: %(mutedText); }
        QLabel#pageTitle { font-size: 17px; font-weight: 700; }
        QLabel#sectionCaption {
            color: %(sectionText);
            font-size: 11px;
            font-weight: 700;
        }

        QWidget#sidebar {
            background: %(sidebar);
            border: none;
            border-right: 1px solid %(border);
        }
        QTreeWidget#navigationTree {
            background: transparent;
            border: none;
            outline: none;
            color: %(sidebarText);
            font-family: "Arial";
            font-size: 12px;
            font-weight: 400;
        }
        QTreeWidget#navigationTree::item {
            border-radius: 0;
            padding: 0;
        }
        QTreeWidget#navigationTree::item:hover { background: %(hover); }
        QTreeWidget#navigationTree::item:selected {
            background: %(sidebarSelection);
            color: #FFFFFF;
        }
        QLineEdit#navigationFilter {
            min-height: 21px;
            max-height: 21px;
            margin: 4px 2px 4px 0;
            padding: 0 5px;
            font-size: 12px;
        }

        QComboBox#historyScope, QComboBox#historyOrder,
        QLineEdit#historyAuthorFilter, QToolButton#historyJumpButton {
            min-height: 20px;
            max-height: 20px;
            padding: 0 5px;
            font-family: "Arial";
            font-size: 12px;
        }
        QComboBox#historyScope, QComboBox#historyOrder {
            background: %(historyControl);
            border-color: %(historyControlBorder);
        }
        QComboBox#historyScope::down-arrow,
        QComboBox#historyOrder::down-arrow {
            image: none;
            width: 0;
            height: 0;
        }
        QLineEdit#historyAuthorFilter {
            padding-right: 20px;
        }
        QToolButton#historyJumpButton {
            background: transparent;
            color: %(text);
            border: 1px solid transparent;
            border-radius: 0;
            padding: 0 3px;
        }
        QToolButton#historyJumpButton:hover,
        QToolButton#historyJumpButton:focus {
            background: %(historyControl);
            border-color: %(historyControlBorder);
        }
        QToolButton#historyJumpButton::menu-indicator {
            image: none;
            width: 0;
        }
        QMenu#historyJumpMenu {
            padding: 2px;
            border: 1px solid %(historyControlBorder);
        }
        QMenu#historyJumpMenu::item {
            padding: 5px 4px;
            border-radius: 0;
        }
        QMenu#historyJumpMenu::separator {
            margin: 2px 0;
        }
        QWidget#historyPage QCheckBox, QWidget#historyPage QLabel {
            font-family: "Arial";
            font-size: 12px;
        }

        QLineEdit, QComboBox, QPlainTextEdit, QTextEdit, QSpinBox {
            background: %(surface);
            color: %(text);
            border: 1px solid %(border);
            border-radius: 1px;
            padding: 5px 7px;
            selection-background-color: %(selection);
            selection-color: %(selectionText);
        }
        QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus {
            border-color: %(accent);
        }
        QTextBrowser#commitDetails { padding: 0; }
        QPlainTextEdit#diffView { padding: 0; }
        QDialog#sideBySideDiffDialog { background: transparent; }
        QFrame#sideDiffWindowFrame {
            background: %(window);
            border: 1px solid rgba(0, 0, 0, 75);
        }
        QWidget#sideDiffTitleBar { background: %(chrome); }
        QLabel#sideDiffWindowTitle {
            color: %(chromeText);
            font-family: "Arial";
            font-size: 12px;
            font-weight: 400;
        }
        QToolButton#sideDiffWindowButton,
        QToolButton#sideDiffCloseButton {
            background: transparent;
            border: none;
            border-radius: 0;
            padding: 0;
        }
        QToolButton#sideDiffWindowButton:hover {
            background: rgba(255, 255, 255, 45);
        }
        QToolButton#sideDiffCloseButton:hover { background: #E81123; }
        QWidget#sideDiffControls {
            background: %(toolbar);
            border-bottom: 1px solid %(border);
        }
        QWidget#sideDiffControls QCheckBox {
            background: transparent;
            color: %(text);
            font-family: "Arial";
            font-size: 12px;
        }
        QWidget#sideDiffPanel { background: %(surface); }
        QLabel#sideDiffHeader {
            background: %(surfaceAlternate);
            color: %(sectionText);
            border: 1px solid %(border);
            border-bottom: none;
            padding: 0 8px;
            font-family: "Arial";
            font-size: 11px;
            font-weight: 700;
        }
        QPlainTextEdit#sideDiffPane {
            background: %(surface);
            color: %(text);
            border: 1px solid %(border);
            border-radius: 0;
            padding: 0;
        }
        QPlainTextEdit#sideDiffPane:focus { border-color: %(accent); }
        QSplitter#sideDiffSplitter::handle:horizontal {
            background: %(border);
            border: none;
            margin: 0;
        }
        QComboBox::drop-down { border: none; width: 18px; }
        QComboBox QAbstractItemView {
            background: %(surface);
            border: 1px solid %(border);
            selection-background-color: %(selection);
            selection-color: %(selectionText);
        }

        QTreeWidget, QTreeView, QListWidget, QTableWidget {
            background: %(surface);
            alternate-background-color: %(surfaceAlternate);
            color: %(text);
            border: 1px solid %(border);
            border-radius: 1px;
            outline: none;
            selection-background-color: %(selection);
            selection-color: %(selectionText);
        }
        QTreeWidget::item, QListWidget::item { min-height: 18px; padding: 0 3px; }
        QTreeWidget::item:hover, QListWidget::item:hover { background: %(hover); }
        QTreeWidget::item:selected, QListWidget::item:selected {
            background: %(selection);
            color: %(selectionText);
        }
        QTreeWidget#historyTree {
            background: %(surface);
            alternate-background-color: %(surface);
            color: %(text);
            font-family: "Arial";
            font-size: 12px;
        }
        QTreeWidget#historyTree::item {
            min-height: 19px;
            max-height: 19px;
            padding: 0 3px;
        }
        QTreeWidget#historyTree QHeaderView::section {
            background: %(surface);
            color: %(historyHeaderText);
            border: none;
            border-right: 1px solid %(border);
            border-bottom: 1px solid %(border);
            min-height: 19px;
            max-height: 19px;
            padding: 0 6px;
            font-family: "Arial";
            font-size: 12px;
            font-weight: 400;
        }
        QTreeWidget#historyTree QScrollBar:vertical {
            background: %(historyScrollTrack);
            border-left: 1px solid %(border);
            width: 16px;
        }
        QTreeWidget#historyTree QScrollBar:horizontal {
            background: %(historyScrollTrack);
            border-top: 1px solid %(border);
            height: 16px;
        }
        QTreeWidget#historyTree QScrollBar::handle:vertical,
        QTreeWidget#historyTree QScrollBar::handle:horizontal {
            background: %(border);
            border-radius: 0;
        }
        QHeaderView::section {
            background: %(window);
            color: %(mutedText);
            border: none;
            border-right: 1px solid %(border);
            border-bottom: 1px solid %(border);
            padding: 2px 6px;
            font-weight: 400;
        }

        QPushButton {
            background: %(surface);
            color: %(text);
            border: 1px solid %(border);
            border-radius: 1px;
            padding: 6px 12px;
        }
        QPushButton:hover { background: %(hover); }
        QPushButton:pressed { background: %(border); }
        QPushButton:disabled { color: %(mutedText); background: %(window); }
        QPushButton[accent="true"] {
            background: %(accent);
            color: #FFFFFF;
            border-color: %(accent);
            font-weight: 600;
        }
        QPushButton[accent="true"]:hover { background: %(accentHover); }
        QPushButton[accent="true"]:disabled { background: %(hover); color: %(mutedText); border-color: %(border); }
        QPushButton[danger="true"] { color: %(danger); }
        QPushButton#linkButton {
            background: transparent;
            border: none;
            color: %(accent);
            text-align: left;
            padding: 2px;
        }

        QGroupBox {
            background: %(surface);
            border: 1px solid %(border);
            border-radius: 4px;
            margin-top: 9px;
            padding-top: 6px;
            font-size: 11px;
            font-weight: 700;
            color: %(sectionText);
        }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }

        /* Zero-width handles remove the gutter while retaining a resize hit area. */
        QSplitter::handle {
            background: transparent;
            border: none;
            margin: 0;
        }
        QStatusBar { background: %(toolbar); color: %(mutedText); border-top: 1px solid %(border); }
        QStatusBar::item { border: none; }
        QProgressBar {
            border: 1px solid %(border);
            border-radius: 3px;
            background: %(surface);
            max-height: 10px;
        }
        QProgressBar::chunk { background: %(accent); border-radius: 3px; }
        QScrollBar:vertical {
            background: %(historyScrollTrack);
            border: none;
            border-left: 1px solid %(border);
            width: 16px;
            margin: 0;
        }
        QScrollBar:horizontal {
            background: %(historyScrollTrack);
            border: none;
            border-top: 1px solid %(border);
            height: 16px;
            margin: 0;
        }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
            background: %(border);
            border-radius: 0;
        }
        QScrollBar::handle:vertical { min-height: 24px; }
        QScrollBar::handle:horizontal { min-width: 24px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QToolTip { background: %(surface); color: %(text); border: 1px solid %(border); padding: 3px; }
        QMenu { background: %(surface); border: 1px solid %(border); padding: 4px; }
        QMenu::item { padding: 5px 22px 5px 22px; border-radius: 4px; }
        QMenu::item:selected { background: %(selection); color: %(selectionText); }
        QMenu#tabContextMenu {
            background: #F2F2F2;
            color: #202020;
            border: 1px solid #8F8F8F;
            padding: 2px;
        }
        QMenu#tabContextMenu::item {
            min-width: 220px;
            padding: 4px 32px;
            border-radius: 0;
        }
        QMenu#tabContextMenu::item:selected {
            background: #D7D7D7;
            color: #202020;
        }
        QMenu#tabContextMenu::item:disabled { color: #8A8A8A; }
        QMenu::separator { height: 1px; background: %(border); margin: 4px 8px; }
        QCheckBox, QRadioButton { spacing: 6px; }
        QDockWidget { titlebar-close-icon: none; color: %(mutedText); }
        QDockWidget::title {
            background: %(window);
            border-bottom: 1px solid %(border);
            padding: 5px 8px;
            text-align: left;
        }
        QDialog { background: %(window); }
        QDialog#aboutDialog { background: transparent; }
        QFrame#aboutWindow {
            background: %(surface);
            border: 1px solid %(border);
        }
        QWidget#aboutTitleBar { background: %(chrome); }
        QLabel#aboutTitleBarText {
            color: %(chromeText);
            font-family: "Arial";
            font-size: 12px;
            font-weight: 400;
        }
        QToolButton#aboutCloseButton {
            background: transparent;
            border: none;
            border-radius: 0;
            padding: 0;
        }
        QToolButton#aboutCloseButton:hover { background: #E81123; }
        QWidget#aboutContent, QWidget#aboutHero,
        QWidget#aboutApplicationInfo { background: %(surface); }
        QLabel#aboutTitle {
            color: %(text);
            font-family: "Arial";
            font-size: 20px;
            font-weight: 700;
        }
        QLabel#aboutMeta {
            color: %(text);
            font-family: "Arial";
            font-size: 11px;
            font-weight: 400;
        }
        QTextBrowser#aboutComponents {
            background: %(surface);
            color: %(text);
            border: 1px solid %(border);
            border-radius: 0;
            padding: 0;
        }
    )")
        .replace(QStringLiteral("%(window)"), name(p.window))
        .replace(QStringLiteral("%(chromeText)"), name(p.chromeText))
        .replace(QStringLiteral("%(chrome)"), name(p.chrome))
        .replace(QStringLiteral("%(tabInactive)"), name(p.tabInactive))
        .replace(QStringLiteral("%(sidebarSelection)"), name(p.sidebarSelection))
        .replace(QStringLiteral("%(sidebarText)"), name(sidebarText))
        .replace(QStringLiteral("%(historyControl)"), name(historyControl))
        .replace(QStringLiteral("%(historyControlBorder)"), name(historyControlBorder))
        .replace(QStringLiteral("%(historyScrollTrack)"), name(historyScrollTrack))
        .replace(QStringLiteral("%(historyHeaderText)"), name(historyHeaderText))
        .replace(QStringLiteral("%(windowFrameBorder)"), name(windowFrameBorder))
        .replace(QStringLiteral("%(toolbar)"), name(p.toolbar))
        .replace(QStringLiteral("%(sidebar)"), name(p.sidebar))
        .replace(QStringLiteral("%(surfaceAlternate)"), name(p.surfaceAlternate))
        .replace(QStringLiteral("%(surface)"), name(p.surface))
        .replace(QStringLiteral("%(border)"), name(p.border))
        .replace(QStringLiteral("%(text)"), name(p.text))
        .replace(QStringLiteral("%(mutedText)"), name(p.mutedText))
        .replace(QStringLiteral("%(sectionText)"), name(p.sectionText))
        .replace(QStringLiteral("%(accentHover)"), name(p.accentHover))
        .replace(QStringLiteral("%(accent)"), name(p.accent))
        .replace(QStringLiteral("%(selectionText)"), name(p.selectionText))
        .replace(QStringLiteral("%(selection)"), name(p.selection))
        .replace(QStringLiteral("%(hover)"), name(p.hover))
        .replace(QStringLiteral("%(danger)"), name(p.danger))
        .replace(QStringLiteral("%(removedBackground)"), name(p.removedBackground))
        .replace(QStringLiteral("%(removedText)"), name(p.removedText))
        .replace(QStringLiteral("%(hunkBackground)"), name(p.hunkBackground))
        .replace(QStringLiteral("%(hunkText)"), name(p.hunkText));
}
