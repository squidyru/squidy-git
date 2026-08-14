// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "diffdocument.h"

#include <QRegularExpression>

namespace {

const QRegularExpression &hunkHeaderPattern() {
    static const QRegularExpression pattern(
        QStringLiteral("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@"));
    return pattern;
}

bool isFileHeaderLine(const QString &line) {
    return DiffDocument::startsFileHeader(line)
           || line.startsWith(QStringLiteral("index "))
           || line.startsWith(QStringLiteral("--- "))
           || line.startsWith(QStringLiteral("+++ "))
           || line.startsWith(QStringLiteral("old mode "))
           || line.startsWith(QStringLiteral("new mode "))
           || line.startsWith(QStringLiteral("new file mode "))
           || line.startsWith(QStringLiteral("deleted file mode "))
           || line.startsWith(QStringLiteral("similarity index "))
           || line.startsWith(QStringLiteral("dissimilarity index "))
           || line.startsWith(QStringLiteral("rename from "))
           || line.startsWith(QStringLiteral("rename to "))
           || line.startsWith(QStringLiteral("copy from "))
           || line.startsWith(QStringLiteral("copy to "));
}

}

bool DiffDocument::startsFileHeader(const QString &line) {
    return line.startsWith(QStringLiteral("diff --git "))
           || line.startsWith(QStringLiteral("diff --cc "))
           || line.startsWith(QStringLiteral("diff --combined "));
}

void DiffDocument::clear() {
    lines_.clear();
    hunks_.clear();
    fileHeaders_.clear();
}

void DiffDocument::parse(const QString &patch) {
    clear();
    if (patch.isEmpty()) {
        return;
    }

    QStringList currentHeader;
    int oldNumber = 0;
    int newNumber = 0;
    int fileIndex = -1;

    const QStringList rawLines = patch.split(u'\n');
    for (qsizetype rawIndex = 0; rawIndex < rawLines.size(); ++rawIndex) {
        const QString &raw = rawLines.at(rawIndex);
        if (raw.isEmpty() && rawIndex == rawLines.size() - 1) {
            continue;
        }

        DiffLine line;
        line.text = raw;

        if (startsFileHeader(raw)) {
            currentHeader.clear();
            currentHeader.append(raw);
            fileHeaders_.append(currentHeader);
            fileIndex = static_cast<int>(fileHeaders_.size()) - 1;
            line.type = DiffLine::Type::FileHeader;
            lines_.append(line);
            continue;
        }

        if (isFileHeaderLine(raw)) {
            if (fileIndex < 0) {
                fileHeaders_.append(QStringList());
                fileIndex = static_cast<int>(fileHeaders_.size()) - 1;
            }
            fileHeaders_[fileIndex].append(raw);
            line.type = DiffLine::Type::FileHeader;
            lines_.append(line);
            continue;
        }

        const QRegularExpressionMatch match = hunkHeaderPattern().match(raw);
        if (match.hasMatch()) {
            if (fileIndex < 0) {
                fileHeaders_.append(QStringList());
                fileIndex = 0;
            }
            DiffHunk hunk;
            hunk.header = raw;
            hunk.fileIndex = fileIndex;
            hunk.oldStart = match.captured(1).toInt();
            hunk.newStart = match.captured(3).toInt();
            hunk.firstLine = static_cast<int>(lines_.size()) + 1;
            hunk.lastLine = hunk.firstLine - 1;
            hunks_.append(hunk);

            oldNumber = hunk.oldStart;
            newNumber = hunk.newStart;

            line.type = DiffLine::Type::HunkHeader;
            line.hunkIndex = static_cast<int>(hunks_.size()) - 1;
            lines_.append(line);
            continue;
        }

        if (hunks_.isEmpty()) {
            line.type = DiffLine::Type::Meta;
            lines_.append(line);
            continue;
        }

        line.hunkIndex = static_cast<int>(hunks_.size()) - 1;
        if (raw.startsWith(QStringLiteral("\\"))) {
            line.type = DiffLine::Type::NoNewline;
        } else if (raw.startsWith(u'+')) {
            line.type = DiffLine::Type::Added;
            line.newNumber = newNumber++;
        } else if (raw.startsWith(u'-')) {
            line.type = DiffLine::Type::Removed;
            line.oldNumber = oldNumber++;
        } else {
            line.type = DiffLine::Type::Context;
            line.oldNumber = oldNumber++;
            line.newNumber = newNumber++;
        }
        lines_.append(line);
        hunks_.last().lastLine = static_cast<int>(lines_.size()) - 1;
    }
}

bool DiffDocument::isEmpty() const {
    return lines_.isEmpty();
}

const QList<DiffLine> &DiffDocument::lines() const {
    return lines_;
}

const QList<DiffHunk> &DiffDocument::hunks() const {
    return hunks_;
}

int DiffDocument::hunkCount() const {
    return static_cast<int>(hunks_.size());
}

QString DiffDocument::text() const {
    QStringList result;
    result.reserve(lines_.size());
    for (const DiffLine &line : lines_) {
        result.append(line.text);
    }
    return result.join(u'\n');
}

QByteArray DiffDocument::patchForHunk(const int hunkIndex, const bool reverse) const {
    if (hunkIndex < 0 || hunkIndex >= hunks_.size()) {
        return {};
    }
    QList<int> selected;
    const DiffHunk &hunk = hunks_.at(hunkIndex);
    for (int index = hunk.firstLine; index <= hunk.lastLine; ++index) {
        selected.append(index);
    }
    return buildPatch(hunkIndex, selected, reverse);
}

QByteArray DiffDocument::patchForLines(const QList<int> &lineIndices, const bool reverse) const {
    if (lineIndices.isEmpty()) {
        return {};
    }

    int hunkIndex = -1;
    for (const int index : lineIndices) {
        if (index < 0 || index >= lines_.size()) {
            continue;
        }
        const DiffLine &line = lines_.at(index);
        if (line.type != DiffLine::Type::Added && line.type != DiffLine::Type::Removed) {
            continue;
        }
        if (hunkIndex < 0) {
            hunkIndex = line.hunkIndex;
        } else if (hunkIndex != line.hunkIndex) {
            // Restrict partial staging to a single hunk at a time.
            return {};
        }
    }
    if (hunkIndex < 0) {
        return {};
    }
    return buildPatch(hunkIndex, lineIndices, reverse);
}

QByteArray DiffDocument::buildPatch(const int hunkIndex, const QList<int> &selectedLines,
                                    const bool reverse) const {
    if (hunkIndex < 0 || hunkIndex >= hunks_.size()) {
        return {};
    }

    const DiffHunk &hunk = hunks_.at(hunkIndex);
    const QStringList header = fileHeaders_.value(hunk.fileIndex);
    if (header.isEmpty()) {
        return {};
    }

    QStringList body;
    int oldCount = 0;
    int newCount = 0;
    bool changed = false;

    for (int index = hunk.firstLine; index <= hunk.lastLine && index < lines_.size(); ++index) {
        const DiffLine &line = lines_.at(index);
        const bool selected = selectedLines.contains(index);

        switch (line.type) {
            case DiffLine::Type::Context:
                body.append(line.text.isEmpty() ? QStringLiteral(" ") : line.text);
                ++oldCount;
                ++newCount;
                break;
            case DiffLine::Type::Added:
                if (selected) {
                    body.append(line.text);
                    ++newCount;
                    changed = true;
                } else if (reverse) {
                    // The line already exists in the file being patched, so it
                    // has to survive the reverse apply as plain context.
                    body.append(QStringLiteral(" ") + line.text.mid(1));
                    ++oldCount;
                    ++newCount;
                }
                break;
            case DiffLine::Type::Removed:
                if (selected) {
                    body.append(line.text);
                    ++oldCount;
                    changed = true;
                } else if (!reverse) {
                    body.append(QStringLiteral(" ") + line.text.mid(1));
                    ++oldCount;
                    ++newCount;
                }
                break;
            case DiffLine::Type::NoNewline:
                body.append(line.text);
                break;
            default:
                break;
        }
    }

    if (!changed) {
        return {};
    }

    QStringList patch = header;
    patch.append(QStringLiteral("@@ -%1,%2 +%3,%4 @@")
                     .arg(oldCount == 0 ? 0 : hunk.oldStart)
                     .arg(oldCount)
                     .arg(newCount == 0 ? 0 : hunk.newStart)
                     .arg(newCount));
    patch.append(body);
    return (patch.join(u'\n') + u'\n').toUtf8();
}
