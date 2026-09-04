// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "ui/icons.h"

#include <QImage>
#include <QTest>

class TestIcons final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void branchNodesHaveTransparentCenters();
};

void TestIcons::branchNodesHaveTransparentCenters() {
    const QImage image = Icons::pixmap(Icons::Glyph::Branch, 48, QColor("#91AEC4")).toImage();
    const qreal scale = image.devicePixelRatio() * 2;
    for (const QPoint center : {QPoint(7, 4), QPoint(7, 20), QPoint(17, 8)}) {
        QCOMPARE(image.pixelColor(qRound(center.x() * scale),
                                   qRound(center.y() * scale)).alpha(), 0);
        QVERIFY(image.pixelColor(qRound((center.x() + 2) * scale),
                                  qRound(center.y() * scale)).alpha() > 0);
    }
}

QTEST_MAIN(TestIcons)
#include "tst_icons.moc"
