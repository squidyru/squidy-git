// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QSettings>

namespace {

ThemePalette lightPalette() {
    ThemePalette palette;
    palette.window = QColor(QStringLiteral("#FFFFFF"));
    palette.chrome = QColor(QStringLiteral("#0B3A5B"));
    palette.chromeText = QColor(QStringLiteral("#EDF7FD"));
    palette.tabInactive = QColor(QStringLiteral("#245777"));
    palette.sidebarSelection = QColor(QStringLiteral("#347FCF"));
    palette.toolbar = QColor(QStringLiteral("#FCFDFE"));
    palette.sidebar = QColor(QStringLiteral("#123F5B"));
    palette.surface = QColor(QStringLiteral("#FFFFFF"));
    palette.surfaceAlternate = QColor(QStringLiteral("#FAFBFD"));
    palette.rowStripe = QColor(QStringLiteral("#F8FAFC"));
    palette.border = QColor(QStringLiteral("#E1E7ED"));
    palette.text = QColor(QStringLiteral("#202938"));
    palette.mutedText = QColor(QStringLiteral("#697386"));
    palette.sectionText = QColor(QStringLiteral("#748096"));
    palette.accent = QColor(QStringLiteral("#3478F6"));
    palette.accentHover = QColor(QStringLiteral("#4B8BFA"));
    palette.accentText = QColor(QStringLiteral("#FFFFFF"));
    palette.selection = QColor(QStringLiteral("#DDEAFF"));
    palette.selectionText = QColor(QStringLiteral("#172033"));
    palette.hover = QColor(QStringLiteral("#EDF3FA"));
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
    const QString interfaceFont =
        QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    const QColor sidebarText = mode_ == Mode::Light
                                   ? QColor(QStringLiteral("#EAF5FC"))
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
            color: %(text);
            font-size: 12px;
            font-family: %(interfaceFont);
        }
        QMainWindow::separator { background: %(border); width: 1px; height: 1px; }
        QToolBar#mainToolbar {
            background: %(toolbar);
            border: none;
            border-bottom: 1px solid %(border);
            border-top-left-radius: 16px;
            border-top-right-radius: 16px;
            spacing: 1px;
            padding: 7px 12px 6px 12px;
        }
        QToolBar#mainToolbar QToolButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 8px;
            color: %(text);
            font-family: %(interfaceFont);
            font-size: 11px;
            font-weight: 400;
            min-width: 68px;
            padding: 2px 6px;
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
            padding: 0 4px;
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
            margin: 6px 4px;
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
        QWidget#windowShadowFrame[nativeBackdrop="true"] {
            background: rgba(8, 39, 65, 110);
            border-color: rgba(255, 255, 255, 30);
        }
        QWidget#titleBar[nativeBackdrop="true"] {
            background: rgba(10, 48, 78, 126);
        }
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
            font-family: %(interfaceFont);
            font-size: 12px;
            font-weight: 400;
        }
        QMenuBar::item { padding: 4px 6px; background: transparent; color: %(chromeText); }
        QMenuBar::item:selected { background: rgba(255, 255, 255, 40); border-radius: 3px; }
        QMenuBar::item:pressed { background: rgba(255, 255, 255, 70); border-radius: 3px; }

        QWidget#workspace {
            background: transparent;
        }
        QWidget#repositoryTabStrip {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                        stop:0 rgba(12, 51, 82, 162),
                        stop:0.7 rgba(18, 67, 104, 150),
                        stop:1 rgba(8, 40, 72, 170));
        }
        QWidget#workspaceBody { background: transparent; }
        QStackedWidget#repositoryPages { background: transparent; }
        QWidget#repositoryView { background: transparent; }
        QWidget#repositoryContentShell { background: %(surface); }
        QStackedWidget#repositoryContentPages { background: %(surface); }
        QTabBar#repositoryTabBar { background: transparent; qproperty-drawBase: 0; }
        QTabBar#repositoryTabBar::tab {
            background: %(tabInactive);
            color: %(chromeText);
            border: 1px solid rgba(255, 255, 255, 16);
            border-radius: 11px;
            min-height: 42px;
            max-height: 42px;
            padding: 1px 12px 0 12px;
            margin: 0 2px 4px 0;
            min-width: 118px;
            max-width: 250px;
        }
        QTabBar#repositoryTabBar::tab:selected {
            background: %(toolbar);
            color: %(text);
            border: none;
            border-top-left-radius: 11px;
            border-top-right-radius: 11px;
            border-bottom-left-radius: 0;
            border-bottom-right-radius: 0;
            margin-bottom: 0;
            font-weight: 600;
        }
        QTabBar#repositoryTabBar::tab:hover:!selected { background: rgba(62, 124, 164, 210); }
        QTabBar#repositoryTabBar::close-button { subcontrol-position: right; }
        QLabel#repositoryTabTitle {
            background: transparent;
            color: %(chromeText);
            font-family: %(interfaceFont);
            font-size: 12px;
            font-weight: 400;
            padding: 0 2px 0 11px;
        }
        QLabel#repositoryTabTitle[selected="true"] { color: %(text); font-weight: 600; }
        QWidget#tabTitleArea QLabel#repositoryTabTitle { padding-left: 2px; }
        QToolButton#addTabButton {
            background: rgba(58, 116, 153, 145);
            border: none;
            border-radius: 10px;
            color: %(chromeText);
            padding: 0;
            margin: 0;
        }
        QToolButton#addTabButton:hover { background: rgba(255, 255, 255, 40); border-radius: 10px; }
        QToolButton#addTabButton:pressed { background: rgba(255, 255, 255, 70); }

        QPushButton#viewSwitchButton {
            background: transparent;
            border: none;
            border-radius: 8px;
            min-height: 32px;
            max-height: 32px;
            margin: 1px 0;
            padding: 0 10px 0 14px;
            text-align: left;
            color: %(sidebarText);
        }
        QPushButton#viewSwitchButton:hover { background: rgba(255, 255, 255, 22); }
        QPushButton#viewSwitchButton:checked {
            background: %(sidebarSelection);
            color: #FFFFFF;
            font-weight: 600;
        }

        /* Checkboxes are intentionally left unstyled: touching them in a
           stylesheet drops the native indicator glyph. */
        QLabel, QGroupBox::title, QSplitter { background: transparent; }
        QWidget#stateBanner {
            background: %(removedBackground);
            border-bottom: 1px solid %(border);
        }
        QLabel#stateBadge { color: %(removedText); font-weight: 400; }
        QWidget#viewSwitcher { background: transparent; border: none; }
        QWidget#workspaceHeader { min-height: 30px; background: transparent; }
        QLabel#workspaceTitle { min-height: 30px; color: #86AFCB; font-size: 11px; font-weight: 700; }
        QFrame#sidebarTopSeparator {
            border: none;
            border-top: 1px solid rgba(255, 255, 255, 48);
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
            background: qlineargradient(x1:0, y1:0, x2:0.9, y2:1,
                        stop:0 rgba(20, 72, 108, 94),
                        stop:0.62 rgba(12, 55, 86, 78),
                        stop:1 rgba(8, 43, 70, 100));
            border: none;
            border-right: 1px solid rgba(255, 255, 255, 22);
        }
        QTreeWidget#navigationTree {
            background: transparent;
            border: none;
            outline: none;
            color: %(sidebarText);
            font-family: %(interfaceFont);
            font-size: 12px;
            font-weight: 400;
        }
        QTreeWidget#navigationTree::item {
            border-radius: 7px;
            padding: 1px 2px;
        }
        QTreeWidget#navigationTree::item:hover {
            border-radius: 7px;
            padding: 1px 2px;
            background: rgba(255, 255, 255, 18);
        }
        QTreeWidget#navigationTree::item:selected {
            border-radius: 7px;
            padding: 1px 2px;
            background: %(sidebarSelection);
            color: #FFFFFF;
        }
        QLineEdit#navigationFilter {
            min-height: 32px;
            max-height: 32px;
            margin: 5px 0;
            padding: 0 8px;
            font-size: 12px;
            color: %(sidebarText);
            background: rgba(3, 29, 45, 66);
            border: 1px solid rgba(255, 255, 255, 38);
            border-radius: 7px;
        }
        QLineEdit#navigationFilter:focus { border-color: rgba(123, 184, 225, 170); }
        QWidget#repositorySummaryCard {
            background: rgba(255, 255, 255, 20);
            border: 1px solid rgba(255, 255, 255, 50);
            border-radius: 14px;
        }
        QLabel#repositorySummaryName { color: #FFFFFF; font-size: 12px; font-weight: 700; }
        QLabel#repositorySummaryBranch,
        QLabel#repositorySummarySync,
        QLabel#repositorySummaryState { color: #C6DEEC; font-size: 11px; }
        QFrame#repositoryCardSeparator { border: none; border-top: 1px solid rgba(255, 255, 255, 28); }

        QWidget#workspaceStatusBar {
            background: %(toolbar);
            border-top: 1px solid %(border);
            border-bottom-right-radius: 16px;
        }
        QLabel#workspaceStatusText {
            color: %(mutedText);
            font-size: 11px;
            background: transparent;
        }

        QWidget#historyToolbar {
            background: %(surface);
            border-bottom: 1px solid %(border);
        }
        QComboBox#historyScope, QComboBox#historyOrder,
        QLineEdit#historyAuthorFilter, QToolButton#historyJumpButton {
            min-height: 30px;
            max-height: 30px;
            padding: 0 10px;
            font-family: %(interfaceFont);
            font-size: 12px;
            border-radius: 8px;
        }
        QComboBox#historyScope, QComboBox#historyOrder {
            background: %(surfaceAlternate);
            border: 1px solid %(border);
        }
        QComboBox#historyScope::down-arrow,
        QComboBox#historyOrder::down-arrow {
            image: none;
            width: 0;
            height: 0;
        }
        QLineEdit#historyAuthorFilter {
            padding-right: 20px;
            background: %(surface);
        }
        QToolButton#historyJumpButton {
            background: %(surfaceAlternate);
            color: %(text);
            border: 1px solid %(border);
            border-radius: 8px;
            padding: 0;
        }
        QToolButton#historyJumpButton:hover,
        QToolButton#historyJumpButton:focus {
            background: %(hover);
            border-color: %(accent);
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
            padding: 6px 8px;
            border-radius: 5px;
        }
        QMenu#historyJumpMenu::separator {
            margin: 2px 0;
        }
        QWidget#historyPage QCheckBox, QWidget#historyPage QLabel,
        QWidget#filesPage QCheckBox, QWidget#filesPage QLabel {
            font-family: %(interfaceFont);
            font-size: 12px;
        }

        QWidget#fileStatusPage,
        QWidget#fileStatusToolbar,
        QWidget#fileChangesPanel,
        QWidget#workingTreeDiffPanel,
        QWidget#commitPanel {
            background: %(surface);
        }
        QLineEdit#fileStatusFilter {
            min-height: 30px;
            max-height: 30px;
            padding: 0 10px;
            border-radius: 8px;
        }
        QToolButton#treeModeButton {
            min-height: 30px;
            max-height: 30px;
            padding: 0 12px;
            background: %(surfaceAlternate);
            color: %(text);
            border: 1px solid %(border);
            border-radius: 8px;
        }
        QToolButton#treeModeButton:hover { background: %(hover); }
        QToolButton#treeModeButton:checked {
            background: %(selection);
            border-color: %(accent);
        }
        QPushButton#filePanelAction {
            min-height: 28px;
            max-height: 28px;
            padding: 0 11px;
            border-radius: 7px;
        }
        QTreeWidget#unstagedTree, QTreeWidget#stagedTree,
        QPlainTextEdit#commitMessage {
            border-radius: 8px;
        }

        QLineEdit, QComboBox, QPlainTextEdit, QTextEdit, QSpinBox {
            background: %(surface);
            color: %(text);
            border: 1px solid %(border);
            border-radius: 7px;
            padding: 6px 9px;
            selection-background-color: %(selection);
            selection-color: %(selectionText);
        }
        QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus {
            border-color: %(accent);
        }
        QTextBrowser#commitDetails {
            padding: 3px;
            font-family: %(interfaceFont);
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
            font-family: %(interfaceFont);
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
            font-family: %(interfaceFont);
            font-size: 12px;
        }
        QWidget#sideDiffPanel { background: %(surface); }
        QLabel#sideDiffHeader {
            background: %(surfaceAlternate);
            color: %(sectionText);
            border: 1px solid %(border);
            border-bottom: none;
            padding: 0 8px;
            font-family: %(interfaceFont);
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
            border-radius: 0;
            outline: none;
            selection-background-color: %(selection);
            selection-color: %(selectionText);
        }
        /* Every state repeats the metrics. A state rule that names only its
           colours leaves Qt to lay the row out with the metrics of the native
           style instead, and the content jumps as the pointer passes over. */
        QTreeWidget::item, QListWidget::item { min-height: 22px; padding: 0 5px; }
        QTreeWidget::item:hover, QListWidget::item:hover {
            min-height: 22px;
            padding: 0 5px;
            background: %(hover);
        }
        QTreeWidget::item:selected, QListWidget::item:selected {
            min-height: 22px;
            padding: 0 5px;
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
            font-family: %(interfaceFont);
            font-size: 12px;
        }
        QTreeView#historyTree::item, QTreeView#fileTimeline::item, QTreeView#fileTree::item {
            min-height: 23px;
            max-height: 23px;
            padding: 0 5px;
        }
        QTreeView#historyTree QHeaderView::section,
        QTreeView#fileTimeline QHeaderView::section,
        QTreeView#fileTree QHeaderView::section {
            background: %(surface);
            color: %(historyHeaderText);
            border: none;
            border-right: 1px solid %(border);
            border-bottom: 1px solid %(border);
            min-height: 25px;
            max-height: 25px;
            padding: 0 8px;
            font-family: %(interfaceFont);
            font-size: 12px;
            font-weight: 400;
        }
        QTreeView#historyTree QScrollBar:vertical,
        QTreeView#fileTimeline QScrollBar:vertical,
        QTreeView#fileTree QScrollBar:vertical {
            background: %(historyScrollTrack);
            border: none;
            width: 10px;
        }
        QTreeView#historyTree QScrollBar:horizontal,
        QTreeView#fileTimeline QScrollBar:horizontal,
        QTreeView#fileTree QScrollBar:horizontal {
            background: %(historyScrollTrack);
            border: none;
            height: 10px;
        }
        QTreeView#historyTree QScrollBar::handle:vertical,
        QTreeView#historyTree QScrollBar::handle:horizontal,
        QTreeView#fileTimeline QScrollBar::handle:vertical,
        QTreeView#fileTimeline QScrollBar::handle:horizontal,
        QTreeView#fileTree QScrollBar::handle:vertical,
        QTreeView#fileTree QScrollBar::handle:horizontal {
            background: #C5CED8;
            border-radius: 5px;
            margin: 2px;
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
            border-radius: 7px;
            min-height: 30px;
            padding: 0 12px;
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

        QSplitter::handle {
            background: transparent;
            border: none;
            margin: 0;
        }
        QSplitter#fileStatusVerticalSplitter::handle:vertical,
        QSplitter#fileListsSplitter::handle:vertical,
        QSplitter#historyVerticalSplitter::handle:vertical,
        QSplitter#historyLeftSplitter::handle:vertical {
            background: %(border);
            margin: 1px 0;
        }
        QSplitter#fileStatusTopSplitter::handle:horizontal,
        QSplitter#historyDetailsSplitter::handle:horizontal {
            background: %(border);
            margin: 0 1px;
        }
        QSplitter#workspaceSplitter::handle:horizontal {
            background: rgba(255, 255, 255, 24);
        }
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
            width: 10px;
            margin: 0;
        }
        QScrollBar:horizontal {
            background: %(historyScrollTrack);
            border: none;
            height: 10px;
            margin: 0;
        }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
            background: #C5CED8;
            border-radius: 5px;
            margin: 2px;
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
            font-family: %(interfaceFont);
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
            font-family: %(interfaceFont);
            font-size: 20px;
            font-weight: 700;
        }
        QLabel#aboutMeta {
            color: %(text);
            font-family: %(interfaceFont);
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
        .replace(QStringLiteral("%(interfaceFont)"), interfaceFont)
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
