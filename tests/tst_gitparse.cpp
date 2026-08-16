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

constexpr char Hash[] = "9660130b61fca1839bda647daf378c01ad68ec51";
constexpr char Modes[] = "100644 100644 100644";

// "1 <XY> <sub> <mH> <mI> <mW> <hH> <hI> <path>"
QByteArray ordinaryEntry(const QByteArray &states, const QByteArray &path) {
    return "1 " + states + " N... " + QByteArray(Modes) + ' ' + Hash + ' ' + Hash + ' ' + path;
}

// "2 <XY> <sub> <mH> <mI> <mW> <hH> <hI> <Xscore> <path>", the original path
// arriving as the record that follows.
QByteArray renamedEntry(const QByteArray &states, const QByteArray &score,
                        const QByteArray &path) {
    return "2 " + states + " N... " + QByteArray(Modes) + ' ' + Hash + ' ' + Hash + ' '
           + score + ' ' + path;
}

// "u <XY> <sub> <m1> <m2> <m3> <mW> <h1> <h2> <h3> <path>"
QByteArray unmergedEntry(const QByteArray &states, const QByteArray &path) {
    return "u " + states + " N... " + QByteArray(Modes) + " 100644 " + Hash + ' ' + Hash
           + ' ' + Hash + ' ' + path;
}

// Encodes one commit of "git log --name-status -z", where the change records
// follow the commit body after a newline.
QByteArray fileRevisionRecord(const QByteArray &hash, const QByteArray &subject,
                              const QByteArray &body, const QList<QByteArray> &changes) {
    QByteArray record = commitRecord({hash, hash.left(7), "", "Alice", "alice@example.com",
                                      "Bob", "bob@example.com", "2026-08-14T10:00:00+03:00",
                                      "2026-08-14T11:00:00+03:00", "", subject, body});
    record.append('\n');
    for (const QByteArray &change : changes) {
        record.append(change);
        record.append('\0');
    }
    record.append('\0');
    return record;
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
    void parsesStatusPathsHoldingSpaces();
    void marksSubmoduleEntries();
    void ignoresTruncatedStatusRecords();
    void parsesCommitFields();
    void keepsSeparatorsInsideCommitBody();
    void skipsCommitsWithMissingFields();
    void parsesTrackInformation();
    void parsesBranches();
    void parsesStashesStrippingBranchPrefix();
    void parsesRemotesOnce();
    void parsesRemoteUrlsHoldingSpaces();
    void parsesSubmodulePathsHoldingSpaces();
    void assignsRemoteBranchesSkippingHead();
    void parsesNameStatusWithRename();
    void parsesNameStatusPairs();
    void assignsChangeCountsAndBinaryFlag();
    void assignsChangeCountsAcrossARename();
    void parsesTreeEntriesWithDirectoriesFirst();
    void readsSizeOfTreeBlobsOnly();
    void followsRenamesThroughFileHistory();
    void keepsFileHistoryBodyOutOfChangeRecords();
    void carriesPathThroughRevisionsWithoutChanges();
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
        "# branch.oid " + QByteArray(Hash),
        "# branch.head main",
        ordinaryEntry(".M", "src/main.cpp"),
        ordinaryEntry("M.", "src/staged.cpp"),
        "? untracked.txt",
        unmergedEntry("UU", "conflict.cpp"),
        "! ignored.log"
    }));

    // Headers and ignored paths carry no file state.
    QCOMPARE(files.size(), 4);

    QCOMPARE(files.at(0).path, QStringLiteral("src/main.cpp"));
    QVERIFY(files.at(0).hasWorkingTreeChanges());
    // Version 2 writes the untouched side as '.', which must not read as a
    // staged change the way a literal dot would.
    QVERIFY(!files.at(0).hasStagedChanges());

    QCOMPARE(files.at(1).path, QStringLiteral("src/staged.cpp"));
    QVERIFY(files.at(1).hasStagedChanges());
    QVERIFY(!files.at(1).hasWorkingTreeChanges());

    QVERIFY(files.at(2).isUntracked());
    QVERIFY(files.at(2).hasWorkingTreeChanges());

    QVERIFY(files.at(3).isConflicted());
    QVERIFY(!files.at(3).hasStagedChanges());
}

void TestGitParse::parsesStatusRenameSpendingTwoRecords() {
    const QList<GitFileStatus> files = GitParse::parseStatus(nulRecords({
        renamedEntry("R.", "R100", "new/path.cpp"),
        "old/path.cpp"
    }));

    QCOMPARE(files.size(), 1);
    QCOMPARE(files.at(0).path, QStringLiteral("new/path.cpp"));
    QCOMPARE(files.at(0).originalPath, QStringLiteral("old/path.cpp"));
    QCOMPARE(files.at(0).indexStatus, u'R');
    QCOMPARE(files.at(0).renameScore, 100);
}

void TestGitParse::parsesStatusRenameFollowedByAnotherFile() {
    const QList<GitFileStatus> files = GitParse::parseStatus(nulRecords({
        renamedEntry("R.", "C75", "new/path.cpp"),
        "old/path.cpp",
        ordinaryEntry(".M", "after.cpp")
    }));

    QCOMPARE(files.size(), 2);
    QCOMPARE(files.at(0).originalPath, QStringLiteral("old/path.cpp"));
    QCOMPARE(files.at(0).renameScore, 75);
    QCOMPARE(files.at(1).path, QStringLiteral("after.cpp"));
    QVERIFY(files.at(1).originalPath.isEmpty());
}

// The path is the last field and is never quoted under -z.
void TestGitParse::parsesStatusPathsHoldingSpaces() {
    const QList<GitFileStatus> files = GitParse::parseStatus(nulRecords({
        ordinaryEntry(".M", "a report \"v2\" 🐙.txt"),
        "? another new file.txt"
    }));

    QCOMPARE(files.size(), 2);
    QCOMPARE(files.at(0).path, QStringLiteral("a report \"v2\" 🐙.txt"));
    QCOMPARE(files.at(1).path, QStringLiteral("another new file.txt"));
}

void TestGitParse::marksSubmoduleEntries() {
    QByteArray submodule = ordinaryEntry(".M", "vendor/library");
    submodule.replace("N...", "S.M.");

    const QList<GitFileStatus> files = GitParse::parseStatus(nulRecords({
        submodule,
        ordinaryEntry(".M", "plain.cpp")
    }));

    QCOMPARE(files.size(), 2);
    QVERIFY(files.at(0).submodule);
    QVERIFY(!files.at(1).submodule);
}

void TestGitParse::ignoresTruncatedStatusRecords() {
    const QList<GitFileStatus> files = GitParse::parseStatus(nulRecords({
        "1 .M",
        "x nonsense",
        ordinaryEntry(".M", "ok.cpp")
    }));
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

// A local or Windows remote often holds a space.
void TestGitParse::parsesRemoteUrlsHoldingSpaces() {
    const QList<GitRemoteInfo> remotes = GitParse::parseRemotes(QStringLiteral(
        "origin\t/home/user/My Projects/repo.git (fetch)\n"
        "origin\t/home/user/My Projects/repo.git (push)\n"));

    QCOMPARE(remotes.size(), 1);
    QCOMPARE(remotes.constFirst().url, QStringLiteral("/home/user/My Projects/repo.git"));
}

void TestGitParse::parsesSubmodulePathsHoldingSpaces() {
    const QList<GitSubmoduleInfo> submodules = GitParse::parseSubmodules(QStringLiteral(
        " 9660130b61fca1839bda647daf378c01ad68ec51 vendor/my library (v1.2.0)\n"
        "-9660130b61fca1839bda647daf378c01ad68ec51 vendor/not checked out\n"));

    QCOMPARE(submodules.size(), 2);
    QCOMPARE(submodules.at(0).path, QStringLiteral("vendor/my library"));
    QCOMPARE(submodules.at(0).describe, QStringLiteral("v1.2.0"));
    QCOMPARE(submodules.at(1).path, QStringLiteral("vendor/not checked out"));
    QVERIFY(submodules.at(1).describe.isEmpty());
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

    GitParse::assignChangeCounts(files, nulRecords({
        "12\t3\ttext.cpp",
        "-\t-\timage.png"
    }));

    QCOMPARE(files.at(0).additions, 12);
    QCOMPARE(files.at(0).deletions, 3);
    QVERIFY(!files.at(0).binary);

    QVERIFY(files.at(1).binary);
    QCOMPARE(files.at(1).additions, 0);
}

// Under -z a rename leaves the path empty and sends the old and new paths as
// the next two records, so no " => " has to be guessed at.
void TestGitParse::assignsChangeCountsAcrossARename() {
    QList<GitChangedFile> files = GitParse::parseNameStatus(nulRecords({
        "R100", "old name.cpp", "new name 🐙.cpp",
        "M", "plain.cpp"
    }), true);

    GitParse::assignChangeCounts(files, nulRecords({
        "4\t2\t", "old name.cpp", "new name 🐙.cpp",
        "1\t1\tplain.cpp"
    }));

    QCOMPARE(files.at(0).path, QStringLiteral("new name 🐙.cpp"));
    QCOMPARE(files.at(0).additions, 4);
    QCOMPARE(files.at(0).deletions, 2);
    QCOMPARE(files.at(1).additions, 1);
}

void TestGitParse::parsesTreeEntriesWithDirectoriesFirst() {
    const QList<GitTreeEntry> entries = GitParse::parseTreeEntries(nulRecords({
        "100644 blob f384549cbeb481e437091320de6d1f2e15e11b4a      19\tsrc/main.cpp",
        "040000 tree ca220cc0cb1424bd101ce23c993b0927e6f10ffc       -\tsrc/ui",
        "160000 commit 5626abf0f72e58d7a153368ba57db4c673c0e171       -\tsrc/vendor",
        "100644 blob 298944b11df396e6cf17e803af4ac9abe4676812     107\tsrc/Api.cpp"
    }));

    QCOMPARE(entries.size(), 4);

    QCOMPARE(entries.at(0).name, QStringLiteral("ui"));
    QVERIFY(entries.at(0).directory);
    QCOMPARE(entries.at(0).path, QStringLiteral("src/ui"));

    // Case is ignored when ordering, and submodules are files of the level.
    QCOMPARE(entries.at(1).name, QStringLiteral("Api.cpp"));
    QCOMPARE(entries.at(2).name, QStringLiteral("main.cpp"));
    QCOMPARE(entries.at(3).name, QStringLiteral("vendor"));
    QVERIFY(entries.at(3).submodule);
    QVERIFY(!entries.at(3).directory);
}

void TestGitParse::readsSizeOfTreeBlobsOnly() {
    const QList<GitTreeEntry> entries = GitParse::parseTreeEntries(nulRecords({
        "100644 blob f384549cbeb481e437091320de6d1f2e15e11b4a      19\treadme.md",
        "040000 tree ca220cc0cb1424bd101ce23c993b0927e6f10ffc       -\tdocs",
        "this record has no tab"
    }));

    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).name, QStringLiteral("docs"));
    QCOMPARE(entries.at(0).size, -1);
    QCOMPARE(entries.at(1).name, QStringLiteral("readme.md"));
    QCOMPARE(entries.at(1).size, 19);
    QCOMPARE(entries.at(1).hash,
             QStringLiteral("f384549cbeb481e437091320de6d1f2e15e11b4a"));
}

void TestGitParse::followsRenamesThroughFileHistory() {
    QByteArray payload;
    payload.append(fileRevisionRecord("aaaa111", "fourth", "", {"M", "src/new.txt"}));
    payload.append(fileRevisionRecord("bbbb222", "rename and edit", "",
                                      {"R100", "src/old.txt", "src/new.txt"}));
    payload.append(fileRevisionRecord("cccc333", "first", "", {"A", "src/old.txt"}));

    const QList<GitFileRevision> revisions = GitParse::parseFileHistory(payload);

    QCOMPARE(revisions.size(), 3);

    QCOMPARE(revisions.at(0).status, u'M');
    QCOMPARE(revisions.at(0).path, QStringLiteral("src/new.txt"));
    QVERIFY(revisions.at(0).previousPath.isEmpty());

    // The rename keeps both paths, because content has to be read by the path
    // the file carried at that commit.
    QVERIFY(revisions.at(1).isRename());
    QCOMPARE(revisions.at(1).previousPath, QStringLiteral("src/old.txt"));
    QCOMPARE(revisions.at(1).path, QStringLiteral("src/new.txt"));

    QVERIFY(revisions.at(2).isAddition());
    QCOMPARE(revisions.at(2).path, QStringLiteral("src/old.txt"));
    QCOMPARE(revisions.at(2).commit.subject, QStringLiteral("first"));
}

void TestGitParse::keepsFileHistoryBodyOutOfChangeRecords() {
    const QByteArray payload = fileRevisionRecord("aaaa111", "subject",
                                                  "first body line\nsecond body line",
                                                  {"M", "src/main.cpp"});

    const QList<GitFileRevision> revisions = GitParse::parseFileHistory(payload);

    QCOMPARE(revisions.size(), 1);
    QCOMPARE(revisions.at(0).commit.body,
             QStringLiteral("first body line\nsecond body line"));
    QCOMPARE(revisions.at(0).path, QStringLiteral("src/main.cpp"));
}

void TestGitParse::carriesPathThroughRevisionsWithoutChanges() {
    QByteArray payload;
    payload.append(fileRevisionRecord("aaaa111", "rename", "",
                                      {"R100", "src/old.txt", "src/new.txt"}));
    payload.append(fileRevisionRecord("bbbb222", "merge", "", {}));

    const QList<GitFileRevision> revisions = GitParse::parseFileHistory(payload);

    QCOMPARE(revisions.size(), 2);
    // A commit reported without a diff sits below the rename, so it carries
    // the name the file had before it.
    QCOMPARE(revisions.at(1).path, QStringLiteral("src/old.txt"));
}

QTEST_APPLESS_MAIN(TestGitParse)

#include "tst_gitparse.moc"
