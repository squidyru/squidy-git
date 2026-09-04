// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "core/gitclient.h"
#include "ui/mainwindow.h"
#include "ui/shellmetrics.h"
#include "ui/theme.h"

#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QScopeGuard>
#include <QLayout>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

class TestMainWindow final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void init() { Theme::instance()->applyToApplication(); }
    void opensOverviewFromSidebarAndClosesLastProject();
    void scrollsManyTabsWithoutOverlappingControls();
};

void TestMainWindow::opensOverviewFromSidebarAndClosesLastProject() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("SquidyGitTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SidebarNavigation"));
    QStringList paths;
    for (const QString &name : {QStringLiteral("one"), QStringLiteral("two")}) {
        const QString path = directory.filePath(name);
        QVERIFY(QDir().mkpath(path));
        QVERIFY(GitClient::initRepository(path, false).succeeded());
        paths.append(path);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("openTabs"), paths);
    settings.setValue(QStringLiteral("activeTab"), 1);
    settings.setValue(QStringLiteral("updates/automaticCheck"), false);
    settings.setValue(QStringLiteral("autoFetch/enabled"), false);
    settings.sync();

    MainWindow window;
    auto *tabs = window.findChild<QTabBar *>(QStringLiteral("repositoryTabBar"));
    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("repositoryPages"));
    auto *overview = window.findChild<QPushButton *>(QStringLiteral("repositoriesButton"));
    auto *add = window.findChild<QToolButton *>(QStringLiteral("addTabButton"));
    QVERIFY(tabs && pages && overview && add);
    QCOMPARE(tabs->count(), 3);
    QVERIFY(!tabs->isTabVisible(0));
    QCOMPARE(tabs->currentIndex(), 1);
    QCOMPARE(pages->currentIndex(), 1);
    QCOMPARE(add->size(), QSize(28, 28));
    QVERIFY(overview->geometry().right() < ShellMetrics::SidebarWidth);

    overview->click();
    QCOMPARE(tabs->currentIndex(), 1);
    QCOMPARE(pages->currentIndex(), 0);
    QVERIFY(tabs->property("overviewActive").toBool());
    auto *firstTitle = tabs->tabButton(1, QTabBar::LeftSide)
                           ->findChild<QLabel *>(QStringLiteral("repositoryTabTitle"));
    QVERIFY(firstTitle);
    QVERIFY(!firstTitle->property("selected").toBool());
    QVERIFY(tabs->isTabVisible(1));
    QVERIFY(tabs->isTabVisible(2));
    QVERIFY(overview->isHidden());

    // Clicking the last project again must work even though Qt's current
    // tab has not changed while the overview was visible.
    tabs->tabBarClicked(1);
    QCOMPARE(pages->currentIndex(), 1);
    QVERIFY(!tabs->property("overviewActive").toBool());
    QVERIFY(firstTitle->property("selected").toBool());
    overview->click();

    tabs->setCurrentIndex(2);
    QCOMPARE(pages->currentIndex(), 2);
    QVERIFY(!overview->isHidden());
    auto *close = tabs->tabButton(2, QTabBar::RightSide)
                      ->findChild<QToolButton *>(QStringLiteral("tabCloseButton"));
    QVERIFY(close);
    close->click();
    QCOMPARE(tabs->count(), 2);
    tabs->tabCloseRequested(1);
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(tabs->currentIndex(), 0);
    QCOMPARE(pages->currentIndex(), 0);
    QVERIFY(overview->isHidden());

    auto *list = pages->widget(0)->findChild<QListWidget *>();
    QVERIFY(list && list->count() > 0);
    list->itemDoubleClicked(list->item(0));
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->currentIndex(), 1);
    QCOMPARE(pages->currentIndex(), 1);
    QVERIFY(!overview->isHidden());

    // Closing an inactive tab from the overview should not leave the list.
    list->itemDoubleClicked(list->item(1));
    QCOMPARE(tabs->count(), 3);
    overview->click();
    tabs->tabCloseRequested(2);
    QCOMPARE(pages->currentIndex(), 0);
    QVERIFY(tabs->property("overviewActive").toBool());
    window.close();
    QCOMPARE(QSettings().value(QStringLiteral("activeTab")).toInt(), 0);
    MainWindow restored;
    auto *restoredTabs = restored.findChild<QTabBar *>(QStringLiteral("repositoryTabBar"));
    auto *restoredPages = restored.findChild<QStackedWidget *>(QStringLiteral("repositoryPages"));
    QCOMPARE(restoredPages->currentIndex(), 0);
    QCOMPARE(restoredTabs->currentIndex(), 1);
    QVERIFY(restoredTabs->property("overviewActive").toBool());
}

void TestMainWindow::scrollsManyTabsWithoutOverlappingControls() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("SquidyGitTests"));
    QCoreApplication::setApplicationName(QStringLiteral("TabOverflow"));
    QStringList paths;
    for (int i = 0; i < 10; ++i) {
        const QString path = directory.filePath(QStringLiteral("repository-with-long-name-%1").arg(i));
        QVERIFY(QDir().mkpath(path));
        QVERIFY(GitClient::initRepository(path, false).succeeded());
        QFile file(path + QStringLiteral("/README.md"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Example repository\n");
        paths.append(path);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("openTabs"), paths);
    settings.setValue(QStringLiteral("activeTab"), 1);
    settings.setValue(QStringLiteral("updates/automaticCheck"), false);
    settings.setValue(QStringLiteral("autoFetch/enabled"), false);
    settings.sync();
    MainWindow window;
    auto *tabs = window.findChild<QTabBar *>(QStringLiteral("repositoryTabBar"));
    QVERIFY(tabs);
    QWidget *strip = tabs->parentWidget();
    QWidget *originalParent = strip->parentWidget();
    // Render only the Qt strip; the native macOS window frame is not needed.
    QWidget preview;
    preview.setObjectName(QStringLiteral("tabPreview"));
    preview.setStyleSheet(QStringLiteral("QWidget#tabPreview { background: #1B405A; }"));
    const auto restoreParent = qScopeGuard([strip, originalParent] { strip->setParent(originalParent); });
    strip->setParent(&preview);
    preview.resize(1100, 80);
    strip->setGeometry(0, 0, 1100, 60);
    preview.show();
    strip->show();
    for (const int width : {1100, 900, 1400}) {
        preview.resize(width, 80);
        strip->resize(width, 60);
        // Activating a page recalculates the tab widths for the new strip.
        tabs->tabBarClicked(1);
        strip->layout()->activate();
        QCoreApplication::processEvents();
        QVERIFY(tabs->usesScrollButtons());
        auto *add = strip->findChild<QToolButton *>(QStringLiteral("addTabButton"));
        QVERIFY(add);
        const int addLeft = add->mapTo(strip, QPoint()).x();
        QCOMPARE(addLeft - tabs->geometry().right() - 1, ShellMetrics::AddTabButtonGap);
        QCOMPARE(add->mapTo(strip, add->rect().center()).y(),
                 tabs->mapTo(strip, tabs->tabRect(2).center()).y());
        for (const int index : {1, 5, 10}) {
            tabs->setCurrentIndex(index);
            QCoreApplication::processEvents();
            const QRect tab = tabs->tabRect(index);
            auto *toolbar = window.findChild<QWidget *>(QStringLiteral("mainToolbar"));
            QVERIFY(toolbar);
            QCOMPARE(toolbar->property("leadingTabJoined").toBool(),
                     tab.left() <= 0 && tab.right() - ShellMetrics::RepositoryTabGap >= 16);
            QVERIFY(tab.width() >= ShellMetrics::RepositoryTabMinimumWidth);
            QWidget *title = tabs->tabButton(index, QTabBar::LeftSide);
            QWidget *closeArea = tabs->tabButton(index, QTabBar::RightSide);
            auto *close = closeArea->findChild<QToolButton *>(QStringLiteral("tabCloseButton"));
            QVERIFY(title && close);
            QVERIFY(tab.contains(title->geometry()));
            const QRect closeRect(close->mapTo(tabs, QPoint()), close->size());
            QVERIFY(tab.adjusted(0, 0, -ShellMetrics::RepositoryTabGap, 0).contains(closeRect));
            QVERIFY(tab.right() - ShellMetrics::RepositoryTabGap - closeRect.right() >= 8);
            QVERIFY(!title->geometry().intersects(closeRect));
            QVERIFY(tabs->rect().contains(closeRect));
            QVERIFY(!title->findChild<QLabel *>(QStringLiteral("repositoryTabTitle"))->text().isEmpty());
        }
    }
    auto *hoveredClose = tabs->tabButton(9, QTabBar::RightSide)
                             ->findChild<QToolButton *>(QStringLiteral("tabCloseButton"));
    QTest::mouseMove(tabs, tabs->tabRect(9).center());
    QCoreApplication::processEvents();
    QVERIFY(hoveredClose->isVisible());
    const QRect hoverRect(hoveredClose->mapTo(tabs, QPoint()), hoveredClose->size());
    QVERIFY(tabs->tabRect(9).adjusted(0, 0, -ShellMetrics::RepositoryTabGap, 0).contains(hoverRect));
    QVERIFY(tabs->tabRect(9).right() - ShellMetrics::RepositoryTabGap - hoverRect.right() >= 8);
    QVERIFY(!tabs->tabButton(9, QTabBar::LeftSide)->geometry().intersects(hoverRect));
    QTest::mouseMove(hoveredClose, hoveredClose->rect().center());
    if (const QString screenshot = qEnvironmentVariable("SQUIDYGIT_TEST_SCREENSHOT"); !screenshot.isEmpty()) {
        preview.grab().save(screenshot);
    }
    // Closing the last visible project after scrolling keeps the next one reachable.
    tabs->tabCloseRequested(10);
    QCOMPARE(tabs->count(), 10);
    QVERIFY(tabs->currentIndex() > 0);
}

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
