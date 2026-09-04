// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "ui/theme.h"
#include "ui/shellmetrics.h"
#include "ui/repositorytabstrip.h"

#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QTest>
#include <QStyle>
#include <QToolBar>
#include <QWidget>

class TestWindowBackground final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void sharesOneTintAcrossChromeAndSidebar();
    void exposesGlassAtAllContentCorners();
    void joinsOnlyTheActiveTabToContent();
    void separatesTabsAndCentersInactiveBackground();
    void repositoryButtonHasRestingFillAndBlueHover();
    void updatesContentCornerWhenScrollingPastActiveTab();
};

void TestWindowBackground::sharesOneTintAcrossChromeAndSidebar() {
    Theme::instance()->applyToApplication();
    QWidget window;
    window.setAttribute(Qt::WA_TranslucentBackground);
    QWidget workspace(&window);
    workspace.setObjectName(QStringLiteral("workspace"));
    workspace.setAttribute(Qt::WA_StyledBackground);

    QWidget tabs(&workspace);
    tabs.setObjectName(QStringLiteral("repositoryTabStrip"));
    QWidget body(&workspace);
    body.setObjectName(QStringLiteral("workspaceBody"));
    QWidget repository(&body);
    repository.setObjectName(QStringLiteral("repositoryView"));
    QWidget sidebar(&repository);
    sidebar.setObjectName(QStringLiteral("sidebar"));

    for (const QSize size : {QSize(600, 400), QSize(800, 620)}) {
        window.resize(size);
        workspace.setGeometry(window.rect());
        tabs.setGeometry(0, 0, size.width(), 60);
        body.setGeometry(0, 60, size.width(), size.height() - 60);
        repository.setGeometry(body.rect());
        sidebar.setGeometry(0, 0, 242, body.height());
        window.show();

        const QImage image = window.grab().toImage();
        const qreal scale = image.devicePixelRatio();
        const auto pixel = [&image, scale](int x, int y) {
            return image.pixelColor(qRound(x * scale), qRound(y * scale));
        };
        const QColor tint = pixel(20, 20);
        QVERIFY(tint.alpha() > 0 && tint.alpha() < 255);
        QCOMPARE(pixel(20, 59), tint);
        QCOMPARE(pixel(20, 60), tint);
        QCOMPARE(pixel(20, size.height() / 2), tint);
        QCOMPARE(pixel(20, size.height() - 2), tint);
        QCOMPARE(pixel(size.width() - 20, size.height() - 2), tint);
    }
}

void TestWindowBackground::exposesGlassAtAllContentCorners() {
    Theme::instance()->applyToApplication();
    QWidget window;
    window.setAttribute(Qt::WA_TranslucentBackground);
    QWidget workspace(&window);
    workspace.setObjectName(QStringLiteral("workspace"));
    QWidget content(&workspace);
    content.setObjectName(QStringLiteral("repositoryContentShell"));
    QWidget pages(&content);
    pages.setObjectName(QStringLiteral("repositoryContentPages"));
    QToolBar toolbar(&workspace);
    toolbar.setObjectName(QStringLiteral("mainToolbar"));
    QWidget footer(&workspace);
    footer.setObjectName(QStringLiteral("workspaceStatusBar"));

    for (const QSize size : {QSize(800, 600), QSize(1100, 800)}) {
        window.resize(size);
        workspace.setGeometry(window.rect());
        const QRect area(ShellMetrics::SidebarWidth, 60,
                         size.width() - ShellMetrics::SidebarWidth, size.height() - 60);
        content.setGeometry(area);
        pages.setGeometry(0, ShellMetrics::ToolbarHeight, area.width(),
                          area.height() - ShellMetrics::ToolbarHeight - ShellMetrics::FooterHeight);
        toolbar.setGeometry(area.x(), area.y(), area.width(), ShellMetrics::ToolbarHeight);
        footer.setGeometry(area.x(), size.height() - ShellMetrics::FooterHeight,
                           area.width(), ShellMetrics::FooterHeight);
        window.show();

        const QImage image = window.grab().toImage();
        const qreal scale = image.devicePixelRatio();
        const auto pixel = [&image, scale](const QPoint &point) {
            return image.pixelColor(qRound(point.x() * scale), qRound(point.y() * scale));
        };
        const QColor glass = pixel(QPoint(20, 20));
        for (const QPoint corner : {area.topLeft() + QPoint(1, 1),
                                     area.topRight() + QPoint(-1, 1),
                                     area.bottomLeft() + QPoint(1, -1),
                                     area.bottomRight() - QPoint(1, 1)}) {
            QCOMPARE(pixel(corner), glass);
        }
        QCOMPARE(pixel(area.center()).alpha(), 255);
        QCOMPARE(pixel(QPoint(area.left(), area.center().y())).alpha(), 255);
    }
}

void TestWindowBackground::joinsOnlyTheActiveTabToContent() {
    Theme::instance()->applyToApplication();
    QWidget window;
    window.setAttribute(Qt::WA_TranslucentBackground);
    window.resize(800, 60);
    RepositoryTabStrip strip(&window);
    strip.setObjectName(QStringLiteral("repositoryTabStrip"));
    strip.setGeometry(window.rect());
    RepositoryTabBar tabs(&strip);
    tabs.setObjectName(QStringLiteral("repositoryTabBar"));
    tabs.setExpanding(false);
    tabs.setMovable(true);
    tabs.addTab(QStringLiteral("Repositories"));
    tabs.addTab(QStringLiteral("project-one"));
    tabs.addTab(QStringLiteral("project-two"));
    tabs.setTabVisible(0, false);
    tabs.setCurrentIndex(0);
    QCOMPARE(tabs.currentIndex(), 0);
    tabs.setGeometry(20, 14, 740, 46);
    strip.setTabBar(&tabs);
    window.show();

    for (const int active : {1, 2}) {
        QTest::mouseClick(&tabs, Qt::LeftButton, Qt::NoModifier,
                          tabs.tabRect(active).center());
        QCOMPARE(tabs.currentIndex(), active);
        const QRect tab = tabs.tabRect(active).translated(tabs.pos());
        const QImage image = window.grab().toImage();
        const qreal scale = image.devicePixelRatio();
        const auto pixel = [&image, scale](int x, int y) {
            return image.pixelColor(qRound(x * scale), qRound(y * scale));
        };
        const int right = tab.right() - ShellMetrics::RepositoryTabGap;
        QCOMPARE(pixel(right + 2, tab.bottom() - 1),
                 Theme::instance()->palette().toolbar);
        QVERIFY(pixel(right + 8, tab.bottom() - 8).alpha() < 255);
        for (int index = 1; index < tabs.count(); ++index) {
            if (index != active) {
                const QRect inactive = tabs.tabRect(index).translated(tabs.pos());
                QCOMPARE(pixel(inactive.center().x(), inactive.bottom() - 1).alpha(), 0);
            }
        }
    }
    tabs.setCurrentIndex(1);
    tabs.setProperty("overviewActive", true);
    tabs.style()->unpolish(&tabs);
    tabs.style()->polish(&tabs);
    const QImage overview = window.grab().toImage();
    const qreal overviewScale = overview.devicePixelRatio();
    for (int index = 1; index < tabs.count(); ++index) {
        const QRect tab = tabs.tabRect(index).translated(tabs.pos());
        QCOMPARE(overview.pixelColor(qRound(tab.center().x() * overviewScale),
                                     qRound((tab.bottom() - 1) * overviewScale)).alpha(), 0);
        QCOMPARE(overview.pixelColor(qRound((tab.right() + 2) * overviewScale),
                                     qRound((tab.bottom() - 1) * overviewScale)).alpha(), 0);
        // No opaque fragment of the hidden overview tab may cover a project.
        QVERIFY(overview.pixelColor(qRound((tab.left() + 15) * overviewScale),
                                    qRound((tab.top() + 10) * overviewScale)).alpha() < 255);
    }
    tabs.setProperty("overviewActive", false);
    tabs.style()->unpolish(&tabs);
    tabs.style()->polish(&tabs);
    window.resize(800, 200);
    QWidget content(&window);
    content.setObjectName(QStringLiteral("repositoryContentShell"));
    const int contentLeft = tabs.x() + tabs.tabRect(1).left();
    content.setGeometry(contentLeft, 60, window.width() - contentLeft, 140);
    QToolBar toolbar(&window);
    toolbar.setObjectName(QStringLiteral("mainToolbar"));
    toolbar.setGeometry(contentLeft, 60, content.width(), ShellMetrics::ToolbarHeight);
    content.show();
    toolbar.show();
    for (const int active : {1, 2, 1}) {
        tabs.setCurrentIndex(active);
        const bool joined = active == 1;
        strip.setLeadingTabJoined(joined);
        for (QWidget *surface : {&content, static_cast<QWidget *>(&toolbar)}) {
            surface->setProperty("leadingTabJoined", joined);
            surface->style()->unpolish(surface);
            surface->style()->polish(surface);
        }
        const QImage image = window.grab().toImage();
        const qreal scale = image.devicePixelRatio();
        for (int y = 58; y <= 62; ++y) {
            QCOMPARE(image.pixelColor(qRound((contentLeft + 1) * scale), qRound(y * scale)).alpha(),
                     joined ? 255 : 0);
        }
        QCOMPARE(image.pixelColor(qRound((contentLeft - 2) * scale), qRound(58 * scale)).alpha(), 0);
    }
    tabs.moveTab(2, 1);
    QCOMPARE(tabs.tabText(1), QStringLiteral("project-two"));
    tabs.removeTab(1);
    QCOMPARE(tabs.count(), 2);
}

void TestWindowBackground::repositoryButtonHasRestingFillAndBlueHover() {
    Theme::instance()->applyToApplication();
    QWidget window;
    window.setAttribute(Qt::WA_TranslucentBackground);
    window.resize(300, 100);
    QPushButton button(&window);
    button.setObjectName(QStringLiteral("repositoriesButton"));
    button.setGeometry(10, 10, 214, 34);
    window.show();
    const auto centerColor = [&button] {
        const QImage image = button.grab().toImage();
        return image.pixelColor(image.width() / 2, image.height() / 2);
    };
    QTest::mouseMove(&window, QPoint(280, 80));
    const QColor resting = centerColor();
    QVERIFY(resting.alpha() > 0 && resting.alpha() < 255);
    QTest::mouseMove(&button, button.rect().center());
    QCOMPARE(centerColor(), Theme::instance()->palette().sidebarSelection);
}

void TestWindowBackground::separatesTabsAndCentersInactiveBackground() {
    Theme::instance()->applyToApplication();
    QWidget window;
    window.setAttribute(Qt::WA_TranslucentBackground);
    window.resize(660, 60);
    RepositoryTabStrip strip(&window);
    strip.setGeometry(window.rect());
    RepositoryTabBar tabs(&strip);
    tabs.setObjectName(QStringLiteral("repositoryTabBar"));
    tabs.setExpanding(false);
    tabs.addTab(QString());
    tabs.setTabVisible(0, false);
    for (int i = 1; i <= 3; ++i) {
        tabs.addTab(QString());
        auto *label = new QLabel(QStringLiteral("project-%1").arg(i));
        label->setObjectName(QStringLiteral("repositoryTabTitle"));
        label->setFixedSize(100, 20);
        tabs.setTabButton(i, QTabBar::LeftSide, label);
    }
    tabs.setCalculatedWidths({0, 200, 200, 200});
    tabs.setGeometry(10, 14, 600, ShellMetrics::RepositoryTabHeight);
    tabs.setCurrentIndex(3);
    strip.setTabBar(&tabs);
    window.show();
    for (const bool overview : {false, true}) {
        tabs.setProperty("overviewActive", overview);
        tabs.style()->unpolish(&tabs);
        tabs.style()->polish(&tabs);
        const QImage image = window.grab().toImage();
        const qreal scale = image.devicePixelRatio();
        const auto alpha = [&image, scale](int x, int y) {
            return image.pixelColor(qRound(x * scale), qRound(y * scale)).alpha();
        };
        for (int i = 1; i <= 3; ++i) {
            const QRect tab = tabs.tabRect(i).translated(tabs.pos());
            for (int gap = 0; gap < ShellMetrics::RepositoryTabGap; ++gap) {
                QCOMPARE(alpha(tab.right() - gap, tab.center().y()), 0);
            }
            if (!overview && i == tabs.currentIndex()) {
                continue;
            }
            const int sampleX = tab.right() - ShellMetrics::RepositoryTabGap - 15;
            int top = tab.top();
            int bottom = tab.bottom();
            while (top <= bottom && alpha(sampleX, top) == 0) ++top;
            while (bottom >= top && alpha(sampleX, bottom) == 0) --bottom;
            QVERIFY(top < bottom);
            const QWidget *label = tabs.tabButton(i, QTabBar::LeftSide);
            const QRect labelRect = label->geometry().translated(tabs.pos());
            const int above = labelRect.top() - top;
            const int below = bottom - labelRect.bottom();
            QVERIFY2(qAbs(above - below) <= 1,
                     qPrintable(QStringLiteral("Tab %1: top padding %2, bottom padding %3")
                                    .arg(i).arg(above).arg(below)));
        }
    }
}

void TestWindowBackground::updatesContentCornerWhenScrollingPastActiveTab() {
    Theme::instance()->applyToApplication();
    QWidget window;
    window.setAttribute(Qt::WA_TranslucentBackground);
    window.resize(520, 160);
    RepositoryTabStrip strip(&window);
    strip.setGeometry(0, 0, 520, 60);
    RepositoryTabBar tabs(&strip);
    tabs.setObjectName(QStringLiteral("repositoryTabBar"));
    tabs.setExpanding(false);
    tabs.setUsesScrollButtons(true);
    tabs.addTab(QString());
    tabs.setTabVisible(0, false);
    for (int i = 1; i <= 6; ++i) tabs.addTab(QStringLiteral("project-%1").arg(i));
    tabs.setCalculatedWidths({0, 180, 180, 180, 180, 180, 180});
    tabs.setGeometry(20, 14, 480, 46);
    strip.setTabBar(&tabs);
    QToolBar content(&window);
    content.setObjectName(QStringLiteral("mainToolbar"));
    content.setGeometry(20, 60, 480, 100);
    connect(&tabs, &RepositoryTabBar::leadingEdgeJoinedChanged, &content, [&] {
        const bool joined = tabs.activeTabTouchesLeadingEdge();
        strip.setLeadingTabJoined(joined);
        content.setProperty("leadingTabJoined", joined);
        content.style()->unpolish(&content);
        content.style()->polish(&content);
    });
    tabs.setCurrentIndex(2);
    window.show();
    QCoreApplication::processEvents();
    QToolButton *right = nullptr;
    QToolButton *left = nullptr;
    for (auto *button : tabs.findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (button->arrowType() == Qt::RightArrow) right = button;
        if (button->arrowType() == Qt::LeftArrow) left = button;
    }
    QVERIFY(right && left);
    bool sawJoined = false;
    bool sawScrolledAway = false;
    const auto verifyCorner = [&] {
        const QImage rendered = window.grab().toImage();
        const qreal scale = rendered.devicePixelRatio();
        const bool joined = tabs.activeTabTouchesLeadingEdge();
        QCOMPARE(content.property("leadingTabJoined").toBool(), joined);
        for (int y = 60; y <= 62; ++y) {
            QCOMPARE(rendered.pixelColor(qRound(21 * scale), qRound(y * scale)).alpha(),
                     joined ? 255 : 0);
        }
        sawJoined |= joined;
        sawScrolledAway |= tabs.tabRect(2).right() < 0;
    };
    verifyCorner();
    for (QToolButton *direction : {right, left}) {
        for (int step = 0; step < 8 && direction->isEnabled(); ++step) {
            QTest::mouseClick(direction, Qt::LeftButton);
            QCoreApplication::processEvents();
            QCOMPARE(tabs.currentIndex(), 2);
            verifyCorner();
        }
    }
    QVERIFY(sawJoined);
    QVERIFY(sawScrolledAway);
}

QTEST_MAIN(TestWindowBackground)
#include "tst_windowbackground.moc"
