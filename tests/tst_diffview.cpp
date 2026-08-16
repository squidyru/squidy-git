// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "ui/diffview.h"

#include <QTest>

// Rendering details of the diff viewer that are easy to break unnoticed.
class TestDiffView final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void fitsLineNumbersOfALongFile();
    void keepsTheGutterNarrowForAShortFile();
    void dropsTheGutterWithoutAPatch();

private:
    /// A three-line hunk that starts at @p start in both files.
    [[nodiscard]] static QString patchStartingAt(int start);
};

QString TestDiffView::patchStartingAt(const int start) {
    return QStringLiteral("diff --git a/file.py b/file.py\n"
                          "--- a/file.py\n"
                          "+++ b/file.py\n"
                          "@@ -%1,3 +%1,3 @@\n"
                          " context\n"
                          "-removed\n"
                          "+added\n").arg(start);
}

void TestDiffView::fitsLineNumbersOfALongFile() {
    DiffView view;
    view.setPatch(patchStartingAt(1806));

    // Two columns of four digits have to fit; the patch itself is seven lines
    // long, which is what the width used to be derived from.
    const int digitWidth = view.fontMetrics().horizontalAdvance(u'9');
    QVERIFY2(view.gutterWidth() >= 2 * 4 * digitWidth,
             "four-digit line numbers must fit into the gutter");
}

void TestDiffView::keepsTheGutterNarrowForAShortFile() {
    DiffView shortFile;
    shortFile.setPatch(patchStartingAt(12));

    DiffView longFile;
    longFile.setPatch(patchStartingAt(1806));

    QVERIFY(shortFile.gutterWidth() > 0);
    QVERIFY(shortFile.gutterWidth() < longFile.gutterWidth());
}

void TestDiffView::dropsTheGutterWithoutAPatch() {
    DiffView view;
    QCOMPARE(view.gutterWidth(), 0);
}

QTEST_MAIN(TestDiffView)

#include "tst_diffview.moc"
