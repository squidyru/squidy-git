// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "credentialprompter.h"

#include "core/gitaskpass.h"
#include "core/gitclient.h"
#include "core/gitredact.h"
#include "platform/platformservices.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QUuid>
#include <QVBoxLayout>

namespace {

// Messages are single UTF-8 strings terminated by NUL. Neither a prompt nor a
// password can contain one, so no escaping is needed.
constexpr char MessageTerminator = '\0';
constexpr int HelperTimeoutMs = 120'000;

/// Git asks for a user name in the clear and for anything else masked. The
/// locale is pinned to C for every invocation, so matching English here is
/// safe.
bool asksForUserName(const QString &prompt) {
    return prompt.startsWith(QStringLiteral("Username"), Qt::CaseInsensitive);
}

QByteArray frame(const QString &text) {
    QByteArray message = text.toUtf8();
    message.append(MessageTerminator);
    return message;
}

/// Reads one NUL terminated message, waiting up to @p timeoutMs in total.
bool readMessage(QLocalSocket *socket, const int timeoutMs, QString *message) {
    QByteArray buffer;
    while (!buffer.contains(MessageTerminator)) {
        if (!socket->waitForReadyRead(timeoutMs)) {
            return false;
        }
        buffer.append(socket->readAll());
    }
    *message = QString::fromUtf8(buffer.left(buffer.indexOf(MessageTerminator)));
    return true;
}

/// The credential question, with a place to answer it. The prompt comes from
/// Git and names the host, so it is shown as the question itself.
class CredentialDialog final : public QDialog {
public:
    CredentialDialog(const QString &prompt, const bool masked, const QString &helper)
        : QDialog(nullptr) {
        setWindowTitle(tr("Git credentials"));
        setModal(true);

        auto *layout = new QVBoxLayout(this);
        // The prompt embeds a remote URL, which may carry a token.
        auto *question = new QLabel(redactCredentials(prompt));
        question->setWordWrap(true);
        layout->addWidget(question);

        value_ = new QLineEdit;
        if (masked) {
            value_->setEchoMode(QLineEdit::Password);
        }
        layout->addWidget(value_);

        if (masked && !helper.isEmpty()) {
            remember_ = new QCheckBox(
                tr("Remember using the system credential store (%1)").arg(helper));
            remember_->setToolTip(
                tr("Configures Git to store the credential itself. Without this the "
                   "answer is kept only until SquidyGit is closed."));
            layout->addWidget(remember_);
        }

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);

        value_->setFocus();
    }

    [[nodiscard]] QString value() const { return value_->text(); }
    [[nodiscard]] bool shouldRemember() const {
        return remember_ != nullptr && remember_->isChecked();
    }

private:
    QLineEdit *value_ = nullptr;
    QCheckBox *remember_ = nullptr;
};

}

CredentialPrompter::CredentialPrompter(QObject *parent)
    : QObject(parent) {
}

CredentialPrompter::~CredentialPrompter() {
    forgetAnswers();
}

bool CredentialPrompter::isHelperInvocation() {
    return !qEnvironmentVariable(GitAskPassServerVariable).isEmpty();
}

int CredentialPrompter::runHelper(const QString &prompt) {
    const QString serverName = qEnvironmentVariable(GitAskPassServerVariable);
    if (serverName.isEmpty()) {
        return 1;
    }

    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(5'000)) {
        return 1;
    }

    socket.write(frame(prompt));
    if (!socket.waitForBytesWritten(5'000)) {
        return 1;
    }

    QString answer;
    if (!readMessage(&socket, HelperTimeoutMs, &answer) || answer.isEmpty()) {
        // A non-zero exit is how a helper reports that it has no answer.
        return 1;
    }

    // Straight to Git through standard output: this never touches the streams
    // the command log records.
    QTextStream out(stdout);
    out << answer << Qt::endl;
    return 0;
}

bool CredentialPrompter::start() {
    server_ = new QLocalServer(this);
    // Reachable by this user alone.
    server_->setSocketOptions(QLocalServer::UserAccessOption);

    // Kept short: the socket lives under the temporary directory, and the path
    // it forms has to fit the limit the platform puts on a socket address.
    const QString name = QStringLiteral("sqg-%1")
                             .arg(QUuid::createUuid().toString(QUuid::Id128).left(12));
    if (!server_->listen(name)) {
        delete server_;
        server_ = nullptr;
        return false;
    }

    connect(server_, &QLocalServer::newConnection, this, [this] { serveConnection(); });
    GitAskPass::instance().configure(QCoreApplication::applicationFilePath(),
                                     server_->fullServerName());
    return true;
}

void CredentialPrompter::forgetAnswers() {
    answers_.clear();
}

void CredentialPrompter::serveConnection() {
    QLocalSocket *socket = server_->nextPendingConnection();
    if (socket == nullptr) {
        return;
    }
    connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);

    QString prompt;
    if (!readMessage(socket, HelperTimeoutMs, &prompt)) {
        socket->disconnectFromServer();
        return;
    }

    const QString answer = answerFor(prompt);
    if (!answer.isEmpty()) {
        socket->write(frame(answer));
        socket->waitForBytesWritten(5'000);
    }
    socket->disconnectFromServer();
}

QString CredentialPrompter::answerFor(const QString &prompt) {
    const auto cached = answers_.constFind(prompt);
    if (cached != answers_.constEnd()) {
        return *cached;
    }

    const bool masked = !asksForUserName(prompt);
    const QString helper = PlatformServices::instance().preferredCredentialHelper();
    CredentialDialog dialog(prompt, masked, helper);
    if (dialog.exec() != QDialog::Accepted || dialog.value().isEmpty()) {
        return {};
    }

    const QString value = dialog.value();
    answers_.insert(prompt, value);

    if (dialog.shouldRemember() && !helper.isEmpty()) {
        // Hand the keeping over to Git itself; the next question is answered
        // by the system store without reaching this dialog at all.
        static_cast<void>(GitClient::configureCredentialHelper(helper));
    }
    return value;
}
