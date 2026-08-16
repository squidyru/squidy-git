// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QString>

class QProcessEnvironment;

/// Names the socket the helper reports to, and doubles as the switch into
/// helper mode: Git runs an askpass program with the prompt as its only
/// argument, leaving no flag to recognise the mode by.
inline constexpr char GitAskPassServerVariable[] = "SQUIDYGIT_ASKPASS_SERVER";

/// Lets Git and ssh ask the application for a credential. Both want an
/// executable that prints the answer on standard output, so the application
/// runs as its own helper and talks back over a local socket.
class GitAskPass {
public:
    [[nodiscard]] static GitAskPass &instance();

    GitAskPass(const GitAskPass &) = delete;
    GitAskPass &operator=(const GitAskPass &) = delete;

    /// Either argument being empty disables prompting.
    void configure(const QString &helperExecutable, const QString &serverName);

    [[nodiscard]] bool isAvailable() const;

    /// Without a helper, ssh goes into batch mode so it fails at once.
    void applyTo(QProcessEnvironment &environment) const;

private:
    GitAskPass() = default;

    QString helperExecutable_;
    QString serverName_;
};
