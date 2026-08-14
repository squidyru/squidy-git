// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

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

/// Parsed unified diff with helpers for partial staging.
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

    /// Returns true for a line that starts a file section.
    [[nodiscard]] static bool startsFileHeader(const QString &line);

private:
    [[nodiscard]] QByteArray buildPatch(int hunkIndex, const QList<int> &selectedLines,
                                        bool reverse) const;

    QList<DiffLine> lines_;
    QList<DiffHunk> hunks_;
    QList<QStringList> fileHeaders_;
};
