// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include "gittypes.h"

#include <QAtomicInteger>
#include <QCoreApplication>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <memory>

class QProcess;

/// Shared between the thread running a Git command and the one stopping it.
/// Cancelling kills the process, the only way out of a command on the network.
class GitCancellation {
public:
    void cancel();
    [[nodiscard]] bool isCancelled() const;

private:
    QAtomicInteger<int> cancelled_ = 0;
};

using GitCancellationPtr = std::shared_ptr<GitCancellation>;

/// The environment every invocation of Git runs with. Shared so that no code
/// path can quietly opt out of it.
[[nodiscard]] QProcessEnvironment gitEnvironment();

/// The -c options that keep the output machine readable whatever the user has
/// configured.
[[nodiscard]] QStringList gitConfigArguments();

/// Process boundary used by GitClient and command tests.
class GitProcessRunner {
public:
    GitProcessRunner() = default;
    virtual ~GitProcessRunner();

    GitProcessRunner(const GitProcessRunner &) = delete;
    GitProcessRunner &operator=(const GitProcessRunner &) = delete;

    [[nodiscard]] virtual GitCommandResult run(const QString &workingDirectory,
                                               const QStringList &arguments,
                                               int timeoutMs,
                                               const QByteArray *input,
                                               const GitCancellationPtr &cancellation) = 0;
};

/// Runs the system Git executable.
class GitProcess final : public GitProcessRunner {
    Q_DECLARE_TR_FUNCTIONS(GitClient)

public:
    [[nodiscard]] GitCommandResult run(const QString &workingDirectory,
                                       const QStringList &arguments,
                                       int timeoutMs,
                                       const QByteArray *input,
                                       const GitCancellationPtr &cancellation) override;

    [[nodiscard]] static QString executable();
};

[[nodiscard]] GitProcessRunner *defaultGitRunner();

/// A Git invocation whose output is read while it runs, for the one case that
/// needs live progress: cloning. Same environment, configuration and command
/// log as GitProcess, with credentials stripped before the text is handed out.
class GitStreamingProcess final : public QObject {
    Q_OBJECT

public:
    explicit GitStreamingProcess(QObject *parent = nullptr);
    ~GitStreamingProcess() override;

    void start(const QString &workingDirectory, const QStringList &arguments);
    void cancel();
    [[nodiscard]] bool isRunning() const;
    /// The invocation as it is safe to display.
    [[nodiscard]] static QString describe(const QStringList &arguments);

Q_SIGNALS:
    /// Already stripped of credentials.
    void outputReceived(const QString &text);
    void finished(const GitCommandResult &result);

private:
    void reportFinished(int exitCode);

    QProcess *process_ = nullptr;
    QStringList arguments_;
    QString workingDirectory_;
    QByteArray collectedOutput_;
    bool cancelled_ = false;
};

/// Broadcasts Git invocations for the command log.
class GitLog final : public QObject {
    Q_OBJECT

public:
    static GitLog *instance();
    void record(const QString &workingDirectory, const QStringList &arguments,
                const GitCommandResult &result);

Q_SIGNALS:
    void commandRecorded(const QString &workingDirectory, const QString &command,
                         const QString &output, bool succeeded);

private:
    explicit GitLog(QObject *parent = nullptr);
};
