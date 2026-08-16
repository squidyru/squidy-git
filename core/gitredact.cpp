// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "gitredact.h"

#include <QRegularExpression>

namespace {
// Matches the user information of a URL, which is where Git carries tokens.
// The scp style "git@host:path" has no scheme and never carries a secret, so it
// is deliberately left out.
const QRegularExpression &userInfoPattern() {
    static const QRegularExpression pattern(
        QStringLiteral(R"((?<scheme>[A-Za-z][A-Za-z0-9+.\-]*)://(?<userinfo>[^/?#\s@]+)@)"));
    return pattern;
}

constexpr QLatin1StringView Placeholder("***");

// An SSH user name is not a secret, but anything carrying a password is, and
// over HTTP the user name itself is usually the token.
bool carriesSecret(const QString &scheme, const QString &userInfo) {
    return userInfo.contains(u':') || scheme.compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0
           || scheme.compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
}
}

QString redactCredentials(const QString &text) {
    if (!text.contains(QStringLiteral("://"))) {
        return text;
    }

    QString redacted;
    qsizetype copied = 0;
    QRegularExpressionMatchIterator matches = userInfoPattern().globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (!carriesSecret(match.captured(QStringLiteral("scheme")),
                           match.captured(QStringLiteral("userinfo")))) {
            continue;
        }

        const qsizetype start = match.capturedStart(QStringLiteral("userinfo"));
        redacted.append(QStringView(text).mid(copied, start - copied));
        redacted.append(Placeholder);
        copied = match.capturedEnd(QStringLiteral("userinfo"));
    }

    if (copied == 0) {
        return text;
    }
    redacted.append(QStringView(text).mid(copied));
    return redacted;
}

QStringList redactCredentials(const QStringList &values) {
    QStringList redacted;
    redacted.reserve(values.size());
    for (const QString &value : values) {
        redacted.append(redactCredentials(value));
    }
    return redacted;
}
