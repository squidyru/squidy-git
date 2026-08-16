// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "core/diffdocument.h"
#include "core/gitclient.h"
#include "core/gitparse.h"
#include "core/gitprocess.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

namespace {

// Records commands without starting Git.
class RecordedRunner final : public GitProcessRunner {
public:
    GitCommandResult run(const QString &workingDirectory, const QStringList &arguments,
                         const int timeoutMs, const QByteArray *input) override {
        lastDirectory = workingDirectory;
        lastArguments = arguments;
        lastTimeoutMs = timeoutMs;
        lastInput = input != nullptr ? *input : QByteArray();
        ++invocations;
        return next;
    }

    QString lastDirectory;
    QStringList lastArguments;
    int lastTimeoutMs = 0;
    QByteArray lastInput;
    int invocations = 0;
    GitCommandResult next;
};

GitCommandResult succeededWith(const QByteArray &output) {
    GitCommandResult result;
    result.exitCode = 0;
    result.output = output;
    return result;
}

}

class TestGitClient final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void refusesToRunWithoutRepository();
    void asksForMachineReadableStatus();
    void passesPatchThroughStandardInput();

    void readsStatusOfNewFile();
    void stagesAndCommits();
    void readsHistoryAndBranches();
    void appliesPartialPatchBuiltFromRealDiff();
    void unstagesPartialPatchInReverse();

    void followsFileHistoryAcrossRename();
    void readsTreeLevelsAtRevision();
    void reachesDeletedFilesThroughOlderRevisions();
    void readsHistoricalContentWithoutTouchingWorkingTree();
    void comparesVersionsThatMovedBetweenPaths();
    void listsWorkingCopyLevelWithSizes();
    void listsEveryTrackedFileOfARevision();

private:
    [[nodiscard]] bool openTemporaryRepository();
    void writeFile(const QString &name, const QString &contents) const;
    [[nodiscard]] QString readFile(const QString &name) const;
    /// Stages every change and commits it.
    [[nodiscard]] bool commitAll(const QString &message);
    /// Builds a repository where a file is renamed and another one is deleted.
    [[nodiscard]] bool buildHistoryWithRenameAndDeletion();

    QTemporaryDir *directory_ = nullptr;
    GitClient git_;
};

void TestGitClient::initTestCase() {
    if (GitClient::gitExecutable().isEmpty()) {
        QSKIP("git is not installed, the integration suite cannot run");
    }
}

void TestGitClient::init() {
    directory_ = new QTemporaryDir;
    QVERIFY(directory_->isValid());
    git_ = GitClient();
}

void TestGitClient::cleanup() {
    delete directory_;
    directory_ = nullptr;
}

bool TestGitClient::openTemporaryRepository() {
    if (!GitClient::initRepository(directory_->path(), false).succeeded()) {
        return false;
    }
    if (!git_.openRepository(directory_->path()).succeeded()) {
        return false;
    }
    return git_.runCustom({QStringLiteral("config"), QStringLiteral("user.name"),
                           QStringLiteral("Test")}).succeeded()
           && git_.runCustom({QStringLiteral("config"), QStringLiteral("user.email"),
                              QStringLiteral("test@example.com")}).succeeded();
}

void TestGitClient::writeFile(const QString &name, const QString &contents) const {
    QFile file(QDir(git_.repositoryRoot()).filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream stream(&file);
    stream << contents;
}

QString TestGitClient::readFile(const QString &name) const {
    QFile file(QDir(git_.repositoryRoot()).filePath(name));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

void TestGitClient::refusesToRunWithoutRepository() {
    RecordedRunner runner;
    const GitClient client(&runner);

    QString errorMessage;
    const QList<GitFileStatus> files = client.status(&errorMessage);

    QVERIFY(files.isEmpty());
    QVERIFY(!errorMessage.isEmpty());
    QCOMPARE(runner.invocations, 0);
}

void TestGitClient::asksForMachineReadableStatus() {
    RecordedRunner runner;
    GitClient client(&runner);
    runner.next = succeededWith(QByteArray("/somewhere\n"));
    QVERIFY(client.openRepository(QStringLiteral("/somewhere")).succeeded());
    QCOMPARE(client.repositoryRoot(), QStringLiteral("/somewhere"));

    runner.next = succeededWith(QByteArray("?? new.txt\0", 11));
    const QList<GitFileStatus> files = client.status();

    QVERIFY(runner.lastArguments.contains(QStringLiteral("status")));
    QVERIFY(runner.lastArguments.contains(QStringLiteral("--porcelain=v1")));
    QVERIFY(runner.lastArguments.contains(QStringLiteral("-z")));
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.constFirst().path, QStringLiteral("new.txt"));
}

void TestGitClient::passesPatchThroughStandardInput() {
    RecordedRunner runner;
    runner.next = succeededWith({});

    GitClient client(&runner);
    runner.next = succeededWith(QByteArray("/tmp/repo\n"));
    QVERIFY(client.openRepository(QStringLiteral("/tmp/repo")).succeeded());

    const QByteArray patch("diff --git a/x b/x\n");
    runner.next = succeededWith({});
    QVERIFY(client.applyPatch(patch, true, false).succeeded());

    QCOMPARE(runner.lastInput, patch);
    QVERIFY(runner.lastArguments.contains(QStringLiteral("apply")));
    QVERIFY(runner.lastArguments.contains(QStringLiteral("--cached")));
    QVERIFY(!runner.lastArguments.contains(QStringLiteral("--reverse")));
}

void TestGitClient::readsStatusOfNewFile() {
    QVERIFY(openTemporaryRepository());
    writeFile(QStringLiteral("new.txt"), QStringLiteral("hello\n"));

    const QList<GitFileStatus> files = git_.status();
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.constFirst().path, QStringLiteral("new.txt"));
    QVERIFY(files.constFirst().isUntracked());
    QVERIFY(!git_.hasCommits());
}

void TestGitClient::stagesAndCommits() {
    QVERIFY(openTemporaryRepository());
    writeFile(QStringLiteral("new.txt"), QStringLiteral("hello\n"));

    QVERIFY(git_.stage({QStringLiteral("new.txt")}).succeeded());
    const QList<GitFileStatus> staged = git_.status();
    QCOMPARE(staged.size(), 1);
    QVERIFY(staged.constFirst().hasStagedChanges());

    QVERIFY(git_.commit(QStringLiteral("Add a file"), false).succeeded());
    QVERIFY(git_.hasCommits());
    QVERIFY(git_.status().isEmpty());
    QCOMPARE(git_.lastCommitMessage().trimmed(), QStringLiteral("Add a file"));
}

void TestGitClient::readsHistoryAndBranches() {
    QVERIFY(openTemporaryRepository());
    writeFile(QStringLiteral("new.txt"), QStringLiteral("hello\n"));
    QVERIFY(git_.stage({QStringLiteral("new.txt")}).succeeded());
    QVERIFY(git_.commit(QStringLiteral("First commit"), false).succeeded());

    GitHistoryOptions options;
    options.maximumCount = 10;
    const QList<GitCommitInfo> commits = git_.history(options);
    QCOMPARE(commits.size(), 1);
    QCOMPARE(commits.constFirst().subject, QStringLiteral("First commit"));
    QCOMPARE(commits.constFirst().author, QStringLiteral("Test"));
    QVERIFY(commits.constFirst().parents.isEmpty());
    QVERIFY(commits.constFirst().committedAt.isValid());

    const QList<GitBranchInfo> branches = git_.branches();
    QCOMPARE(branches.size(), 1);
    QVERIFY(branches.constFirst().current);
    QCOMPARE(branches.constFirst().subject, QStringLiteral("First commit"));

    const QList<GitChangedFile> files = git_.commitFiles(commits.constFirst().hash);
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.constFirst().path, QStringLiteral("new.txt"));
    QCOMPARE(files.constFirst().additions, 1);
}

void TestGitClient::appliesPartialPatchBuiltFromRealDiff() {
    QVERIFY(openTemporaryRepository());
    writeFile(QStringLiteral("lines.txt"), QStringLiteral("one\ntwo\nthree\n"));
    QVERIFY(git_.stage({QStringLiteral("lines.txt")}).succeeded());
    QVERIFY(git_.commit(QStringLiteral("Base"), false).succeeded());

    writeFile(QStringLiteral("lines.txt"),
              QStringLiteral("one\nfirst added\ntwo\nsecond added\nthree\n"));

    DiffDocument document;
    document.parse(git_.diff(QStringLiteral("lines.txt"), false, false).outputText());
    QCOMPARE(document.hunkCount(), 1);

    int firstAddition = -1;
    for (int index = 0; index < document.lines().size(); ++index) {
        if (document.lines().at(index).text == QStringLiteral("+first added")) {
            firstAddition = index;
            break;
        }
    }
    QVERIFY(firstAddition >= 0);

    const QByteArray patch = document.patchForLines({firstAddition}, false);
    QVERIFY(!patch.isEmpty());
    const GitCommandResult applied = git_.applyPatch(patch, true, false);
    QVERIFY2(applied.succeeded(), qPrintable(applied.errorText()));

    const QString staged = git_.diff(QStringLiteral("lines.txt"), true, false).outputText();
    QVERIFY(staged.contains(QStringLiteral("+first added")));
    QVERIFY(!staged.contains(QStringLiteral("+second added")));
    QVERIFY(readFile(QStringLiteral("lines.txt")).contains(QStringLiteral("second added")));
}

void TestGitClient::unstagesPartialPatchInReverse() {
    QVERIFY(openTemporaryRepository());
    writeFile(QStringLiteral("lines.txt"), QStringLiteral("one\ntwo\nthree\n"));
    QVERIFY(git_.stage({QStringLiteral("lines.txt")}).succeeded());
    QVERIFY(git_.commit(QStringLiteral("Base"), false).succeeded());

    writeFile(QStringLiteral("lines.txt"),
              QStringLiteral("one\nfirst added\ntwo\nsecond added\nthree\n"));
    QVERIFY(git_.stage({QStringLiteral("lines.txt")}).succeeded());

    DiffDocument document;
    document.parse(git_.diff(QStringLiteral("lines.txt"), true, false).outputText());

    int secondAddition = -1;
    for (int index = 0; index < document.lines().size(); ++index) {
        if (document.lines().at(index).text == QStringLiteral("+second added")) {
            secondAddition = index;
            break;
        }
    }
    QVERIFY(secondAddition >= 0);

    const QByteArray patch = document.patchForLines({secondAddition}, true);
    QVERIFY(!patch.isEmpty());
    const GitCommandResult applied = git_.applyPatch(patch, true, true);
    QVERIFY2(applied.succeeded(), qPrintable(applied.errorText()));

    const QString staged = git_.diff(QStringLiteral("lines.txt"), true, false).outputText();
    QVERIFY(staged.contains(QStringLiteral("+first added")));
    QVERIFY(!staged.contains(QStringLiteral("+second added")));
    QVERIFY(readFile(QStringLiteral("lines.txt")).contains(QStringLiteral("second added")));
}

bool TestGitClient::commitAll(const QString &message) {
    return git_.stageAll().succeeded() && git_.commit(message, false).succeeded();
}

bool TestGitClient::buildHistoryWithRenameAndDeletion() {
    if (!openTemporaryRepository()) {
        return false;
    }
    if (!QDir(git_.repositoryRoot()).mkpath(QStringLiteral("src"))
        || !QDir(git_.repositoryRoot()).mkpath(QStringLiteral("docs"))) {
        return false;
    }

    writeFile(QStringLiteral("src/old.txt"), QStringLiteral("one\n"));
    writeFile(QStringLiteral("docs/readme.md"), QStringLiteral("documentation\n"));
    if (!commitAll(QStringLiteral("First commit"))) {
        return false;
    }

    writeFile(QStringLiteral("src/old.txt"), QStringLiteral("one\ntwo\n"));
    if (!commitAll(QStringLiteral("Extend the file"))) {
        return false;
    }

    if (!git_.runCustom({QStringLiteral("mv"), QStringLiteral("src/old.txt"),
                         QStringLiteral("src/new.txt")}).succeeded()) {
        return false;
    }
    writeFile(QStringLiteral("src/new.txt"), QStringLiteral("one\ntwo\nthree\n"));
    if (!commitAll(QStringLiteral("Rename and extend"))) {
        return false;
    }

    return git_.runCustom({QStringLiteral("rm"), QStringLiteral("docs/readme.md")}).succeeded()
           && git_.commit(QStringLiteral("Drop the documentation"), false).succeeded();
}

void TestGitClient::followsFileHistoryAcrossRename() {
    QVERIFY(buildHistoryWithRenameAndDeletion());

    const QList<GitFileRevision> revisions = git_.fileHistory(QStringLiteral("src/new.txt"), {});

    QCOMPARE(revisions.size(), 3);
    QCOMPARE(revisions.at(0).commit.subject, QStringLiteral("Rename and extend"));
    QVERIFY(revisions.at(0).isRename());
    QCOMPARE(revisions.at(0).previousPath, QStringLiteral("src/old.txt"));
    QCOMPARE(revisions.at(0).path, QStringLiteral("src/new.txt"));

    // Below the rename the file has to be read by its former path.
    QCOMPARE(revisions.at(1).path, QStringLiteral("src/old.txt"));
    QCOMPARE(revisions.at(2).path, QStringLiteral("src/old.txt"));
    QVERIFY(revisions.at(2).isAddition());
}

void TestGitClient::readsTreeLevelsAtRevision() {
    QVERIFY(buildHistoryWithRenameAndDeletion());

    const QList<GitTreeEntry> root = git_.treeEntries(QStringLiteral("HEAD"), {});
    QCOMPARE(root.size(), 1);
    QCOMPARE(root.constFirst().name, QStringLiteral("src"));
    QVERIFY(root.constFirst().directory);

    const QList<GitTreeEntry> source = git_.treeEntries(QStringLiteral("HEAD"),
                                                        QStringLiteral("src"));
    QCOMPARE(source.size(), 1);
    QCOMPARE(source.constFirst().path, QStringLiteral("src/new.txt"));
    QVERIFY(!source.constFirst().directory);
    QCOMPARE(source.constFirst().size, 14);
}

void TestGitClient::reachesDeletedFilesThroughOlderRevisions() {
    QVERIFY(buildHistoryWithRenameAndDeletion());

    // The file is gone from HEAD but the revision that still held it lists it.
    QVERIFY(git_.treeEntries(QStringLiteral("HEAD"), QStringLiteral("docs")).isEmpty());

    const QList<GitTreeEntry> documents = git_.treeEntries(QStringLiteral("HEAD~1"),
                                                           QStringLiteral("docs"));
    QCOMPARE(documents.size(), 1);
    QCOMPARE(documents.constFirst().path, QStringLiteral("docs/readme.md"));

    const GitCommandResult content = git_.fileContent(QStringLiteral("HEAD~1"),
                                                      QStringLiteral("docs/readme.md"));
    QVERIFY2(content.succeeded(), qPrintable(content.errorText()));
    QCOMPARE(content.outputText(), QStringLiteral("documentation\n"));
}

void TestGitClient::readsHistoricalContentWithoutTouchingWorkingTree() {
    QVERIFY(buildHistoryWithRenameAndDeletion());

    const GitCommandResult first = git_.fileContent(QStringLiteral("HEAD~3"),
                                                    QStringLiteral("src/old.txt"));
    QVERIFY2(first.succeeded(), qPrintable(first.errorText()));
    QCOMPARE(first.outputText(), QStringLiteral("one\n"));
    QCOMPARE(git_.fileSize(QStringLiteral("HEAD~3"), QStringLiteral("src/old.txt")), 4);

    QCOMPARE(readFile(QStringLiteral("src/new.txt")), QStringLiteral("one\ntwo\nthree\n"));
    QVERIFY(git_.status().isEmpty());
    QCOMPARE(git_.fileSize(QStringLiteral("HEAD"), QStringLiteral("src/old.txt")), -1);
}

void TestGitClient::comparesVersionsThatMovedBetweenPaths() {
    QVERIFY(buildHistoryWithRenameAndDeletion());

    const GitCommandResult patch = git_.fileDiff(QStringLiteral("HEAD~3"),
                                                 QStringLiteral("src/old.txt"),
                                                 QStringLiteral("HEAD"),
                                                 QStringLiteral("src/new.txt"));
    QVERIFY2(patch.succeeded(), qPrintable(patch.errorText()));

    const QString text = patch.outputText();
    QVERIFY(text.contains(QStringLiteral("--- a/src/old.txt")));
    QVERIFY(text.contains(QStringLiteral("+++ b/src/new.txt")));
    QVERIFY(text.contains(QStringLiteral("+two")));
    QVERIFY(text.contains(QStringLiteral("+three")));
}

void TestGitClient::listsWorkingCopyLevelWithSizes() {
    QVERIFY(buildHistoryWithRenameAndDeletion());
    writeFile(QStringLiteral("src/new.txt"), QStringLiteral("one\ntwo\nthree\nfour\n"));

    const QList<GitTreeEntry> root = git_.treeEntries({}, {});
    QCOMPARE(root.size(), 1);
    QCOMPARE(root.constFirst().name, QStringLiteral("src"));
    QVERIFY(root.constFirst().directory);

    // The working copy reports the file on disk, not the committed blob.
    const QList<GitTreeEntry> source = git_.treeEntries({}, QStringLiteral("src"));
    QCOMPARE(source.size(), 1);
    QCOMPARE(source.constFirst().path, QStringLiteral("src/new.txt"));
    QCOMPARE(source.constFirst().size, 19);
}

void TestGitClient::listsEveryTrackedFileOfARevision() {
    QVERIFY(buildHistoryWithRenameAndDeletion());

    const QList<GitTreeEntry> head = git_.allFiles(QStringLiteral("HEAD"));
    QCOMPARE(head.size(), 1);
    QCOMPARE(head.constFirst().path, QStringLiteral("src/new.txt"));
    QCOMPARE(head.constFirst().name, QStringLiteral("new.txt"));

    // The revision that still held the documentation reports both files, in
    // path order and without directory entries of their own.
    const QList<GitTreeEntry> earlier = git_.allFiles(QStringLiteral("HEAD~1"));
    QCOMPARE(earlier.size(), 2);
    QCOMPARE(earlier.at(0).path, QStringLiteral("docs/readme.md"));
    QCOMPARE(earlier.at(1).path, QStringLiteral("src/new.txt"));
    QVERIFY(!earlier.at(0).directory);

    const QList<GitTreeEntry> working = git_.allFiles({});
    QCOMPARE(working.size(), 1);
    QCOMPARE(working.constFirst().path, QStringLiteral("src/new.txt"));
}

QTEST_GUILESS_MAIN(TestGitClient)

#include "tst_gitclient.moc"
