// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include "core/diffdocument.h"

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QPlainTextEdit>
#include <QString>
#include <QStringList>

#include <functional>

class QPaintEvent;
class QResizeEvent;

enum class DiffAction {
    Stage,
    Unstage,
    Discard
};

class DiffView final : public QPlainTextEdit {
    Q_OBJECT

public:
    enum class Mode {
        ReadOnly,
        Unstaged,
        Staged
    };

    /// Reads the same diff with full context, once per patch when the side by
    /// side view opens. Answered on a worker so the dialog appears at once.
    using FullPatchProvider = std::function<QFuture<QString>()>;

    explicit DiffView(QWidget *parent = nullptr);

    void setMode(Mode mode);
    void setPatch(const QString &patch, FullPatchProvider fullPatchProvider = {});
    void setPlaceholderMessage(const QString &message);
    [[nodiscard]] bool hasPatch() const;
    [[nodiscard]] int hunkCountAtCursor() const;

    void gutterPaintEvent(QPaintEvent *event);
    [[nodiscard]] int gutterWidth() const;

Q_SIGNALS:
    void patchRequested(const QByteArray &patch, DiffAction action);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void updateGutterWidth();
    void updateGutter(const QRect &rect, int scrollY);
    [[nodiscard]] int currentHunkIndex() const;
    [[nodiscard]] QList<int> selectedLineIndices() const;
    void requestHunk(DiffAction action);
    void requestLines(DiffAction action);
    void openSideBySideDiff();

    DiffDocument document_;
    Mode mode_ = Mode::ReadOnly;
    QWidget *gutter_ = nullptr;
    QString placeholderMessage_;
    FullPatchProvider fullPatchProvider_;
};
