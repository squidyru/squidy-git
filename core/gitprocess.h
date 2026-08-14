// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include "gittypes.h"

#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QStringList>

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
                                               const QByteArray *input) = 0;
};

/// Runs the system Git executable.
class GitProcess final : public GitProcessRunner {
    Q_DECLARE_TR_FUNCTIONS(GitClient)

public:
    [[nodiscard]] GitCommandResult run(const QString &workingDirectory,
                                       const QStringList &arguments,
                                       int timeoutMs,
                                       const QByteArray *input) override;

    [[nodiscard]] static QString executable();
};

[[nodiscard]] GitProcessRunner *defaultGitRunner();

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
