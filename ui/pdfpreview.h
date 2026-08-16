// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QByteArray>
#include <QWidget>

class QBuffer;
class QLabel;
class QPdfDocument;
class QPdfView;

/// Shows a PDF revision, page after page, without writing it anywhere. The Qt
/// PDF module is optional at build time; where it is missing the class reports
/// that it cannot help and the caller falls back to the byte dump.
class PdfPreview final : public QWidget {
    Q_OBJECT

public:
    explicit PdfPreview(QWidget *parent = nullptr);

    /// True when this build was linked against the Qt PDF module.
    [[nodiscard]] static bool isSupported();

    /// Opens @p content, which stays owned by the preview while it is shown.
    /// Returns the number of pages, or -1 when the document cannot be read.
    int showDocument(const QByteArray &content);
    void clear();

private:
    QByteArray content_;
    QBuffer *source_ = nullptr;
    QPdfDocument *document_ = nullptr;
    QPdfView *view_ = nullptr;
    QLabel *message_ = nullptr;
};
