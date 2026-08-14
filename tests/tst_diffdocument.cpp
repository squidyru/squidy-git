// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "core/diffdocument.h"

#include <QTest>

namespace {

QString samplePatch() {
    return QStringLiteral(
        "diff --git a/main.cpp b/main.cpp\n"
        "index 1111111..2222222 100644\n"
        "--- a/main.cpp\n"
        "+++ b/main.cpp\n"
        "@@ -1,5 +1,6 @@\n"
        " #include <cstdio>\n"
        "-int old_one();\n"
        "-int old_two();\n"
        "+int new_one();\n"
        "+int new_two();\n"
        "+int new_three();\n"
        " \n"
        "@@ -20,3 +21,3 @@\n"
        " void tail() {\n"
        "-    puts(\"before\");\n"
        "+    puts(\"after\");\n"
        " }\n");
}

QList<int> linesOfType(const DiffDocument &document, const DiffLine::Type type) {
    QList<int> indices;
    for (int index = 0; index < document.lines().size(); ++index) {
        if (document.lines().at(index).type == type) {
            indices.append(index);
        }
    }
    return indices;
}

QStringList bodyOf(const QByteArray &patch) {
    QStringList lines = QString::fromUtf8(patch).split(u'\n');
    while (!lines.isEmpty() && lines.constLast().isEmpty()) {
        lines.removeLast();
    }
    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (lines.at(index).startsWith(QStringLiteral("@@ "))) {
            return lines.mid(index);
        }
    }
    return {};
}

}

class TestDiffDocument final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void parsesHunksAndLineNumbers();
    void parsesEmptyPatch();
    void countsLineNumbersAcrossHunks();
    void buildsPatchForWholeHunk();
    void keepsUnselectedAdditionsAsContextWhenReversing();
    void dropsUnselectedAdditionsWhenApplyingForward();
    void keepsUnselectedRemovalsAsContextWhenApplyingForward();
    void refusesSelectionsSpanningTwoHunks();
    void refusesSelectionWithoutAnyChange();
    void reportsHunkCountsInHeader();
    void handlesNewFileWithoutContext();
};

void TestDiffDocument::parsesHunksAndLineNumbers() {
    DiffDocument document;
    document.parse(samplePatch());

    QCOMPARE(document.hunkCount(), 2);
    QVERIFY(!document.isEmpty());
    QCOMPARE(document.hunks().at(0).oldStart, 1);
    QCOMPARE(document.hunks().at(0).newStart, 1);
    QCOMPARE(document.hunks().at(1).oldStart, 20);
    QCOMPARE(document.hunks().at(1).newStart, 21);
}

void TestDiffDocument::parsesEmptyPatch() {
    DiffDocument document;
    document.parse(QString());

    QVERIFY(document.isEmpty());
    QCOMPARE(document.hunkCount(), 0);
    QVERIFY(document.patchForHunk(0, false).isEmpty());
    QVERIFY(document.patchForLines({0, 1}, false).isEmpty());
}

void TestDiffDocument::countsLineNumbersAcrossHunks() {
    DiffDocument document;
    document.parse(samplePatch());

    const QList<int> context = linesOfType(document, DiffLine::Type::Context);
    QVERIFY(context.size() >= 4);
    const DiffLine &tail = document.lines().at(context.at(2));
    QCOMPARE(tail.text, QStringLiteral(" void tail() {"));
    QCOMPARE(tail.oldNumber, 20);
    QCOMPARE(tail.newNumber, 21);
}

void TestDiffDocument::buildsPatchForWholeHunk() {
    DiffDocument document;
    document.parse(samplePatch());

    const QString patch = QString::fromUtf8(document.patchForHunk(1, false));
    QVERIFY(patch.startsWith(QStringLiteral("diff --git a/main.cpp b/main.cpp\n")));
    QVERIFY(patch.contains(QStringLiteral("@@ -20,3 +21,3 @@")));
    QVERIFY(patch.contains(QStringLiteral("-    puts(\"before\");")));
    QVERIFY(patch.contains(QStringLiteral("+    puts(\"after\");")));
    QVERIFY(patch.endsWith(u'\n'));
    QVERIFY(!patch.contains(QStringLiteral("new_three")));
}

void TestDiffDocument::keepsUnselectedAdditionsAsContextWhenReversing() {
    DiffDocument document;
    document.parse(samplePatch());

    const QList<int> added = linesOfType(document, DiffLine::Type::Added);
    QCOMPARE(added.size(), 4);
    const QStringList body = bodyOf(document.patchForLines({added.at(0)}, true));

    QCOMPARE(body.at(0), QStringLiteral("@@ -1,4 +1,5 @@"));
    QVERIFY(body.contains(QStringLiteral("+int new_one();")));
    QVERIFY(body.contains(QStringLiteral(" int new_two();")));
    QVERIFY(body.contains(QStringLiteral(" int new_three();")));
    QVERIFY(!body.contains(QStringLiteral("+int new_two();")));
}

void TestDiffDocument::dropsUnselectedAdditionsWhenApplyingForward() {
    DiffDocument document;
    document.parse(samplePatch());

    const QList<int> added = linesOfType(document, DiffLine::Type::Added);
    const QStringList body = bodyOf(document.patchForLines({added.at(0)}, false));

    QVERIFY(body.contains(QStringLiteral("+int new_one();")));
    QVERIFY(!body.contains(QStringLiteral(" int new_two();")));
    QVERIFY(!body.contains(QStringLiteral("+int new_two();")));
}

void TestDiffDocument::keepsUnselectedRemovalsAsContextWhenApplyingForward() {
    DiffDocument document;
    document.parse(samplePatch());

    const QList<int> removed = linesOfType(document, DiffLine::Type::Removed);
    QCOMPARE(removed.size(), 3);
    const QStringList body = bodyOf(document.patchForLines({removed.at(0)}, false));

    QVERIFY(body.contains(QStringLiteral("-int old_one();")));
    QVERIFY(body.contains(QStringLiteral(" int old_two();")));
}

void TestDiffDocument::refusesSelectionsSpanningTwoHunks() {
    DiffDocument document;
    document.parse(samplePatch());

    const QList<int> added = linesOfType(document, DiffLine::Type::Added);
    QVERIFY(document.lines().at(added.constFirst()).hunkIndex
            != document.lines().at(added.constLast()).hunkIndex);
    QVERIFY(document.patchForLines({added.constFirst(), added.constLast()}, false).isEmpty());
}

void TestDiffDocument::refusesSelectionWithoutAnyChange() {
    DiffDocument document;
    document.parse(samplePatch());

    const QList<int> context = linesOfType(document, DiffLine::Type::Context);
    QVERIFY(!context.isEmpty());
    QVERIFY(document.patchForLines({context.constFirst()}, false).isEmpty());
}

void TestDiffDocument::reportsHunkCountsInHeader() {
    DiffDocument document;
    document.parse(samplePatch());

    const QList<int> removed = linesOfType(document, DiffLine::Type::Removed);
    const QStringList body = bodyOf(document.patchForLines({removed.at(0), removed.at(1)}, false));
    QCOMPARE(body.at(0), QStringLiteral("@@ -1,4 +1,2 @@"));
}

void TestDiffDocument::handlesNewFileWithoutContext() {
    DiffDocument document;
    document.parse(QStringLiteral(
        "diff --git a/new.txt b/new.txt\n"
        "new file mode 100644\n"
        "index 0000000..3333333\n"
        "--- /dev/null\n"
        "+++ b/new.txt\n"
        "@@ -0,0 +1,2 @@\n"
        "+first\n"
        "+second\n"));

    QCOMPARE(document.hunkCount(), 1);
    const QStringList body = bodyOf(document.patchForHunk(0, false));
    QCOMPARE(body.at(0), QStringLiteral("@@ -0,0 +1,2 @@"));
    QVERIFY(body.contains(QStringLiteral("+first")));
    QVERIFY(body.contains(QStringLiteral("+second")));
}

QTEST_APPLESS_MAIN(TestDiffDocument)

#include "tst_diffdocument.moc"
