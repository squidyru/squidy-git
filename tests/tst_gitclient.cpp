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

private:
    [[nodiscard]] bool openTemporaryRepository();
    void writeFile(const QString &name, const QString &contents) const;
    [[nodiscard]] QString readFile(const QString &name) const;

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

QTEST_GUILESS_MAIN(TestGitClient)

#include "tst_gitclient.moc"
