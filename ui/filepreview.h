// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

/// Turns a blob into something the viewer can show. Everything here works on
/// bytes alone, so it runs on the worker thread that reads the revision.
namespace FilePreview {

/// Decodes a picture. Returns a null image when no installed image plugin can
/// read the data. @p format receives the short upper-case name, "PNG" or "SVG".
[[nodiscard]] QImage decodeImage(const QByteArray &content, QString *format = nullptr);

/// Renders bytes as offset, hex and printable columns. Long files are cut at
/// @p maximumBytes, because the point is to recognise the format, not to read
/// the whole blob.
[[nodiscard]] QString hexDump(const QByteArray &content, qsizetype maximumBytes = 64 * 1024);

}
