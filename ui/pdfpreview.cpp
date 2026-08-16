// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "pdfpreview.h"

#include "theme.h"

#include <QBuffer>
#include <QLabel>
#include <QPalette>
#include <QVBoxLayout>

#ifdef SQUIDYGIT_HAS_PDF
#include <QPdfDocument>
#include <QPdfView>
#endif

PdfPreview::PdfPreview(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("pdfPreview"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    message_ = new QLabel;
    message_->setObjectName(QStringLiteral("mutedText"));
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);
    layout->addWidget(message_);

#ifdef SQUIDYGIT_HAS_PDF
    source_ = new QBuffer(&content_, this);
    document_ = new QPdfDocument(this);

    view_ = new QPdfView;
    view_->setObjectName(QStringLiteral("pdfView"));
    view_->setDocument(document_);
    view_->setPageMode(QPdfView::PageMode::MultiPage);
    view_->setZoomMode(QPdfView::ZoomMode::FitInView);
    view_->setDocumentMargins(QMargins(6, 6, 6, 6));
    view_->setPageSpacing(6);
    layout->addWidget(view_, 1);
    message_->hide();

    // The view paints the surround of the pages with the Dark role, which is a
    // fixed grey of its own until the theme fills it in.
    const auto followTheme = [this] {
        QPalette palette = view_->palette();
        palette.setBrush(QPalette::Dark, Theme::instance()->palette().border);
        view_->setPalette(palette);
    };
    followTheme();
    connect(Theme::instance(), &Theme::changed, this, followTheme);
#endif
}

bool PdfPreview::isSupported() {
#ifdef SQUIDYGIT_HAS_PDF
    return true;
#else
    return false;
#endif
}

void PdfPreview::clear() {
#ifdef SQUIDYGIT_HAS_PDF
    document_->close();
    source_->close();
#endif
    content_.clear();
}

int PdfPreview::showDocument(const QByteArray &content) {
#ifdef SQUIDYGIT_HAS_PDF
    clear();

    // The document reads from the buffer while it is open, so the bytes are
    // kept here rather than on the caller's stack.
    content_ = content;
    if (!source_->open(QIODevice::ReadOnly)) {
        return -1;
    }

    document_->load(source_);
    if (document_->status() != QPdfDocument::Status::Ready) {
        clear();
        return -1;
    }

    view_->show();
    message_->hide();
    return document_->pageCount();
#else
    Q_UNUSED(content)
    message_->setText(tr("This build has no PDF viewer."));
    message_->show();
    return -1;
#endif
}
