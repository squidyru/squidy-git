// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "gitaskpass.h"

#include <QProcessEnvironment>

GitAskPass &GitAskPass::instance() {
    static GitAskPass askPass;
    return askPass;
}

void GitAskPass::configure(const QString &helperExecutable, const QString &serverName) {
    helperExecutable_ = helperExecutable;
    serverName_ = serverName;
}

bool GitAskPass::isAvailable() const {
    return !helperExecutable_.isEmpty() && !serverName_.isEmpty();
}

void GitAskPass::applyTo(QProcessEnvironment &environment) const {
    if (!isAvailable()) {
        // GIT_TERMINAL_PROMPT does not reach ssh, which would read a
        // passphrase from the terminal and hold the operation until it timed
        // out. A key held by an agent still works, and a command the user
        // configured wins over this one.
        if (!environment.contains(QStringLiteral("GIT_SSH_COMMAND"))) {
            environment.insert(QStringLiteral("GIT_SSH_COMMAND"),
                               QStringLiteral("ssh -o BatchMode=yes"));
        }
        return;
    }

    environment.insert(QStringLiteral("GIT_ASKPASS"), helperExecutable_);
    environment.insert(QStringLiteral("SSH_ASKPASS"), helperExecutable_);
    // Otherwise ssh only uses an askpass when it has no terminal at all.
    environment.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
    environment.insert(QString::fromLatin1(GitAskPassServerVariable), serverName_);
    // GIT_SSH_COMMAND is left alone: anything set here came from the user.
}
