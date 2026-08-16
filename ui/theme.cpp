// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>

namespace {

ThemePalette lightPalette() {
    ThemePalette palette;
    palette.window = QColor(QStringLiteral("#F4F6F9"));
    palette.chrome = QColor(QStringLiteral("#10162E"));
    palette.chromeText = QColor(QStringLiteral("#E8ECF6"));
    palette.tabInactive = QColor(QStringLiteral("#E4E8F0"));
    palette.sidebarSelection = QColor(QStringLiteral("#3A4C82"));
    palette.toolbar = QColor(QStringLiteral("#F4F6F9"));
    palette.sidebar = QColor(QStringLiteral("#F4F6F9"));
    palette.surface = QColor(QStringLiteral("#FFFFFF"));
    palette.surfaceAlternate = QColor(QStringLiteral("#FAFBFD"));
    palette.rowStripe = QColor(QStringLiteral("#F2F5FA"));
    palette.border = QColor(QStringLiteral("#CDD4E0"));
    palette.text = QColor(QStringLiteral("#1E2333"));
    palette.mutedText = QColor(QStringLiteral("#6E7488"));
    palette.sectionText = QColor(QStringLiteral("#6E7488"));
    palette.accent = QColor(QStringLiteral("#D02B44"));
    palette.accentHover = QColor(QStringLiteral("#E8455C"));
    palette.accentText = QColor(QStringLiteral("#FFFFFF"));
    palette.selection = QColor(QStringLiteral("#2C3A66"));
    palette.selectionText = QColor(QStringLiteral("#FFFFFF"));
    palette.hover = QColor(QStringLiteral("#E9EDF4"));
    palette.success = QColor(QStringLiteral("#1A7F4B"));
    palette.warning = QColor(QStringLiteral("#B0730B"));
    palette.danger = QColor(QStringLiteral("#9B1B2E"));
    palette.staged = QColor(QStringLiteral("#1A7F4B"));
    palette.untracked = QColor(QStringLiteral("#6D5BB5"));
    palette.addedBackground = QColor(QStringLiteral("#E8F6EC"));
    palette.addedText = QColor(QStringLiteral("#14663C"));
    palette.removedBackground = QColor(QStringLiteral("#FBE9EA"));
    palette.removedText = QColor(QStringLiteral("#9B1B2E"));
    palette.hunkBackground = QColor(QStringLiteral("#E7ECF7"));
    palette.hunkText = QColor(QStringLiteral("#22305C"));
    palette.graphNodeBorder = QColor(QStringLiteral("#FFFFFF"));
    palette.syntaxKeyword = QColor(QStringLiteral("#8A3FA0"));
    palette.syntaxType = QColor(QStringLiteral("#0E7490"));
    palette.syntaxString = QColor(QStringLiteral("#B0433A"));
    palette.syntaxNumber = QColor(QStringLiteral("#2A5DB0"));
    palette.syntaxComment = QColor(QStringLiteral("#6E7488"));
    palette.syntaxMeta = QColor(QStringLiteral("#A2680F"));
    palette.laneColors = {
        QColor(QStringLiteral("#3A4C82")),
        QColor(QStringLiteral("#D02B44")),
        QColor(QStringLiteral("#1A7F4B")),
        QColor(QStringLiteral("#B0730B")),
        QColor(QStringLiteral("#6D5BB5")),
        QColor(QStringLiteral("#0E7490")),
        QColor(QStringLiteral("#B5468B"))
    };
    return palette;
}

ThemePalette darkPalette() {
    ThemePalette palette;
    palette.window = QColor(QStringLiteral("#0F1527"));
    palette.chrome = QColor(QStringLiteral("#080D1C"));
    palette.chromeText = QColor(QStringLiteral("#DDE4F2"));
    palette.tabInactive = QColor(QStringLiteral("#151D31"));
    palette.sidebarSelection = QColor(QStringLiteral("#33427A"));
    palette.toolbar = QColor(QStringLiteral("#0F1527"));
    palette.sidebar = QColor(QStringLiteral("#0D1322"));
    palette.surface = QColor(QStringLiteral("#0A0F1E"));
    palette.surfaceAlternate = QColor(QStringLiteral("#101728"));
    palette.rowStripe = QColor(QStringLiteral("#131B2E"));
    palette.border = QColor(QStringLiteral("#232C45"));
    palette.text = QColor(QStringLiteral("#DDE4F2"));
    palette.mutedText = QColor(QStringLiteral("#8794AC"));
    palette.sectionText = QColor(QStringLiteral("#74829C"));
    palette.accent = QColor(QStringLiteral("#FF6E7C"));
    palette.accentHover = QColor(QStringLiteral("#FF8A95"));
    // Coral is light enough that white on it stops being readable.
    palette.accentText = QColor(QStringLiteral("#1A1020"));
    palette.selection = QColor(QStringLiteral("#2A3A6B"));
    palette.selectionText = QColor(QStringLiteral("#FFFFFF"));
    palette.hover = QColor(QStringLiteral("#172034"));
    palette.success = QColor(QStringLiteral("#46C88A"));
    palette.warning = QColor(QStringLiteral("#E0A33A"));
    palette.danger = QColor(QStringLiteral("#F04438"));
    palette.staged = QColor(QStringLiteral("#46C88A"));
    palette.untracked = QColor(QStringLiteral("#B08CFF"));
    palette.addedBackground = QColor(QStringLiteral("#0E2A22"));
    palette.addedText = QColor(QStringLiteral("#6FDCA4"));
    palette.removedBackground = QColor(QStringLiteral("#2C1420"));
    palette.removedText = QColor(QStringLiteral("#FF9AA2"));
    palette.hunkBackground = QColor(QStringLiteral("#121B33"));
    palette.hunkText = QColor(QStringLiteral("#8FA6E8"));
    palette.graphNodeBorder = QColor(QStringLiteral("#0A0F1E"));
    palette.syntaxKeyword = QColor(QStringLiteral("#C792EA"));
    palette.syntaxType = QColor(QStringLiteral("#4EC9B0"));
    palette.syntaxString = QColor(QStringLiteral("#CE9178"));
    palette.syntaxNumber = QColor(QStringLiteral("#B5CEA8"));
    palette.syntaxComment = QColor(QStringLiteral("#7A87A0"));
    palette.syntaxMeta = QColor(QStringLiteral("#D7BA7D"));
    palette.laneColors = {
        QColor(QStringLiteral("#7B92E8")),
        QColor(QStringLiteral("#FF6E7C")),
        QColor(QStringLiteral("#46C88A")),
        QColor(QStringLiteral("#E0A33A")),
        QColor(QStringLiteral("#B08CFF")),
        QColor(QStringLiteral("#4CC5D6")),
        QColor(QStringLiteral("#F07FC0"))
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
                                   ? QColor(QStringLiteral("#3A4257"))
                                   : p.text;
    const QColor historyControl = mode_ == Mode::Light
                                      ? QColor(QStringLiteral("#E8EBF1"))
                                      : p.surfaceAlternate;
    const QColor historyControlBorder = mode_ == Mode::Light
                                            ? QColor(QStringLiteral("#A9B2C4"))
                                            : p.border;
    const QColor historyScrollTrack = mode_ == Mode::Light
                                          ? QColor(QStringLiteral("#EDF0F5"))
                                          : p.window;
    const QColor historyHeaderText = mode_ == Mode::Light
                                         ? QColor(QStringLiteral("#12182A"))
                                         : p.text;
    const QColor windowFrameBorder(
        (p.border.red() + p.chrome.red()) / 2,
        (p.border.green() + p.chrome.green()) / 2,
        (p.border.blue() + p.chrome.blue()) / 2);
    return QStringLiteral(R"(
        QWidget {
            background: %(window);
            color: %(text);
            font-size: 12px;
            font-family: "Noto Sans";
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
            color: %(text);
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
        QLabel#commitBadge, QLabel#pushBadge, QLabel#pullBadge,
        QLabel#tabCommitBadge, QLabel#tabPushBadge, QLabel#tabPullBadge {
            border-radius: 4px;
            font-size: 10px;
            font-weight: 800;
        }
        QLabel#tabCommitBadge, QLabel#tabPushBadge, QLabel#tabPullBadge {
            padding: 0 6px;
        }
        QLabel#commitBadge, QLabel#tabCommitBadge {
            background: %(accent);
            color: %(accentText);
        }
        QLabel#pushBadge, QLabel#tabPushBadge {
            background: %(selection);
            color: %(selectionText);
        }
        QLabel#pullBadge, QLabel#tabPullBadge {
            background: %(success);
            color: %(surface);
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
            padding: 0;
            spacing: 3px;
            font-family: "Arial";
            font-size: 12px;
            font-weight: 400;
        }
        QMenuBar::item { padding: 4px 6px; background: transparent; color: %(chromeText); }
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
            border-bottom: 1px solid %(border);
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
            color: %(text);
            font-family: "Arial";
            font-size: 12px;
            font-weight: 400;
            padding: 0 2px 0 11px;
        }
        QWidget#tabTitleArea QLabel#repositoryTabTitle { padding-left: 2px; }
        QToolButton#addTabButton {
            background: transparent;
            border: none;
            color: %(chromeText);
            min-width: 28px;
            max-width: 28px;
            padding: 0;
            margin: 0;
        }
        QToolButton#addTabButton:hover { background: rgba(255, 255, 255, 40); border-radius: 0; }
        QToolButton#addTabButton:pressed { background: rgba(255, 255, 255, 70); }

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
            color: %(selectionText);
            font-weight: 400;
        }

        /* Checkboxes are intentionally left unstyled: touching them in a
           stylesheet drops the native indicator glyph. */
        QLabel, QGroupBox::title, QSplitter { background: transparent; }
        QWidget#stateBanner {
            background: %(removedBackground);
            border-bottom: 1px solid %(border);
        }
        QLabel#stateBadge { color: %(removedText); font-weight: 400; }
        QWidget#viewSwitcher { background: %(window); border: none; }
        QWidget#workspaceHeader { min-height: 30px; background: transparent; }
        QLabel#workspaceTitle { min-height: 30px; color: %(sectionText); font-size: 12px; font-weight: 600; }
        QFrame#sidebarTopSeparator {
            border: none;
            border-top: 1px solid %(border);
            margin: 0;
        }
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
        QTreeWidget#navigationTree::item:hover {
            border-radius: 0;
            padding: 0;
            background: %(hover);
        }
        QTreeWidget#navigationTree::item:selected {
            border-radius: 0;
            padding: 0;
            background: %(sidebarSelection);
            color: %(selectionText);
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
        QWidget#historyPage QCheckBox, QWidget#historyPage QLabel,
        QWidget#filesPage QCheckBox, QWidget#filesPage QLabel {
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
        QTextBrowser#commitDetails {
            padding: 3px;
            font-family: "Noto Sans";
            font-size: 12px;
        }
        QPlainTextEdit#diffView, QPlainTextEdit#fileContents { padding: 0; }
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
            alternate-background-color: %(rowStripe);
            color: %(text);
            border: 1px solid %(border);
            border-radius: 1px;
            outline: none;
            selection-background-color: %(selection);
            selection-color: %(selectionText);
        }
        /* Every state repeats the metrics. A state rule that names only its
           colours leaves Qt to lay the row out with the metrics of the native
           style instead, and the content jumps as the pointer passes over. */
        QTreeWidget::item, QListWidget::item { min-height: 18px; padding: 0 3px; }
        QTreeWidget::item:hover, QListWidget::item:hover {
            min-height: 18px;
            padding: 0 3px;
            background: %(hover);
        }
        QTreeWidget::item:selected, QListWidget::item:selected {
            min-height: 18px;
            padding: 0 3px;
            background: %(selection);
            color: %(selectionText);
        }
        /* The long lists stripe their rows. The branch column of the file tree
           is left unstyled: a rule there would replace the expander marks Qt
           draws by default. */
        QTreeView#historyTree, QTreeView#fileTimeline, QTreeView#fileTree {
            background: %(surface);
            alternate-background-color: %(rowStripe);
            color: %(text);
            font-family: "Noto Sans";
            font-size: 12px;
        }
        QTreeView#historyTree::item, QTreeView#fileTimeline::item, QTreeView#fileTree::item {
            min-height: 19px;
            max-height: 19px;
            padding: 0 3px;
        }
        QTreeView#historyTree QHeaderView::section,
        QTreeView#fileTimeline QHeaderView::section,
        QTreeView#fileTree QHeaderView::section {
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
        QTreeView#historyTree QScrollBar:vertical,
        QTreeView#fileTimeline QScrollBar:vertical,
        QTreeView#fileTree QScrollBar:vertical {
            background: %(historyScrollTrack);
            border-left: 1px solid %(border);
            width: 14px;
        }
        QTreeView#historyTree QScrollBar:horizontal,
        QTreeView#fileTimeline QScrollBar:horizontal,
        QTreeView#fileTree QScrollBar:horizontal {
            background: %(historyScrollTrack);
            border-top: 1px solid %(border);
            height: 14px;
        }
        QTreeView#historyTree QScrollBar::handle:vertical,
        QTreeView#historyTree QScrollBar::handle:horizontal,
        QTreeView#fileTimeline QScrollBar::handle:vertical,
        QTreeView#fileTimeline QScrollBar::handle:horizontal,
        QTreeView#fileTree QScrollBar::handle:vertical,
        QTreeView#fileTree QScrollBar::handle:horizontal {
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
            color: %(accentText);
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

        /* Handles take no room, so the panes meet flush. Nothing may be
           dragged shut against them: see RepositorySplitterWidth. */
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
            width: 14px;
            margin: 0;
        }
        QScrollBar:horizontal {
            background: %(historyScrollTrack);
            border: none;
            border-top: 1px solid %(border);
            height: 14px;
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
            background: %(surfaceAlternate);
            color: %(text);
            border: 1px solid %(border);
            padding: 2px;
        }
        QMenu#tabContextMenu::item {
            min-width: 220px;
            padding: 4px 32px;
            border-radius: 0;
        }
        QMenu#tabContextMenu::item:selected {
            background: %(hover);
            color: %(text);
        }
        QMenu#tabContextMenu::item:disabled { color: %(mutedText); }
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
        .replace(QStringLiteral("%(rowStripe)"), name(p.rowStripe))
        .replace(QStringLiteral("%(surface)"), name(p.surface))
        .replace(QStringLiteral("%(border)"), name(p.border))
        .replace(QStringLiteral("%(text)"), name(p.text))
        .replace(QStringLiteral("%(mutedText)"), name(p.mutedText))
        .replace(QStringLiteral("%(sectionText)"), name(p.sectionText))
        .replace(QStringLiteral("%(accentHover)"), name(p.accentHover))
        .replace(QStringLiteral("%(accentText)"), name(p.accentText))
        .replace(QStringLiteral("%(accent)"), name(p.accent))
        .replace(QStringLiteral("%(selectionText)"), name(p.selectionText))
        .replace(QStringLiteral("%(selection)"), name(p.selection))
        .replace(QStringLiteral("%(hover)"), name(p.hover))
        .replace(QStringLiteral("%(success)"), name(p.success))
        .replace(QStringLiteral("%(danger)"), name(p.danger))
        .replace(QStringLiteral("%(removedBackground)"), name(p.removedBackground))
        .replace(QStringLiteral("%(removedText)"), name(p.removedText))
        .replace(QStringLiteral("%(hunkBackground)"), name(p.hunkBackground))
        .replace(QStringLiteral("%(hunkText)"), name(p.hunkText));
}
