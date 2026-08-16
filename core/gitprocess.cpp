// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "gitprocess.h"

#include "platform/platformservices.h"

#include <QProcess>
#include <QProcessEnvironment>

namespace {
constexpr int GitStartTimeoutMs = 5'000;
}

GitProcessRunner::~GitProcessRunner() = default;

QString GitProcess::executable() {
    return PlatformServices::instance().gitExecutable();
}

GitCommandResult GitProcess::run(const QString &workingDirectory, const QStringList &arguments,
                                 const int timeoutMs, const QByteArray *input) {
    GitCommandResult result;
    const QString program = executable();
    if (program.isEmpty()) {
        result.processError = tr("Git was not found. Install Git and add it to the system "
                                 "PATH.");
        GitLog::instance()->record(workingDirectory, arguments, result);
        return result;
    }

    QProcess process;
    process.setProgram(program);
    process.setWorkingDirectory(workingDirectory);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // Never let git block the UI waiting for an interactive credential prompt.
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    process.setProcessEnvironment(environment);

    QStringList completeArguments{
        QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
        QStringLiteral("-c"), QStringLiteral("color.ui=false"),
        QStringLiteral("-c"), QStringLiteral("advice.detachedHead=false")
    };
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

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        result.processError = tr("Git did not finish the operation in time.");
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

GitLog::GitLog(QObject *parent)
    : QObject(parent) {
}

GitLog *GitLog::instance() {
    static GitLog log;
    return &log;
}

void GitLog::record(const QString &workingDirectory, const QStringList &arguments,
                    const GitCommandResult &result) {
    Q_EMIT commandRecorded(workingDirectory,
                           QStringLiteral("git %1").arg(arguments.join(u' ')),
                           result.reportText(), result.succeeded());
}
