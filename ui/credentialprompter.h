// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QHash>
#include <QObject>
#include <QString>

class QLocalServer;

/// Answers the credential questions Git and ssh ask. Remembers them for as
/// long as the application runs; keeping one for longer is left to the system
/// credential helper the dialog offers to configure.
class CredentialPrompter final : public QObject {
    Q_OBJECT

public:
    explicit CredentialPrompter(QObject *parent = nullptr);
    ~CredentialPrompter() override;

    /// Starts the socket and points GitAskPass at it. Without one Git keeps
    /// failing rather than waiting.
    bool start();

    /// Forgets every answer given so far.
    void forgetAnswers();

    /// The helper side: asks @p prompt of the server named in the environment
    /// and prints the answer on standard output.
    [[nodiscard]] static int runHelper(const QString &prompt);

    /// Whether Git started this process as the credential helper.
    [[nodiscard]] static bool isHelperInvocation();

private:
    [[nodiscard]] QString answerFor(const QString &prompt);
    void serveConnection();

    QLocalServer *server_ = nullptr;
    /// Keyed by the question, which already names the host and user.
    QHash<QString, QString> answers_;
};
