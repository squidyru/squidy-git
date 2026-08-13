// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QByteArray>
#include <QList>
#include <QPlainTextEdit>
#include <QString>
#include <QStringList>

#include <functional>

class QPaintEvent;
class QResizeEvent;

struct DiffLine {
    enum class Type {
        Meta,
        FileHeader,
        HunkHeader,
        Context,
        Added,
        Removed,
        NoNewline
    };

    Type type = Type::Context;
    QString text;
    int oldNumber = -1;
    int newNumber = -1;
    int hunkIndex = -1;
};

struct DiffHunk {
    QString header;
    int fileIndex = 0;
    int firstLine = 0;
    int lastLine = 0;
    int oldStart = 0;
    int newStart = 0;
};

/// Parses a unified diff and can rebuild partial patches for hunk or line level
/// staging, allowing part of a file to be committed.
class DiffDocument {
public:
    void parse(const QString &patch);
    void clear();

    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] const QList<DiffLine> &lines() const;
    [[nodiscard]] const QList<DiffHunk> &hunks() const;
    [[nodiscard]] int hunkCount() const;
    [[nodiscard]] QString text() const;

    /// @param reverse build the patch for "git apply --reverse", where the
    ///        current file holds the new side and unselected additions have to
    ///        stay in place as context.
    [[nodiscard]] QByteArray patchForHunk(int hunkIndex, bool reverse) const;
    [[nodiscard]] QByteArray patchForLines(const QList<int> &lineIndices, bool reverse) const;

private:
    [[nodiscard]] QByteArray buildPatch(int hunkIndex, const QList<int> &selectedLines,
                                        bool reverse) const;

    QList<DiffLine> lines_;
    QList<DiffHunk> hunks_;
    QList<QStringList> fileHeaders_;
};

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

    explicit DiffView(QWidget *parent = nullptr);

    void setMode(Mode mode);
    void setPatch(const QString &patch,
                  std::function<QString()> fullPatchProvider = {});
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
    std::function<QString()> fullPatchProvider_;
};
