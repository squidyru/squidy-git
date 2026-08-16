// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "gitprocess.h"

#include "gitaskpass.h"
#include "gitredact.h"
#include "platform/platformservices.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>

namespace {
constexpr int GitStartTimeoutMs = 5'000;
// How long the wait loop sleeps between looks at the cancellation flag.
constexpr int CancellationPollMs = 100;
// Reaping a killed process waits on its pipes, which stay open while anything
// it spawned still holds them. Bounded, because the default is half a minute
// and the command is already on its way out.
constexpr int KilledProcessReapMs = 2'000;
}

void GitCancellation::cancel() {
    cancelled_.storeRelease(1);
}

bool GitCancellation::isCancelled() const {
    return cancelled_.loadAcquire() != 0;
}

GitProcessRunner::~GitProcessRunner() = default;

QProcessEnvironment gitEnvironment() {
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // Never block waiting for a prompt on a terminal we do not own.
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    GitAskPass::instance().applyTo(environment);
    return environment;
}

QStringList gitConfigArguments() {
    return {
        QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
        QStringLiteral("-c"), QStringLiteral("color.ui=false"),
        QStringLiteral("-c"), QStringLiteral("advice.detachedHead=false")
    };
}

QString GitProcess::executable() {
    return PlatformServices::instance().gitExecutable();
}

GitCommandResult GitProcess::run(const QString &workingDirectory, const QStringList &arguments,
                                 const int timeoutMs, const QByteArray *input,
                                 const GitCancellationPtr &cancellation) {
    GitCommandResult result;
    result.command = QStringLiteral("git %1").arg(redactCredentials(arguments).join(u' '));

    const QString program = executable();
    if (program.isEmpty()) {
        result.processError = tr("Git was not found. Install Git and add it to the system "
                                 "PATH.");
        GitLog::instance()->record(workingDirectory, arguments, result);
        return result;
    }

    if (cancellation != nullptr && cancellation->isCancelled()) {
        result.cancelled = true;
        result.processError = tr("The operation was cancelled.");
        GitLog::instance()->record(workingDirectory, arguments, result);
        return result;
    }

    QProcess process;
    process.setProgram(program);
    process.setWorkingDirectory(workingDirectory);

    process.setProcessEnvironment(gitEnvironment());

    QStringList completeArguments = gitConfigArguments();
    completeArguments.append(arguments);
    process.setArguments(completeArguments);
    process.start();

    if (!process.waitForStarted(GitStartTimeoutMs)) {
        result.processError = process.errorString();
        GitLog::instance()->record(workingDirectory, arguments, result);
        return result;
    }

    if (input != nullptr) {
        process.write(*input);
    }
    process.closeWriteChannel();

    // Waiting in slices keeps the cancellation flag observable; one
    // waitForFinished(timeoutMs) would hold the command until it expired.
    QElapsedTimer elapsed;
    elapsed.start();
    while (!process.waitForFinished(CancellationPollMs)) {
        const bool cancelled = cancellation != nullptr && cancellation->isCancelled();
        if (!cancelled && !elapsed.hasExpired(timeoutMs)) {
            continue;
        }

        process.kill();
        process.waitForFinished(KilledProcessReapMs);
        result.cancelled = cancelled;
        result.processError = cancelled
                                  ? tr("The operation was cancelled.")
                                  : tr("Git did not finish the operation in time.");
        GitLog::instance()->record(workingDirectory, arguments, result);
        return result;
    }

    result.exitCode = process.exitCode();
    result.output = process.readAllStandardOutput();
    result.errorOutput = process.readAllStandardError();
    GitLog::instance()->record(workingDirectory, arguments, result);
    return result;
}

GitProcessRunner *defaultGitRunner() {
    static GitProcess runner;
    return &runner;
}

GitStreamingProcess::GitStreamingProcess(QObject *parent)
    : QObject(parent) {
}

GitStreamingProcess::~GitStreamingProcess() = default;

QString GitStreamingProcess::describe(const QStringList &arguments) {
    return QStringLiteral("git %1").arg(redactCredentials(arguments).join(u' '));
}

bool GitStreamingProcess::isRunning() const {
    return process_ != nullptr && process_->state() != QProcess::NotRunning;
}

void GitStreamingProcess::start(const QString &workingDirectory, const QStringList &arguments) {
    if (isRunning()) {
        return;
    }

    arguments_ = arguments;
    workingDirectory_ = workingDirectory;
    collectedOutput_.clear();
    cancelled_ = false;

    const QString program = GitProcess::executable();
    if (program.isEmpty()) {
        GitCommandResult result;
        result.command = describe(arguments);
        result.processError = tr("Git was not found. Install Git and add it to the system "
                                 "PATH.");
        GitLog::instance()->record(workingDirectory, arguments, result);
        Q_EMIT finished(result);
        return;
    }

    process_ = new QProcess(this);
    process_->setProgram(program);
    process_->setWorkingDirectory(workingDirectory);
    process_->setProcessEnvironment(gitEnvironment());

    QStringList completeArguments = gitConfigArguments();
    completeArguments.append(arguments);
    process_->setArguments(completeArguments);

    const auto forward = [this](const QByteArray &chunk) {
        collectedOutput_.append(chunk);
        // Stripped at the source, not where it is shown: a caller cannot
        // forget to.
        Q_EMIT outputReceived(redactCredentials(QString::fromUtf8(chunk)));
    };
    connect(process_, &QProcess::readyReadStandardOutput, this,
            [this, forward] { forward(process_->readAllStandardOutput()); });
    connect(process_, &QProcess::readyReadStandardError, this,
            [this, forward] { forward(process_->readAllStandardError()); });
    connect(process_, &QProcess::finished, this,
            [this](const int exitCode) { reportFinished(exitCode); });
    connect(process_, &QProcess::errorOccurred, this, [this] {
        if (process_->state() == QProcess::NotRunning) {
            reportFinished(-1);
        }
    });

    process_->start();
}

void GitStreamingProcess::cancel() {
    if (!isRunning()) {
        return;
    }
    cancelled_ = true;
    process_->kill();
}

void GitStreamingProcess::reportFinished(const int exitCode) {
    // A crash reports through both channels; whichever arrives first wins.
    if (process_ == nullptr) {
        return;
    }

    GitCommandResult result;
    result.command = describe(arguments_);
    result.exitCode = exitCode;
    result.output = collectedOutput_;
    result.cancelled = cancelled_;
    if (cancelled_) {
        result.processError = tr("The operation was cancelled.");
    } else if (exitCode < 0) {
        result.processError = process_ != nullptr ? process_->errorString() : QString();
    }

    GitLog::instance()->record(workingDirectory_, arguments_, result);
    if (process_ != nullptr) {
        process_->deleteLater();
        process_ = nullptr;
    }
    Q_EMIT finished(result);
}

GitLog::GitLog(QObject *parent)
    : QObject(parent) {
}

GitLog *GitLog::instance() {
    static GitLog log;
    return &log;
}

void GitLog::record(const QString &workingDirectory, const QStringList &arguments,
                    const GitCommandResult &result) {
    // A remote URL carries a token into both the arguments and the output.
    Q_EMIT commandRecorded(workingDirectory,
                           QStringLiteral("git %1").arg(redactCredentials(arguments).join(u' ')),
                           result.reportText(), result.succeeded());
}
