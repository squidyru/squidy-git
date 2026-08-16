// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QString>
#include <QStringList>

/// Hides the user information of every URL in @p text. For human readable text
/// only: output that is parsed afterwards must be left untouched.
[[nodiscard]] QString redactCredentials(const QString &text);

/// Redacts every entry.
[[nodiscard]] QStringList redactCredentials(const QStringList &values);
