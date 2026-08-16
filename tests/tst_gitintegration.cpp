// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

// Drives the real Git executable over throwaway repositories. A bare
// repository on disk stands in for the remote, so the release smoke run needs
// neither network access nor credentials.

#include "core/gitclient.h"
#include "core/gitprocess.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

namespace {

void writeFileAt(const QString &directory, const QString &name, const QString &contents) {
    QFile file(QDir(directory).filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream stream(&file);
    stream << contents;
}

// Identity has to be set explicitly: the machine running the suite may have none.
void configureIdentity(const GitClient &git) {
    QVERIFY(git.runCustom({QStringLiteral("config"), QStringLiteral("user.name"),
                           QStringLiteral("Test")}).succeeded());
    QVERIFY(git.runCustom({QStringLiteral("config"), QStringLiteral("user.email"),
                           QStringLiteral("test@example.com")}).succeeded());
}

// macOS stores names decomposed and hands them back that way, so the same name
// can arrive spelled differently from the one that was written. The round trip
// is what matters here, not which form the filesystem chose.
[[nodiscard]] QString normalized(const QString &path) {
    return path.normalized(QString::NormalizationForm_C);
}

[[nodiscard]] GitFileStatus findStatus(const QList<GitFileStatus> &files, const QString &path) {
    for (const GitFileStatus &file : files) {
        if (normalized(file.path) == normalized(path)) {
            return file;
        }
    }
    return {};
}

}

class TestGitIntegration final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void runsTheReleaseSmokeScenario();
    void reportsAndResolvesAMergeConflict();
    void abortsAMergeConflict();
    void reportsAndContinuesARebaseConflict();
    void abortsARebaseConflict();
    void reportsAndContinuesACherryPickConflict();
    void abortsACherryPickConflict();
    void reportsAndContinuesARevertConflict();
    void abortsARevertConflict();
    void readsAwkwardFileNames_data();
    void readsAwkwardFileNames();
    void reportsRenamesWithTheOriginalPath();
    void stopsACommandThroughTheCancellationToken();
    void reportsTheFailingCommandAndExitCode();

private:
    [[nodiscard]] QString workPath() const;
    [[nodiscard]] QString remotePath() const;
    [[nodiscard]] GitClient workRepository() const;
    void writeWorkFile(const QString &name, const QString &contents) const;
    /// Leaves the working repository on @p branch with @p file diverged from main.
    void divergeBranch(const QString &branch, const QString &file,
                       const QString &mine, const QString &theirs) const;

    QTemporaryDir *root_ = nullptr;
};

void TestGitIntegration::initTestCase() {
    if (GitClient::gitExecutable().isEmpty()) {
        QSKIP("git is not installed, the integration suite cannot run");
    }
}

QString TestGitIntegration::workPath() const {
    return QDir(root_->path()).filePath(QStringLiteral("work"));
}

QString TestGitIntegration::remotePath() const {
    return QDir(root_->path()).filePath(QStringLiteral("remote.git"));
}

GitClient TestGitIntegration::workRepository() const {
    GitClient git;
    [[maybe_unused]] const GitCommandResult opened = git.openRepository(workPath());
    return git;
}

void TestGitIntegration::writeWorkFile(const QString &name, const QString &contents) const {
    writeFileAt(workPath(), name, contents);
}

void TestGitIntegration::init() {
    root_ = new QTemporaryDir;
    QVERIFY(root_->isValid());

    QVERIFY(GitClient::initRepository(remotePath(), true).succeeded());
    QVERIFY(GitClient::initRepository(workPath(), false).succeeded());

    const GitClient git = workRepository();
    QVERIFY(git.hasRepository());
    configureIdentity(git);

    writeFileAt(workPath(), QStringLiteral("README.md"), QStringLiteral("start\n"));
    QVERIFY(git.stage({QStringLiteral("README.md")}).succeeded());
    QVERIFY(git.commit(QStringLiteral("First commit"), false).succeeded());
    QVERIFY(git.addRemote(QStringLiteral("origin"), remotePath()).succeeded());
    QVERIFY(git.push(QStringLiteral("origin"), {git.currentBranch()}, true, false, false)
                .succeeded());
}

void TestGitIntegration::cleanup() {
    delete root_;
    root_ = nullptr;
}

// The scenario worth running before every release candidate.
void TestGitIntegration::runsTheReleaseSmokeScenario() {
    const GitClient git = workRepository();
    const QString main = git.currentBranch();
    QVERIFY(!main.isEmpty());

    // Modify and stage.
    writeWorkFile(QStringLiteral("README.md"), QStringLiteral("start\nsecond line\n"));
    QCOMPARE(findStatus(git.status(), QStringLiteral("README.md")).workTreeStatus, u'M');
    QVERIFY(git.stage({QStringLiteral("README.md")}).succeeded());
    QVERIFY(findStatus(git.status(), QStringLiteral("README.md")).hasStagedChanges());

    // Commit.
    QVERIFY(git.commit(QStringLiteral("Extend the readme"), false).succeeded());
    QVERIFY(git.status().isEmpty());
    QCOMPARE(git.lastCommitMessage().trimmed(), QStringLiteral("Extend the readme"));

    // Branch and merge back.
    QVERIFY(git.createBranch(QStringLiteral("feature"), main, true).succeeded());
    QCOMPARE(git.currentBranch(), QStringLiteral("feature"));
    writeWorkFile(QStringLiteral("feature.txt"), QStringLiteral("feature\n"));
    QVERIFY(git.stage({QStringLiteral("feature.txt")}).succeeded());
    QVERIFY(git.commit(QStringLiteral("Add a feature"), false).succeeded());
    QVERIFY(git.checkoutBranch(main).succeeded());
    QVERIFY(git.merge(QStringLiteral("feature"), true, false, true).succeeded());
    QVERIFY(!git.repositoryState().isBusy());

    // Push, then fetch and pull the round trip back.
    QVERIFY(git.push(QStringLiteral("origin"), {main}, false, false, false).succeeded());
    QVERIFY(git.fetch(QStringLiteral("origin"), true, true).succeeded());
    QVERIFY(git.pull(QStringLiteral("origin"), main, false).succeeded());

    // The remote really holds the merge.
    GitClient clone;
    const QString clonePath = QDir(root_->path()).filePath(QStringLiteral("clone"));
    QVERIFY(GitClient::initRepository(clonePath, false).succeeded());
    QVERIFY(clone.openRepository(clonePath).succeeded());
    configureIdentity(clone);
    QVERIFY(clone.addRemote(QStringLiteral("origin"), remotePath()).succeeded());
    QVERIFY(clone.fetch(QStringLiteral("origin"), false, true).succeeded());
    QVERIFY(clone.checkoutRemoteBranch(QStringLiteral("origin/") + main, main).succeeded());
    QVERIFY(QFile::exists(QDir(clonePath).filePath(QStringLiteral("feature.txt"))));
}

void TestGitIntegration::divergeBranch(const QString &branch, const QString &file,
                                       const QString &mine, const QString &theirs) const {
    const GitClient git = workRepository();
    const QString main = git.currentBranch();

    QVERIFY(git.createBranch(branch, main, true).succeeded());
    writeWorkFile(file, theirs);
    QVERIFY(git.stage({file}).succeeded());
    QVERIFY(git.commit(QStringLiteral("Their change"), false).succeeded());

    QVERIFY(git.checkoutBranch(main).succeeded());
    writeWorkFile(file, mine);
    QVERIFY(git.stage({file}).succeeded());
    QVERIFY(git.commit(QStringLiteral("My change"), false).succeeded());
}

void TestGitIntegration::reportsAndResolvesAMergeConflict() {
    divergeBranch(QStringLiteral("other"), QStringLiteral("shared.txt"),
                  QStringLiteral("mine\n"), QStringLiteral("theirs\n"));

    const GitClient git = workRepository();
    QVERIFY(!git.merge(QStringLiteral("other"), false, false, true).succeeded());

    // The view must not be able to mistake this for an ordinary repository.
    const GitRepositoryState state = git.repositoryState();
    QVERIFY(state.merging);
    QVERIFY(state.isBusy());
    QVERIFY(!state.description().isEmpty());

    const GitFileStatus conflicted = findStatus(git.status(), QStringLiteral("shared.txt"));
    QVERIFY(conflicted.isConflicted());

    QVERIFY(git.resolveWith({QStringLiteral("shared.txt")}, true).succeeded());
    QVERIFY(git.stage({QStringLiteral("shared.txt")}).succeeded());
    QVERIFY(git.commit(QStringLiteral("Resolve the conflict"), false).succeeded());

    QVERIFY(!git.repositoryState().isBusy());
    QVERIFY(git.status().isEmpty());
}

void TestGitIntegration::abortsAMergeConflict() {
    divergeBranch(QStringLiteral("other"), QStringLiteral("shared.txt"),
                  QStringLiteral("mine\n"), QStringLiteral("theirs\n"));

    const GitClient git = workRepository();
    const QString headBeforeMerge = git.headHash();
    QVERIFY(!git.merge(QStringLiteral("other"), false, false, true).succeeded());
    QVERIFY(git.repositoryState().merging);

    QVERIFY(git.abortOperation().succeeded());
    QVERIFY(!git.repositoryState().isBusy());
    QCOMPARE(git.headHash(), headBeforeMerge);
    QVERIFY(git.status().isEmpty());
}

// Every unfinished operation must be recognisable, or the interface shows an
// ordinary repository while Git sits in the middle of something.
void TestGitIntegration::reportsAndContinuesARebaseConflict() {
    divergeBranch(QStringLiteral("other"), QStringLiteral("shared.txt"),
                  QStringLiteral("mine\n"), QStringLiteral("theirs\n"));

    const GitClient git = workRepository();
    QVERIFY(!git.rebase(QStringLiteral("other")).succeeded());

    const GitRepositoryState state = git.repositoryState();
    QVERIFY(state.rebasing);
    QVERIFY(state.isBusy());
    QVERIFY(findStatus(git.status(), QStringLiteral("shared.txt")).isConflicted());

    QVERIFY(git.resolveWith({QStringLiteral("shared.txt")}, true).succeeded());
    QVERIFY(git.stage({QStringLiteral("shared.txt")}).succeeded());
    QVERIFY(git.continueOperation().succeeded());
    QVERIFY(!git.repositoryState().isBusy());
}

void TestGitIntegration::abortsARebaseConflict() {
    divergeBranch(QStringLiteral("other"), QStringLiteral("shared.txt"),
                  QStringLiteral("mine\n"), QStringLiteral("theirs\n"));

    const GitClient git = workRepository();
    const QString headBefore = git.headHash();
    QVERIFY(!git.rebase(QStringLiteral("other")).succeeded());
    QVERIFY(git.repositoryState().rebasing);

    QVERIFY(git.abortOperation().succeeded());
    QVERIFY(!git.repositoryState().isBusy());
    QCOMPARE(git.headHash(), headBefore);
}

void TestGitIntegration::reportsAndContinuesACherryPickConflict() {
    divergeBranch(QStringLiteral("other"), QStringLiteral("shared.txt"),
                  QStringLiteral("mine\n"), QStringLiteral("theirs\n"));

    const GitClient git = workRepository();
    const QString theirs = git.runCustom({QStringLiteral("rev-parse"),
                                          QStringLiteral("other")}).outputText().trimmed();
    QVERIFY(!theirs.isEmpty());
    QVERIFY(!git.cherryPick(theirs).succeeded());

    QVERIFY(git.repositoryState().cherryPicking);
    QVERIFY(git.repositoryState().isBusy());
    QVERIFY(findStatus(git.status(), QStringLiteral("shared.txt")).isConflicted());

    QVERIFY(git.resolveWith({QStringLiteral("shared.txt")}, false).succeeded());
    QVERIFY(git.stage({QStringLiteral("shared.txt")}).succeeded());
    QVERIFY(git.continueOperation().succeeded());
    QVERIFY(!git.repositoryState().isBusy());
}

void TestGitIntegration::abortsACherryPickConflict() {
    divergeBranch(QStringLiteral("other"), QStringLiteral("shared.txt"),
                  QStringLiteral("mine\n"), QStringLiteral("theirs\n"));

    const GitClient git = workRepository();
    const QString headBefore = git.headHash();
    const QString theirs = git.runCustom({QStringLiteral("rev-parse"),
                                          QStringLiteral("other")}).outputText().trimmed();
    QVERIFY(!git.cherryPick(theirs).succeeded());
    QVERIFY(git.repositoryState().cherryPicking);

    QVERIFY(git.abortOperation().succeeded());
    QVERIFY(!git.repositoryState().isBusy());
    QCOMPARE(git.headHash(), headBefore);
    QVERIFY(git.status().isEmpty());
}

void TestGitIntegration::reportsAndContinuesARevertConflict() {
    const GitClient git = workRepository();

    // Reverting a commit that a later one has since rewritten conflicts over
    // the contents, leaving all three stages in the index.
    const auto commitContents = [&git, this](const QString &contents, const QString &message) {
        writeWorkFile(QStringLiteral("shared.txt"), contents);
        QVERIFY(git.stage({QStringLiteral("shared.txt")}).succeeded());
        QVERIFY(git.commit(message, false).succeeded());
    };

    commitContents(QStringLiteral("one\n"), QStringLiteral("First shared"));
    QVERIFY(!QTest::currentTestFailed());
    commitContents(QStringLiteral("two\n"), QStringLiteral("Second shared"));
    QVERIFY(!QTest::currentTestFailed());
    const QString target = git.headHash();
    commitContents(QStringLiteral("three\n"), QStringLiteral("Third shared"));
    QVERIFY(!QTest::currentTestFailed());

    QVERIFY(!git.revert(target).succeeded());
    QVERIFY(git.repositoryState().reverting);
    QVERIFY(git.repositoryState().isBusy());
    QVERIFY(findStatus(git.status(), QStringLiteral("shared.txt")).isConflicted());

    // The reverted side is the one carrying a change: resolving in favour of
    // "ours" would restore what is already committed and leave Git nothing to
    // record.
    QVERIFY(git.resolveWith({QStringLiteral("shared.txt")}, false).succeeded());
    QVERIFY(git.stage({QStringLiteral("shared.txt")}).succeeded());
    QVERIFY(git.continueOperation().succeeded());
    QVERIFY(!git.repositoryState().isBusy());
}

void TestGitIntegration::abortsARevertConflict() {
    const GitClient git = workRepository();

    writeWorkFile(QStringLiteral("shared.txt"), QStringLiteral("first\n"));
    QVERIFY(git.stage({QStringLiteral("shared.txt")}).succeeded());
    QVERIFY(git.commit(QStringLiteral("Add shared"), false).succeeded());
    const QString target = git.headHash();

    writeWorkFile(QStringLiteral("shared.txt"), QStringLiteral("second\n"));
    QVERIFY(git.stage({QStringLiteral("shared.txt")}).succeeded());
    QVERIFY(git.commit(QStringLiteral("Rewrite shared"), false).succeeded());
    const QString headBefore = git.headHash();

    QVERIFY(!git.revert(target).succeeded());
    QVERIFY(git.repositoryState().reverting);

    QVERIFY(git.abortOperation().succeeded());
    QVERIFY(!git.repositoryState().isBusy());
    QCOMPARE(git.headHash(), headBefore);
    QVERIFY(git.status().isEmpty());
}

void TestGitIntegration::readsAwkwardFileNames_data() {
    QTest::addColumn<QString>("name");

    QTest::newRow("space") << QStringLiteral("a file.txt");
    QTest::newRow("cyrillic") << QStringLiteral("файл.txt");
    QTest::newRow("emoji") << QStringLiteral("report 🐙.txt");
    QTest::newRow("dash prefix") << QStringLiteral("-leading-dash.txt");
    QTest::newRow("combining") << QStringLiteral("отчёт 🚀.txt");

#ifndef Q_OS_WIN
    // Windows forbids these outright, so there is nothing for Git to report.
    QTest::newRow("quote") << QStringLiteral("say \"hello\".txt");
    QTest::newRow("backslash") << QStringLiteral("back\\slash.txt");
    QTest::newRow("mixed") << QStringLiteral("отчёт \"v2\" 🚀.txt");
#endif
}

void TestGitIntegration::readsAwkwardFileNames() {
    QFETCH(QString, name);

    const GitClient git = workRepository();
    writeWorkFile(name, QStringLiteral("contents\n"));

    // Untracked, then staged, then committed: the path must survive each step
    // exactly as written, with no quoting and no escapes.
    const GitFileStatus untracked = findStatus(git.status(), name);
    QCOMPARE(normalized(untracked.path), normalized(name));
    QVERIFY(untracked.isUntracked());

    QVERIFY(git.stage({name}).succeeded());
    QVERIFY(findStatus(git.status(), name).hasStagedChanges());

    QVERIFY(git.commit(QStringLiteral("Add an awkward name"), false).succeeded());
    QVERIFY(git.status().isEmpty());

    const QList<GitChangedFile> changed = git.commitFiles(git.headHash());
    QCOMPARE(changed.size(), 1);
    QCOMPARE(normalized(changed.constFirst().path), normalized(name));
    QCOMPARE(changed.constFirst().status, u'A');
}

void TestGitIntegration::reportsRenamesWithTheOriginalPath() {
    const GitClient git = workRepository();
    const QString original = QStringLiteral("отчёт 🐙.txt");
    const QString renamed = QStringLiteral("отчёт 🐙 (v2).txt");

    writeWorkFile(original, QStringLiteral("some reasonably unique contents\n"));
    QVERIFY(git.stage({original}).succeeded());
    QVERIFY(git.commit(QStringLiteral("Add the report"), false).succeeded());

    QVERIFY(QFile::rename(QDir(workPath()).filePath(original),
                          QDir(workPath()).filePath(renamed)));
    QVERIFY(git.stage({original, renamed}).succeeded());

    const GitFileStatus status = findStatus(git.status(), renamed);
    QCOMPARE(status.indexStatus, u'R');
    QCOMPARE(normalized(status.originalPath), normalized(original));
}

void TestGitIntegration::stopsACommandThroughTheCancellationToken() {
    GitClient git = workRepository();
    const auto cancellation = std::make_shared<GitCancellation>();
    cancellation->cancel();
    git.setCancellation(cancellation);

    const GitCommandResult result = git.fetch(QStringLiteral("origin"), true, true);
    QVERIFY(!result.succeeded());
    QVERIFY(result.cancelled);
    // A cancelled command must not be reported to the user as a failure.
    QVERIFY(!result.errorText().isEmpty());

    // The token belongs to the client copy, so the untouched one still works.
    const GitClient other = workRepository();
    QVERIFY(other.fetch(QStringLiteral("origin"), true, true).succeeded());
}

void TestGitIntegration::reportsTheFailingCommandAndExitCode() {
    const GitClient git = workRepository();
    const GitCommandResult result = git.checkoutBranch(QStringLiteral("no-such-branch"));

    QVERIFY(!result.succeeded());
    QVERIFY(!result.cancelled);
    // What the error dialog needs in order to say something actionable.
    QVERIFY(result.command.startsWith(QStringLiteral("git ")));
    QVERIFY(result.command.contains(QStringLiteral("no-such-branch")));
    QVERIFY(result.exitCode > 0);
    QVERIFY(!result.errorText().isEmpty());
}

QTEST_MAIN(TestGitIntegration)

#include "tst_gitintegration.moc"
