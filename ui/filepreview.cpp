// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "filepreview.h"

#include <QBuffer>
#include <QImageReader>
#include <QSize>

namespace {

constexpr qint64 MaximumPixels = 40'000'000;
constexpr int ScalableSize = 512;

// ICO files can contain several resolutions.
void jumpToLargestFrame(QImageReader &reader) {
    if (reader.imageCount() <= 1) {
        return;
    }

    int best = 0;
    qint64 bestArea = 0;
    for (int frame = 0; frame < reader.imageCount(); ++frame) {
        if (!reader.jumpToImage(frame)) {
            break;
        }
        const QSize size = reader.size();
        const qint64 area = qint64(size.width()) * size.height();
        if (area > bestArea) {
            bestArea = area;
            best = frame;
        }
    }
    reader.jumpToImage(best);
}

}

QImage FilePreview::decodeImage(const QByteArray &content, QString *format) {
    if (content.isEmpty()) {
        return {};
    }

    QBuffer buffer;
    buffer.setData(content);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }

    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        return {};
    }

    const QByteArray name = reader.format();
    if (format != nullptr) {
        *format = QString::fromLatin1(name).toUpper();
    }

    if (name == "ico") {
        jumpToLargestFrame(reader);
    }

    const QSize natural = reader.size();
    if (natural.isValid() && qint64(natural.width()) * natural.height() > MaximumPixels) {
        return {};
    }

    // A drawing has no resolution of its own, so it is rasterised at a size
    // that stays sharp in the viewer instead of its nominal one.
    if (name == "svg" && natural.isValid() && !natural.isEmpty()) {
        reader.setScaledSize(natural.scaled(ScalableSize, ScalableSize,
                                            Qt::KeepAspectRatio));
    }

    return reader.read();
}

QString FilePreview::hexDump(const QByteArray &content, const qsizetype maximumBytes) {
    const QByteArray shown = content.first(qMin(content.size(), qMax<qsizetype>(0, maximumBytes)));

    QString dump;
    dump.reserve(static_cast<qsizetype>(shown.size() * 4.5));

    for (qsizetype offset = 0; offset < shown.size(); offset += 16) {
        const QByteArray row = shown.sliced(offset, qMin<qsizetype>(16, shown.size() - offset));

        QString hex;
        QString printable;
        for (qsizetype index = 0; index < row.size(); ++index) {
            const auto value = static_cast<quint8>(row.at(index));
            hex += QStringLiteral("%1 ").arg(value, 2, 16, QLatin1Char('0'));
            if (index == 7) {
                hex += u' ';
            }
            printable += value >= 0x20 && value < 0x7f ? QLatin1Char(char(value))
                                                       : QLatin1Char('.');
        }

        dump += QStringLiteral("%1  %2 %3\n")
                    .arg(offset, 8, 16, QLatin1Char('0'))
                    .arg(hex, -50)
                    .arg(printable);
    }

    return dump;
}
