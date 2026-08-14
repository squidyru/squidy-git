// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "core/graphlayout.h"

#include <QTest>

namespace {

GitCommitInfo commit(const QString &hash, const QStringList &parents) {
    GitCommitInfo info;
    info.hash = hash;
    info.shortHash = hash;
    info.parents = parents;
    return info;
}

QList<GitCommitInfo> chain(const QStringList &hashes) {
    QList<GitCommitInfo> commits;
    for (qsizetype index = 0; index < hashes.size(); ++index) {
        const QStringList parents = index + 1 < hashes.size()
                                        ? QStringList{hashes.at(index + 1)}
                                        : QStringList();
        commits.append(commit(hashes.at(index), parents));
    }
    return commits;
}

}

class TestGraphLayout final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void laysOutStraightChainInOneLane();
    void reportsUniformLaneCount();
    void addsLeadingUncommittedRow();
    void addsLeadingRowWithoutHead();
    void marksMergeAndFansOutParents();
    void bringsMergedBranchBackToOneLane();
    void handlesOctopusMerge();
    void keepsDanglingParentsOfTruncatedHistory();
    void handlesEmptyHistory();
    void splitsReferences();
};

void TestGraphLayout::laysOutStraightChainInOneLane() {
    const QList<GraphRow> rows = computeCommitGraph(
        chain({QStringLiteral("c"), QStringLiteral("b"), QStringLiteral("a")}), false, QString());

    QCOMPARE(rows.size(), 3);
    for (const GraphRow &row : rows) {
        QCOMPARE(row.lane, 0);
        QVERIFY(!row.isMerge);
        QVERIFY(row.passEdges.isEmpty());
    }
    QVERIFY(!rows.at(0).hasIncoming);
    QVERIFY(rows.at(1).hasIncoming);
    QVERIFY(rows.at(2).hasIncoming);
}

void TestGraphLayout::reportsUniformLaneCount() {
    QList<GitCommitInfo> commits;
    commits.append(commit(QStringLiteral("m"), {QStringLiteral("a"), QStringLiteral("b")}));
    commits.append(commit(QStringLiteral("a"), {QStringLiteral("root")}));
    commits.append(commit(QStringLiteral("b"), {QStringLiteral("root")}));
    commits.append(commit(QStringLiteral("root"), {}));

    const QList<GraphRow> rows = computeCommitGraph(commits, false, QString());

    QVERIFY(!rows.isEmpty());
    const int width = rows.constFirst().laneCount;
    QVERIFY(width >= 2);
    for (const GraphRow &row : rows) {
        QCOMPARE(row.laneCount, width);
    }
}

void TestGraphLayout::addsLeadingUncommittedRow() {
    const QList<GitCommitInfo> commits = chain({QStringLiteral("b"), QStringLiteral("a")});
    const QList<GraphRow> rows = computeCommitGraph(commits, true, QStringLiteral("b"));

    QCOMPARE(rows.size(), commits.size() + 1);
    QCOMPARE(rows.constFirst().lane, 0);
    QVERIFY(!rows.constFirst().hasIncoming);
    QCOMPARE(rows.constFirst().parentLanes, QList<int>{0});
    QVERIFY(rows.at(1).hasIncoming);
}

void TestGraphLayout::addsLeadingRowWithoutHead() {
    const QList<GraphRow> rows = computeCommitGraph({}, true, QString());

    QCOMPARE(rows.size(), 1);
    QVERIFY(rows.constFirst().parentLanes.isEmpty());
    QVERIFY(!rows.constFirst().hasIncoming);
}

void TestGraphLayout::marksMergeAndFansOutParents() {
    QList<GitCommitInfo> commits;
    commits.append(commit(QStringLiteral("m"), {QStringLiteral("a"), QStringLiteral("b")}));
    commits.append(commit(QStringLiteral("a"), {QStringLiteral("root")}));
    commits.append(commit(QStringLiteral("b"), {QStringLiteral("root")}));
    commits.append(commit(QStringLiteral("root"), {}));

    const QList<GraphRow> rows = computeCommitGraph(commits, false, QString());

    QVERIFY(rows.at(0).isMerge);
    QCOMPARE(rows.at(0).parentLanes.size(), 2);
    QVERIFY(rows.at(0).parentLanes.at(0) != rows.at(0).parentLanes.at(1));
    QVERIFY(!rows.at(1).isMerge);
}

void TestGraphLayout::bringsMergedBranchBackToOneLane() {
    QList<GitCommitInfo> commits;
    commits.append(commit(QStringLiteral("m"), {QStringLiteral("a"), QStringLiteral("b")}));
    commits.append(commit(QStringLiteral("a"), {QStringLiteral("root")}));
    commits.append(commit(QStringLiteral("b"), {QStringLiteral("root")}));
    commits.append(commit(QStringLiteral("root"), {}));

    const QList<GraphRow> rows = computeCommitGraph(commits, false, QString());

    QCOMPARE(rows.constLast().lane, 0);
    QVERIFY(rows.constLast().hasIncoming);
    QVERIFY(rows.constLast().parentLanes.isEmpty());
}

void TestGraphLayout::handlesOctopusMerge() {
    QList<GitCommitInfo> commits;
    commits.append(commit(QStringLiteral("o"), {QStringLiteral("a"), QStringLiteral("b"),
                                                QStringLiteral("c")}));
    commits.append(commit(QStringLiteral("a"), {}));
    commits.append(commit(QStringLiteral("b"), {}));
    commits.append(commit(QStringLiteral("c"), {}));

    const QList<GraphRow> rows = computeCommitGraph(commits, false, QString());

    QVERIFY(rows.constFirst().isMerge);
    QCOMPARE(rows.constFirst().parentLanes.size(), 3);
    QVERIFY(rows.constFirst().laneCount >= 3);
    for (qsizetype index = 1; index < rows.size(); ++index) {
        QVERIFY(rows.at(index).hasIncoming);
    }
}

void TestGraphLayout::keepsDanglingParentsOfTruncatedHistory() {
    const QList<GraphRow> rows = computeCommitGraph(
        {commit(QStringLiteral("b"), {QStringLiteral("missing")})}, false, QString());

    QCOMPARE(rows.size(), 1);
    QVERIFY(!rows.constFirst().hasIncoming);
    QCOMPARE(rows.constFirst().parentLanes, QList<int>{0});
}

void TestGraphLayout::handlesEmptyHistory() {
    const QList<GraphRow> rows = computeCommitGraph({}, false, QString());
    QVERIFY(rows.isEmpty());
}

void TestGraphLayout::splitsReferences() {
    QCOMPARE(splitReferences(QStringLiteral("HEAD -> main, origin/main, tag: v1.0")),
             QStringList({QStringLiteral("HEAD -> main"), QStringLiteral("origin/main"),
                          QStringLiteral("tag: v1.0")}));
    QVERIFY(splitReferences(QString()).isEmpty());
    QCOMPARE(splitReferences(QStringLiteral("  main  ")),
             QStringList{QStringLiteral("main")});
}

QTEST_APPLESS_MAIN(TestGraphLayout)

#include "tst_graphlayout.moc"
