// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "sourceview.h"

#include "theme.h"

#include <QFontDatabase>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTextBlock>
#include <QTextDocument>

namespace {

constexpr int TextInset = 6;

}

class SourceGutter final : public QWidget {
public:
    explicit SourceGutter(SourceView *view)
        : QWidget(view), view_(view) {
    }

    [[nodiscard]] QSize sizeHint() const override {
        return {view_->gutterWidth(), 0};
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        view_->gutterPaintEvent(event);
    }

private:
    SourceView *view_ = nullptr;
};

SourceView::SourceView(QWidget *parent)
    : QPlainTextEdit(parent) {
    setObjectName(QStringLiteral("fileContents"));
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    document()->setDocumentMargin(0.0);
    setTabStopDistance(fontMetrics().horizontalAdvance(u' ') * 4);

    gutter_ = new SourceGutter(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateGutterWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            [this](const QRect &rect, const int scrollY) { updateGutter(rect, scrollY); });
    connect(Theme::instance(), &Theme::changed, this, [this] {
        viewport()->update();
        gutter_->update();
    });
    updateGutterWidth();
}

int SourceView::gutterWidth() const {
    const int digits = qMax(2, QString::number(qMax(1, blockCount())).size());
    return 12 + fontMetrics().horizontalAdvance(u'9') * digits;
}

void SourceView::updateGutterWidth() {
    setViewportMargins(gutterWidth() + TextInset, 0, 0, 0);
    gutter_->setGeometry(contentsRect().left(), contentsRect().top(),
                         gutterWidth(), contentsRect().height());
}

void SourceView::updateGutter(const QRect &rect, const int scrollY) {
    if (scrollY != 0) {
        gutter_->scroll(0, scrollY);
    } else {
        gutter_->update(0, rect.y(), gutter_->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        updateGutterWidth();
    }
}

void SourceView::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);
    gutter_->setGeometry(contentsRect().left(), contentsRect().top(),
                         gutterWidth(), contentsRect().height());
}

void SourceView::gutterPaintEvent(QPaintEvent *event) {
    const ThemePalette &palette = Theme::instance()->palette();
    QPainter painter(gutter_);
    // The gutter is a plain widget and takes its font from the style sheet,
    // which is not the fixed font the width was measured with.
    painter.setFont(font());
    painter.fillRect(event->rect(), palette.surfaceAlternate);
    painter.setPen(palette.border);
    painter.drawLine(gutter_->width() - 1, event->rect().top(),
                     gutter_->width() - 1, event->rect().bottom());

    const int currentLine = textCursor().blockNumber();
    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();

    while (block.isValid() && top <= event->rect().bottom()) {
        const qreal bottom = top + blockBoundingRect(block).height();
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(blockNumber == currentLine ? palette.text : palette.mutedText);
            painter.drawText(QRect(2, static_cast<int>(top), gutter_->width() - 8,
                                   static_cast<int>(blockBoundingRect(block).height())),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        ++blockNumber;
    }
}
