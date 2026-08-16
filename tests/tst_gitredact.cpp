// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "core/gitredact.h"
#include "core/gittypes.h"

#include <QTest>

class TestGitRedact final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void hidesUserInformation_data();
    void hidesUserInformation();

    void keepsTextWithoutUrls();
    void hidesEveryUrlOfOneLine();
    void redactsEachArgument();
    void hidesTokenInReportedText();
    void keepsParsedOutputVerbatim();
};

void TestGitRedact::hidesUserInformation_data() {
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");

    QTest::newRow("token as user name")
        << QStringLiteral("https://ghp_secret@github.com/acme/app.git")
        << QStringLiteral("https://***@github.com/acme/app.git");
    QTest::newRow("user and password")
        << QStringLiteral("https://alice:s3cret@example.com/repo.git")
        << QStringLiteral("https://***@example.com/repo.git");
    QTest::newRow("plain http")
        << QStringLiteral("http://alice:s3cret@example.com/repo.git")
        << QStringLiteral("http://***@example.com/repo.git");
    QTest::newRow("uppercase scheme")
        << QStringLiteral("HTTPS://ghp_secret@github.com/acme/app.git")
        << QStringLiteral("HTTPS://***@github.com/acme/app.git");
    QTest::newRow("ssh passphrase in url")
        << QStringLiteral("ssh://alice:s3cret@example.com/repo.git")
        << QStringLiteral("ssh://***@example.com/repo.git");

    // An ssh user name is not a secret and helps diagnose a failure.
    QTest::newRow("ssh user name is kept")
        << QStringLiteral("ssh://git@github.com/acme/app.git")
        << QStringLiteral("ssh://git@github.com/acme/app.git");
    // The scp form has no scheme and never carries a secret.
    QTest::newRow("scp form is kept")
        << QStringLiteral("git@github.com:acme/app.git")
        << QStringLiteral("git@github.com:acme/app.git");
    QTest::newRow("url without user information")
        << QStringLiteral("https://github.com/acme/app.git")
        << QStringLiteral("https://github.com/acme/app.git");
}

void TestGitRedact::hidesUserInformation() {
    QFETCH(QString, text);
    QFETCH(QString, expected);
    QCOMPARE(redactCredentials(text), expected);
}

void TestGitRedact::keepsTextWithoutUrls() {
    const QString message = QStringLiteral("fatal: not a git repository");
    QCOMPARE(redactCredentials(message), message);
    QCOMPARE(redactCredentials(QString()), QString());
}

void TestGitRedact::hidesEveryUrlOfOneLine() {
    const QString text = QStringLiteral(
        "moving https://a:1@x.test/r.git to https://b:2@y.test/r.git");
    QCOMPARE(redactCredentials(text),
             QStringLiteral("moving https://***@x.test/r.git to https://***@y.test/r.git"));
}

void TestGitRedact::redactsEachArgument() {
    const QStringList arguments{QStringLiteral("push"),
                                QStringLiteral("https://ghp_secret@github.com/acme/app.git"),
                                QStringLiteral("main")};
    QCOMPARE(redactCredentials(arguments),
             QStringList({QStringLiteral("push"),
                          QStringLiteral("https://***@github.com/acme/app.git"),
                          QStringLiteral("main")}));
}

void TestGitRedact::hidesTokenInReportedText() {
    // Git echoes the remote URL back on failure, which is how a token reaches
    // the activity log and the error dialog.
    GitCommandResult result;
    result.exitCode = 128;
    result.errorOutput = QByteArray(
        "fatal: unable to access 'https://alice:ghp_secret@github.com/acme/app.git/': 403");

    QVERIFY(!result.reportText().contains(QStringLiteral("ghp_secret")));
    QVERIFY(!result.errorText().contains(QStringLiteral("ghp_secret")));
    QVERIFY(result.reportText().contains(QStringLiteral("https://***@github.com")));
}

void TestGitRedact::keepsParsedOutputVerbatim() {
    // Redacting here would corrupt what the parsers read.
    GitCommandResult result;
    result.exitCode = 0;
    result.output = QByteArray("https://alice:ghp_secret@github.com/acme/app.git\n");
    QCOMPARE(result.outputText(),
             QStringLiteral("https://alice:ghp_secret@github.com/acme/app.git\n"));
}

QTEST_APPLESS_MAIN(TestGitRedact)

#include "tst_gitredact.moc"
