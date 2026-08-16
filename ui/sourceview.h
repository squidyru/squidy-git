// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QPlainTextEdit>

class SourceGutter;
class QPaintEvent;
class QResizeEvent;

/// A read-only text view with a line-number gutter, used to show a file at a
/// revision. The colours and the fixed font match the diff panes.
class SourceView final : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit SourceView(QWidget *parent = nullptr);

    void gutterPaintEvent(QPaintEvent *event);
    [[nodiscard]] int gutterWidth() const;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateGutterWidth();
    void updateGutter(const QRect &rect, int scrollY);

    SourceGutter *gutter_ = nullptr;
};
