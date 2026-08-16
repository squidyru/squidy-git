// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

// Everything that involves talking to a remote: the real smart HTTP protocol
// against a locally hosted repository, the credential helper protocol, and
// stopping a command that is already under way. No network and no account.

#include "core/gitaskpass.h"
#include "core/gitclient.h"
#include "core/gitprocess.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <memory>

namespace {

constexpr char BasicUser[] = "tester";
constexpr char BasicPassword[] = "s3cret-token";

void writeFileAt(const QString &directory, const QString &name, const QString &contents) {
    QFile file(QDir(directory).filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream stream(&file);
    stream << contents;
}

void configureIdentity(const GitClient &git) {
    QVERIFY(git.runCustom({QStringLiteral("config"), QStringLiteral("user.name"),
                           QStringLiteral("Test")}).succeeded());
    QVERIFY(git.runCustom({QStringLiteral("config"), QStringLiteral("user.email"),
                           QStringLiteral("test@example.com")}).succeeded());
}

/// Runs Git straight against @p directory. GitClient works through a working
/// tree, so a bare repository has to be configured this way.
bool runGitAt(const QString &directory, const QStringList &arguments) {
    QProcess process;
    process.setProgram(GitClient::gitExecutable());
    process.setArguments(QStringList{QStringLiteral("--git-dir"), directory} + arguments);
    process.start();
    return process.waitForFinished(30'000) && process.exitCode() == 0;
}

/// Waits for @p process while still serving the event loop, which a plain
/// waitForFinished would block and with it any server this test is running.
bool waitServingEvents(QProcess *process, const int timeoutMs) {
    return QTest::qWaitFor([process] { return process->state() == QProcess::NotRunning; },
                           timeoutMs);
}

/// Writes an executable script, used to stand in for ssh and for askpass.
bool writeScript(const QString &path, const QString &body) {
    QFile script(path);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream stream(&script);
    stream << QStringLiteral("#!/bin/sh\n") << body;
    script.close();
    return script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

}

class TestGitRemote final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void wiresBatchModeWithoutAHelper();
    void wiresTheHelperWhenOneIsConfigured();
    void answersThroughTheHelperProtocol();
    void reportsNoAnswerAsAFailure();

    void clonesFetchesAndPushesOverHttp();
    void authenticatesOverHttpThroughAskPass();
    void keepsTheTokenOutOfTheReport();

    void stopsACommandAlreadyUnderWay();

private:
    [[nodiscard]] QString path(const QString &name) const;
    /// Starts the bridge over @p repositoryRoot, returning its base URL.
    [[nodiscard]] QString startServer(const QString &repositoryRoot,
                                      const QString &credentials = {});
    /// Creates a bare repository holding one commit.
    void createRemote(const QString &directory);

    QTemporaryDir *root_ = nullptr;
    /// Why startServer() gave up, so a skip can say something useful.
    QString serverError_;
    /// The branch createRemote() seeded, whatever Git named it.
    QString seededBranch_;
    std::unique_ptr<QProcess> server_;
};

void TestGitRemote::initTestCase() {
    if (GitClient::gitExecutable().isEmpty()) {
        QSKIP("git is not installed, the remote suite cannot run");
    }
}

void TestGitRemote::init() {
    root_ = new QTemporaryDir;
    QVERIFY(root_->isValid());
}

void TestGitRemote::cleanup() {
    if (server_ != nullptr) {
        server_->terminate();
        if (!server_->waitForFinished(5'000)) {
            server_->kill();
            server_->waitForFinished(5'000);
        }
        server_.reset();
    }
    GitAskPass::instance().configure({}, {});
    delete root_;
    root_ = nullptr;
}

QString TestGitRemote::path(const QString &name) const {
    return QDir(root_->path()).filePath(name);
}

void TestGitRemote::createRemote(const QString &directory) {
    const QString work = path(QStringLiteral("seed"));
    QVERIFY(GitClient::initRepository(directory, true).succeeded());
    QVERIFY(GitClient::initRepository(work, false).succeeded());

    GitClient git;
    QVERIFY(git.openRepository(work).succeeded());
    configureIdentity(git);
    QVERIFY(!QTest::currentTestFailed());

    writeFileAt(work, QStringLiteral("README.md"), QStringLiteral("hosted\n"));
    QVERIFY(git.stage({QStringLiteral("README.md")}).succeeded());
    QVERIFY(git.commit(QStringLiteral("First commit"), false).succeeded());
    QVERIFY(git.addRemote(QStringLiteral("origin"), directory).succeeded());
    seededBranch_ = git.currentBranch();
    QVERIFY(!seededBranch_.isEmpty());
    QVERIFY(git.push(QStringLiteral("origin"), {seededBranch_}, true, false, false)
                .succeeded());

    // http-backend refuses a push unless the repository opts into it.
    QVERIFY(runGitAt(directory, {QStringLiteral("config"),
                                 QStringLiteral("http.receivepack"),
                                 QStringLiteral("true")}));
}

QString TestGitRemote::startServer(const QString &repositoryRoot, const QString &credentials) {
    const QString script = QStringLiteral(SQUIDYGIT_TEST_DATA_DIR "/githttpserver.py");
    if (!QFile::exists(script)) {
        serverError_ = QStringLiteral("the bridge script is missing: %1").arg(script);
        return {};
    }

    server_ = std::make_unique<QProcess>();
    QStringList arguments{script, repositoryRoot};
    if (!credentials.isEmpty()) {
        arguments.append(credentials);
    }
    server_->setProgram(QStringLiteral("python3"));
    server_->setArguments(arguments);
    server_->start();
    if (!server_->waitForStarted(10'000)) {
        serverError_ = QStringLiteral("python3 did not start: %1").arg(server_->errorString());
        server_.reset();
        return {};
    }

    // The first line is the port the operating system handed out.
    if (!server_->waitForReadyRead(10'000)) {
        serverError_ = QStringLiteral("the bridge printed no port: %1")
                           .arg(QString::fromUtf8(server_->readAllStandardError()).trimmed());
        server_.reset();
        return {};
    }
    const QString port = QString::fromUtf8(server_->readLine()).trimmed();
    if (port.isEmpty() || port.toInt() == 0) {
        serverError_ = QStringLiteral("the bridge printed \"%1\" instead of a port").arg(port);
        server_.reset();
        return {};
    }
    return QStringLiteral("http://127.0.0.1:%1").arg(port);
}

void TestGitRemote::wiresBatchModeWithoutAHelper() {
    GitAskPass::instance().configure({}, {});
    QVERIFY(!GitAskPass::instance().isAvailable());

    QProcessEnvironment environment;
    GitAskPass::instance().applyTo(environment);

    // Without somewhere to ask, ssh must fail rather than wait on a terminal.
    QVERIFY(environment.value(QStringLiteral("GIT_SSH_COMMAND"))
                .contains(QStringLiteral("BatchMode=yes")));
    QVERIFY(!environment.contains(QStringLiteral("SSH_ASKPASS")));
}

void TestGitRemote::wiresTheHelperWhenOneIsConfigured() {
    GitAskPass::instance().configure(QStringLiteral("/opt/squidygit"),
                                     QStringLiteral("socket-name"));
    QVERIFY(GitAskPass::instance().isAvailable());

    QProcessEnvironment environment;
    GitAskPass::instance().applyTo(environment);

    QCOMPARE(environment.value(QStringLiteral("GIT_ASKPASS")), QStringLiteral("/opt/squidygit"));
    QCOMPARE(environment.value(QStringLiteral("SSH_ASKPASS")), QStringLiteral("/opt/squidygit"));
    // Without force, ssh skips the helper whenever it can see a terminal.
    QCOMPARE(environment.value(QStringLiteral("SSH_ASKPASS_REQUIRE")), QStringLiteral("force"));
    QCOMPARE(environment.value(QString::fromLatin1(GitAskPassServerVariable)),
             QStringLiteral("socket-name"));
    // Batch mode here would stop ssh before it ever reached the helper.
    QVERIFY(!environment.contains(QStringLiteral("GIT_SSH_COMMAND")));
}

void TestGitRemote::answersThroughTheHelperProtocol() {
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    // Short on purpose: the resulting path has to fit the platform limit on a
    // socket address, which the temporary directory already eats into.
    const QString socketName = QStringLiteral("sqg-t-%1")
                                   .arg(QUuid::createUuid().toString(QUuid::Id128).left(12));
    QVERIFY2(server.listen(socketName), qPrintable(server.errorString()));

    QString received;
    connect(&server, &QLocalServer::newConnection, &server, [&server, &received] {
        QLocalSocket *socket = server.nextPendingConnection();
        QVERIFY(socket->waitForReadyRead(10'000));
        QByteArray request = socket->readAll();
        received = QString::fromUtf8(request.left(request.indexOf('\0')));

        QByteArray reply = QByteArray(BasicPassword);
        reply.append('\0');
        socket->write(reply);
        QVERIFY(socket->waitForBytesWritten(10'000));
        socket->disconnectFromServer();
    });

    QProcess helper;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QString::fromLatin1(GitAskPassServerVariable), server.fullServerName());
    helper.setProcessEnvironment(environment);
    helper.setProgram(QStringLiteral(SQUIDYGIT_APPLICATION_PATH));
    helper.setArguments({QStringLiteral("Password for 'https://tester@example.com': ")});
    helper.start();

    // The server above answers from this event loop, so the wait must serve it.
    QVERIFY(waitServingEvents(&helper, 30'000));
    QCOMPARE(helper.exitCode(), 0);
    QCOMPARE(QString::fromUtf8(helper.readAllStandardOutput()).trimmed(),
             QString::fromLatin1(BasicPassword));
    QVERIFY(received.startsWith(QStringLiteral("Password for")));
}

void TestGitRemote::reportsNoAnswerAsAFailure() {
    QProcess helper;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // A socket nobody is listening on stands in for a dismissed dialog.
    environment.insert(QString::fromLatin1(GitAskPassServerVariable),
                       QStringLiteral("squidygit-absent-socket"));
    helper.setProcessEnvironment(environment);
    helper.setProgram(QStringLiteral(SQUIDYGIT_APPLICATION_PATH));
    helper.setArguments({QStringLiteral("Password for 'https://example.com': ")});
    helper.start();

    QVERIFY(waitServingEvents(&helper, 30'000));
    // A non-zero exit is how a helper tells Git it has no answer.
    QVERIFY(helper.exitCode() != 0);
    QVERIFY(helper.readAllStandardOutput().isEmpty());
}

void TestGitRemote::clonesFetchesAndPushesOverHttp() {
    const QString hosted = path(QStringLiteral("hosted"));
    QVERIFY(QDir().mkpath(hosted));
    createRemote(QDir(hosted).filePath(QStringLiteral("repo.git")));
    QVERIFY(!QTest::currentTestFailed());

    const QString base = startServer(hosted);
    if (base.isEmpty()) {
        QSKIP(qPrintable(QStringLiteral("no HTTP bridge: %1").arg(serverError_)));
    }
    const QString url = base + QStringLiteral("/repo.git");

    const QString clone = path(QStringLiteral("clone"));
    QVERIFY(GitClient::initRepository(clone, false).succeeded());
    GitClient git;
    QVERIFY(git.openRepository(clone).succeeded());
    configureIdentity(git);
    QVERIFY(!QTest::currentTestFailed());

    QVERIFY(git.addRemote(QStringLiteral("origin"), url).succeeded());
    QVERIFY(git.fetch(QStringLiteral("origin"), false, true).succeeded());

    // Not hard coded: init.defaultBranch differs between installations.
    const QString branch = seededBranch_;
    const QString remoteBranch = QStringLiteral("origin/") + branch;
    QVERIFY(git.checkoutRemoteBranch(remoteBranch, branch).succeeded());
    QVERIFY(QFile::exists(QDir(clone).filePath(QStringLiteral("README.md"))));

    // Push a new commit back over the same protocol and read it again.
    writeFileAt(clone, QStringLiteral("added.txt"), QStringLiteral("over http\n"));
    QVERIFY(git.stage({QStringLiteral("added.txt")}).succeeded());
    QVERIFY(git.commit(QStringLiteral("Add over http"), false).succeeded());
    QVERIFY(git.push(QStringLiteral("origin"), {branch}, false, false, false).succeeded());
    QVERIFY(git.fetch(QStringLiteral("origin"), true, true).succeeded());
    QVERIFY(git.pull(QStringLiteral("origin"), branch, false).succeeded());
}

void TestGitRemote::authenticatesOverHttpThroughAskPass() {
    const QString hosted = path(QStringLiteral("hosted"));
    QVERIFY(QDir().mkpath(hosted));
    createRemote(QDir(hosted).filePath(QStringLiteral("repo.git")));
    QVERIFY(!QTest::currentTestFailed());

    const QString credentials = QStringLiteral("%1:%2")
                                    .arg(QString::fromLatin1(BasicUser),
                                         QString::fromLatin1(BasicPassword));
    const QString base = startServer(hosted, credentials);
    if (base.isEmpty()) {
        QSKIP(qPrintable(QStringLiteral("no HTTP bridge: %1").arg(serverError_)));
    }

    // The user name travels in the URL, so only the password is asked for.
    const QString url = QStringLiteral("http://%1@127.0.0.1:%2/repo.git")
                            .arg(QString::fromLatin1(BasicUser),
                                 base.section(u':', -1));

    const QString askpass = path(QStringLiteral("askpass.sh"));
    QVERIFY(writeScript(askpass, QStringLiteral("echo '%1'\n")
                                     .arg(QString::fromLatin1(BasicPassword))));
    qputenv("GIT_ASKPASS", askpass.toLocal8Bit());

    const QString clone = path(QStringLiteral("authclone"));
    QVERIFY(GitClient::initRepository(clone, false).succeeded());
    GitClient git;
    QVERIFY(git.openRepository(clone).succeeded());
    QVERIFY(git.addRemote(QStringLiteral("origin"), url).succeeded());

    const GitCommandResult fetched = git.fetch(QStringLiteral("origin"), false, true);
    qunsetenv("GIT_ASKPASS");
    QVERIFY2(fetched.succeeded(), qPrintable(fetched.errorText()));
}

void TestGitRemote::keepsTheTokenOutOfTheReport() {
    const QString clone = path(QStringLiteral("leak"));
    QVERIFY(GitClient::initRepository(clone, false).succeeded());
    GitClient git;
    QVERIFY(git.openRepository(clone).succeeded());

    // Nothing listens on this port, so the attempt fails and git echoes the
    // URL it tried back at us.
    const QString url = QStringLiteral("http://%1:%2@127.0.0.1:1/repo.git")
                            .arg(QString::fromLatin1(BasicUser),
                                 QString::fromLatin1(BasicPassword));
    QVERIFY(git.addRemote(QStringLiteral("origin"), url).succeeded());

    const GitCommandResult result = git.fetch(QStringLiteral("origin"), false, true);
    QVERIFY(!result.succeeded());
    QVERIFY(!result.reportText().contains(QString::fromLatin1(BasicPassword)));
    QVERIFY(!result.errorText().contains(QString::fromLatin1(BasicPassword)));
    QVERIFY(!result.command.contains(QString::fromLatin1(BasicPassword)));
}

// The existing coverage only trips the flag before the process starts. Here
// the command is already running, which is the case that matters when a
// remote stops answering.
void TestGitRemote::stopsACommandAlreadyUnderWay() {
#ifdef Q_OS_WIN
    QSKIP("the stand-in transport is a shell script");
#endif
    const QString clone = path(QStringLiteral("slow"));
    QVERIFY(GitClient::initRepository(clone, false).succeeded());

    // A transport that never answers, standing in for an unreachable host.
    const QString sleeper = path(QStringLiteral("slowssh.sh"));
    QVERIFY(writeScript(sleeper, QStringLiteral("sleep 120\n")));
    // Set on this process, so gitEnvironment() carries it through and leaves
    // it alone rather than substituting batch mode.
    qputenv("GIT_SSH_COMMAND", sleeper.toLocal8Bit());

    GitClient git;
    QVERIFY(git.openRepository(clone).succeeded());
    QVERIFY(git.addRemote(QStringLiteral("origin"),
                          QStringLiteral("ssh://example.invalid/repo.git")).succeeded());

    const auto cancellation = std::make_shared<GitCancellation>();
    git.setCancellation(cancellation);

    // Cancel once the command is genuinely in flight.
    QTimer::singleShot(500, [cancellation] { cancellation->cancel(); });

    QElapsedTimer elapsed;
    elapsed.start();
    GitCommandResult result;
    {
        // fetch() blocks, so it runs where the timer above is not needed to be
        // serviced: the flag is set from this thread's event loop before the
        // call, through a worker that watches the clock.
        QScopedPointer<QThread> worker(QThread::create([cancellation] {
            QThread::msleep(500);
            cancellation->cancel();
        }));
        worker->start();
        result = git.fetch(QStringLiteral("origin"), false, true);
        worker->wait();
    }
    const qint64 elapsedMs = elapsed.elapsed();
    qunsetenv("GIT_SSH_COMMAND");

    QVERIFY(!result.succeeded());
    QVERIFY2(result.cancelled, qPrintable(result.errorText()));
    // Far below both the sleeping transport and the network timeout.
    QVERIFY2(elapsedMs < 30'000, qPrintable(QStringLiteral("took %1 ms").arg(elapsedMs)));
}

QTEST_MAIN(TestGitRemote)

#include "tst_gitremote.moc"
