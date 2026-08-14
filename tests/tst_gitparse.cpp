// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "core/gitparse.h"

#include <QTest>

namespace {

// Encodes records in Git's -z format.
QByteArray nulRecords(const QList<QByteArray> &records) {
    QByteArray payload;
    for (const QByteArray &record : records) {
        payload.append(record);
        payload.append('\0');
    }
    return payload;
}

// Encodes one commit in the format expected by parseCommits().
QByteArray commitRecord(const QList<QByteArray> &fields) {
    QByteArray record("\x1e");
    for (qsizetype index = 0; index < fields.size(); ++index) {
        if (index > 0) {
            record.append('\x1f');
        }
        record.append(fields.at(index));
    }
    return record;
}

// Avoid inline hex escapes, which can consume following hex digits.
QString refLine(const QStringList &fields) {
    return fields.join(QChar(0x01));
}

QByteArray sampleCommit(const QByteArray &hash, const QByteArray &parents,
                        const QByteArray &subject, const QByteArray &body = QByteArray()) {
    return commitRecord({hash, hash.left(7), parents, "Alice", "alice@example.com",
                         "Bob", "bob@example.com", "2026-08-14T10:00:00+03:00",
                         "2026-08-14T11:00:00+03:00", "HEAD -> main, tag: v1.0",
                         subject, body});
}

}

class TestGitParse final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void splitsNulRecordsDroppingTrailingEmpty();
    void parsesStatusEntries();
    void parsesStatusRenameSpendingTwoRecords();
    void parsesStatusRenameFollowedByAnotherFile();
    void ignoresTruncatedStatusRecords();
    void parsesCommitFields();
    void keepsSeparatorsInsideCommitBody();
    void skipsCommitsWithMissingFields();
    void parsesTrackInformation();
    void parsesBranches();
    void parsesStashesStrippingBranchPrefix();
    void parsesRemotesOnce();
    void assignsRemoteBranchesSkippingHead();
    void parsesNameStatusWithRename();
    void parsesNameStatusPairs();
    void assignsChangeCountsAndBinaryFlag();
};

void TestGitParse::splitsNulRecordsDroppingTrailingEmpty() {
    const QList<QByteArray> records = GitParse::splitNulRecords(nulRecords({"one", "two"}));
    QCOMPARE(records.size(), 2);
    QCOMPARE(records.at(0), QByteArray("one"));
    QCOMPARE(records.at(1), QByteArray("two"));
    QVERIFY(GitParse::splitNulRecords(QByteArray()).isEmpty());
}

void TestGitParse::parsesStatusEntries() {
    const QList<GitFileStatus> files = GitParse::parseStatus(nulRecords({
        " M src/main.cpp",
        "M  src/staged.cpp",
        "?? untracked.txt",
        "UU conflict.cpp"
    }));

    QCOMPARE(files.size(), 4);

    QCOMPARE(files.at(0).path, QStringLiteral("src/main.cpp"));
    QVERIFY(files.at(0).hasWorkingTreeChanges());
    QVERIFY(!files.at(0).hasStagedChanges());

    QCOMPARE(files.at(1).path, QStringLiteral("src/staged.cpp"));
    QVERIFY(files.at(1).hasStagedChanges());

    QVERIFY(files.at(2).isUntracked());
    QVERIFY(files.at(2).hasWorkingTreeChanges());

    QVERIFY(files.at(3).isConflicted());
    QVERIFY(!files.at(3).hasStagedChanges());
}

void TestGitParse::parsesStatusRenameSpendingTwoRecords() {
    const QList<GitFileStatus> files = GitParse::parseStatus(nulRecords({
        "R  new/path.cpp",
        "old/path.cpp"
    }));

    QCOMPARE(files.size(), 1);
    QCOMPARE(files.at(0).path, QStringLiteral("new/path.cpp"));
    QCOMPARE(files.at(0).originalPath, QStringLiteral("old/path.cpp"));
    QCOMPARE(files.at(0).indexStatus, u'R');
}

void TestGitParse::parsesStatusRenameFollowedByAnotherFile() {
    const QList<GitFileStatus> files = GitParse::parseStatus(nulRecords({
        "R  new/path.cpp",
        "old/path.cpp",
        " M after.cpp"
    }));

    QCOMPARE(files.size(), 2);
    QCOMPARE(files.at(0).originalPath, QStringLiteral("old/path.cpp"));
    QCOMPARE(files.at(1).path, QStringLiteral("after.cpp"));
    QVERIFY(files.at(1).originalPath.isEmpty());
}

void TestGitParse::ignoresTruncatedStatusRecords() {
    const QList<GitFileStatus> files = GitParse::parseStatus(nulRecords({"M", " M ok.cpp"}));
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.at(0).path, QStringLiteral("ok.cpp"));
}

void TestGitParse::parsesCommitFields() {
    const QList<GitCommitInfo> commits = GitParse::parseCommits(
        sampleCommit("aaaaaaaaaaaa", "bbbbbbbbbbbb cccccccccccc", "Merge two branches"));

    QCOMPARE(commits.size(), 1);
    const GitCommitInfo &commit = commits.constFirst();
    QCOMPARE(commit.hash, QStringLiteral("aaaaaaaaaaaa"));
    QCOMPARE(commit.shortHash, QStringLiteral("aaaaaaa"));
    QCOMPARE(commit.parents.size(), 2);
    QCOMPARE(commit.author, QStringLiteral("Alice"));
    QCOMPARE(commit.committerEmail, QStringLiteral("bob@example.com"));
    QCOMPARE(commit.subject, QStringLiteral("Merge two branches"));
    QCOMPARE(commit.references, QStringLiteral("HEAD -> main, tag: v1.0"));
    QVERIFY(commit.authoredAt.isValid());
    QVERIFY(commit.committedAt.isValid());
    QVERIFY(commit.authoredAt < commit.committedAt);
}

void TestGitParse::keepsSeparatorsInsideCommitBody() {
    QByteArray record = sampleCommit("dddddddddddd", "eeeeeeeeeeee", "Subject");
    record.append("first line\x1fsecond line");

    const QList<GitCommitInfo> commits = GitParse::parseCommits(record);
    QCOMPARE(commits.size(), 1);
    QCOMPARE(commits.constFirst().body,
             QStringLiteral("first linesecond line"));
}

void TestGitParse::skipsCommitsWithMissingFields() {
    QByteArray payload = commitRecord({"short", "record"});
    payload.append(sampleCommit("ffffffffffff", "", "Good one"));

    const QList<GitCommitInfo> commits = GitParse::parseCommits(payload);
    QCOMPARE(commits.size(), 1);
    QCOMPARE(commits.constFirst().subject, QStringLiteral("Good one"));
}

void TestGitParse::parsesTrackInformation() {
    int ahead = -1;
    int behind = -1;

    GitParse::parseTrackInformation(QStringLiteral("[ahead 3, behind 12]"), &ahead, &behind);
    QCOMPARE(ahead, 3);
    QCOMPARE(behind, 12);

    GitParse::parseTrackInformation(QStringLiteral("[ahead 2]"), &ahead, &behind);
    QCOMPARE(ahead, 2);
    QCOMPARE(behind, 0);

    GitParse::parseTrackInformation(QString(), &ahead, &behind);
    QCOMPARE(ahead, 0);
    QCOMPARE(behind, 0);

    GitParse::parseTrackInformation(QStringLiteral("[gone]"), &ahead, &behind);
    QCOMPARE(ahead, 0);
    QCOMPARE(behind, 0);
}

void TestGitParse::parsesBranches() {
    const QString payload =
        refLine({QStringLiteral("*"), QStringLiteral("main"), QStringLiteral("aaaaaaa"),
                 QStringLiteral("origin/main"), QStringLiteral("[ahead 1]"),
                 QStringLiteral("Latest work"), QStringLiteral("2026-08-14T10:00:00+03:00")})
        + u'\n'
        + refLine({QStringLiteral(" "), QStringLiteral("topic"), QStringLiteral("bbbbbbb"),
                   QString(), QString(), QStringLiteral("Started something"),
                   QStringLiteral("2026-08-13T09:00:00+03:00")})
        + u'\n';

    const QList<GitBranchInfo> branches = GitParse::parseBranches(payload);
    QCOMPARE(branches.size(), 2);

    QVERIFY(branches.at(0).current);
    QCOMPARE(branches.at(0).name, QStringLiteral("main"));
    QCOMPARE(branches.at(0).upstream, QStringLiteral("origin/main"));
    QCOMPARE(branches.at(0).ahead, 1);
    QCOMPARE(branches.at(0).behind, 0);
    QVERIFY(branches.at(0).committedAt.isValid());

    QVERIFY(!branches.at(1).current);
    QVERIFY(branches.at(1).upstream.isEmpty());
}

void TestGitParse::parsesStashesStrippingBranchPrefix() {
    const QString payload =
        refLine({QStringLiteral("stash@{0}"),
                 QStringLiteral("WIP on main: 1234567 Earlier subject"),
                 QStringLiteral("2026-08-14T10:00:00+03:00")})
        + u'\n'
        + refLine({QStringLiteral("stash@{1}"),
                   QStringLiteral("On topic: hand written message"),
                   QStringLiteral("2026-08-13T10:00:00+03:00")})
        + u'\n'
        + refLine({QStringLiteral("stash@{2}"), QStringLiteral("no prefix at all"),
                   QStringLiteral("2026-08-12T10:00:00+03:00")})
        + u'\n';

    const QList<GitStashInfo> stashes = GitParse::parseStashes(payload);
    QCOMPARE(stashes.size(), 3);

    QCOMPARE(stashes.at(0).index, 0);
    QCOMPARE(stashes.at(0).branch, QStringLiteral("main"));
    QCOMPARE(stashes.at(0).message, QStringLiteral("1234567 Earlier subject"));

    QCOMPARE(stashes.at(1).index, 1);
    QCOMPARE(stashes.at(1).branch, QStringLiteral("topic"));
    QCOMPARE(stashes.at(1).message, QStringLiteral("hand written message"));

    QVERIFY(stashes.at(2).branch.isEmpty());
    QCOMPARE(stashes.at(2).message, QStringLiteral("no prefix at all"));
}

void TestGitParse::parsesRemotesOnce() {
    const QList<GitRemoteInfo> remotes = GitParse::parseRemotes(QStringLiteral(
        "origin\thttps://example.com/repo.git (fetch)\n"
        "origin\thttps://example.com/repo.git (push)\n"
        "mirror\tssh://example.org/repo.git (fetch)\n"
        "mirror\tssh://example.org/repo.git (push)\n"));

    QCOMPARE(remotes.size(), 2);
    QCOMPARE(remotes.at(0).name, QStringLiteral("origin"));
    QCOMPARE(remotes.at(0).url, QStringLiteral("https://example.com/repo.git"));
    QCOMPARE(remotes.at(1).name, QStringLiteral("mirror"));
}

void TestGitParse::assignsRemoteBranchesSkippingHead() {
    QList<GitRemoteInfo> remotes = GitParse::parseRemotes(
        QStringLiteral("origin\thttps://example.com/repo.git (fetch)\n"));

    GitParse::assignRemoteBranches(remotes, QStringLiteral(
        "origin/HEAD\n"
        "origin/main\n"
        "origin/feature/nested\n"
        "unknown/branch\n"));

    QCOMPARE(remotes.size(), 1);
    QCOMPARE(remotes.constFirst().branches,
             QStringList({QStringLiteral("main"), QStringLiteral("feature/nested")}));
}

void TestGitParse::parsesNameStatusWithRename() {
    const QList<GitChangedFile> files = GitParse::parseNameStatus(nulRecords({
        "M", "kept.cpp",
        "R100", "before.cpp", "after.cpp",
        "A", "added.cpp"
    }), true);

    QCOMPARE(files.size(), 3);
    QCOMPARE(files.at(0).status, u'M');
    QCOMPARE(files.at(0).path, QStringLiteral("kept.cpp"));

    QCOMPARE(files.at(1).status, u'R');
    QCOMPARE(files.at(1).originalPath, QStringLiteral("before.cpp"));
    QCOMPARE(files.at(1).path, QStringLiteral("after.cpp"));

    QCOMPARE(files.at(2).status, u'A');
    QCOMPARE(files.at(2).path, QStringLiteral("added.cpp"));
}

void TestGitParse::parsesNameStatusPairs() {
    const QList<GitChangedFile> files = GitParse::parseNameStatus(nulRecords({
        "M", "one.cpp",
        "D", "two.cpp"
    }), false);

    QCOMPARE(files.size(), 2);
    QCOMPARE(files.at(0).path, QStringLiteral("one.cpp"));
    QCOMPARE(files.at(1).status, u'D');
}

void TestGitParse::assignsChangeCountsAndBinaryFlag() {
    QList<GitChangedFile> files = GitParse::parseNameStatus(nulRecords({
        "M", "text.cpp",
        "M", "image.png"
    }), true);

    GitParse::assignChangeCounts(files, QStringLiteral(
        "12\t3\ttext.cpp\n"
        "-\t-\timage.png\n"));

    QCOMPARE(files.at(0).additions, 12);
    QCOMPARE(files.at(0).deletions, 3);
    QVERIFY(!files.at(0).binary);

    QVERIFY(files.at(1).binary);
    QCOMPARE(files.at(1).additions, 0);
}

QTEST_APPLESS_MAIN(TestGitParse)

#include "tst_gitparse.moc"
