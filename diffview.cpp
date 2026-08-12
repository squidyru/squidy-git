// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "diffview.h"

#include "theme.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QMenu>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>

namespace {

const QRegularExpression &hunkHeaderPattern() {
    static const QRegularExpression pattern(
        QStringLiteral("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@"));
    return pattern;
}

bool startsFileHeader(const QString &line) {
    return line.startsWith(QStringLiteral("diff --git "))
           || line.startsWith(QStringLiteral("diff --cc "))
           || line.startsWith(QStringLiteral("diff --combined "));
}

bool isFileHeaderLine(const QString &line) {
    return startsFileHeader(line)
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

/// A gutter with "old line / new line" columns.
class DiffGutter final : public QWidget {
public:
    explicit DiffGutter(DiffView *view)
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
    DiffView *view_ = nullptr;
};

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

DiffView::DiffView(QWidget *parent)
    : QPlainTextEdit(parent) {
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);
    QFont monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monospace.setPointSizeF(monospace.pointSizeF() + 0.5);
    setFont(monospace);
    setTabStopDistance(fontMetrics().horizontalAdvance(u' ') * 4);

    gutter_ = new DiffGutter(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateGutterWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            [this](const QRect &rect, const int scrollY) { updateGutter(rect, scrollY); });
    connect(Theme::instance(), &Theme::changed, this, [this] { viewport()->update(); });
    updateGutterWidth();
}

void DiffView::setMode(const Mode mode) {
    mode_ = mode;
}

void DiffView::setPatch(const QString &patch) {
    document_.parse(patch);
    setPlainText(document_.text());
    updateGutterWidth();
    viewport()->update();
    gutter_->update();
}

void DiffView::setPlaceholderMessage(const QString &message) {
    placeholderMessage_ = message;
    document_.clear();
    setPlainText(QString());
    setPlaceholderText(message);
    updateGutterWidth();
    viewport()->update();
    gutter_->update();
}

bool DiffView::hasPatch() const {
    return !document_.isEmpty();
}

int DiffView::hunkCountAtCursor() const {
    return document_.hunkCount();
}

int DiffView::gutterWidth() const {
    if (document_.isEmpty()) {
        return 0;
    }
    const int digits = qMax(3, QString::number(qMax(1, blockCount())).size());
    return 12 + 2 * fontMetrics().horizontalAdvance(u'9') * digits;
}

void DiffView::updateGutterWidth() {
    setViewportMargins(gutterWidth(), 0, 0, 0);
    gutter_->setGeometry(contentsRect().left(), contentsRect().top(),
                         gutterWidth(), contentsRect().height());
}

void DiffView::updateGutter(const QRect &rect, const int scrollY) {
    if (scrollY != 0) {
        gutter_->scroll(0, scrollY);
    } else {
        gutter_->update(0, rect.y(), gutter_->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        updateGutterWidth();
    }
}

void DiffView::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);
    gutter_->setGeometry(contentsRect().left(), contentsRect().top(),
                         gutterWidth(), contentsRect().height());
}

void DiffView::gutterPaintEvent(QPaintEvent *event) {
    const ThemePalette &palette = Theme::instance()->palette();
    QPainter painter(gutter_);
    painter.fillRect(event->rect(), palette.surfaceAlternate);
    painter.setPen(palette.border);
    painter.drawLine(gutter_->width() - 1, event->rect().top(),
                     gutter_->width() - 1, event->rect().bottom());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();

    const int half = (gutter_->width() - 8) / 2;
    while (block.isValid() && top <= event->rect().bottom()) {
        const qreal bottom = top + blockBoundingRect(block).height();
        if (block.isVisible() && bottom >= event->rect().top()
            && blockNumber < document_.lines().size()) {
            const DiffLine &line = document_.lines().at(blockNumber);
            painter.setPen(line.type == DiffLine::Type::HunkHeader
                               ? palette.hunkText
                               : palette.mutedText);
            const QString oldText = line.oldNumber > 0 ? QString::number(line.oldNumber)
                                                       : QString();
            const QString newText = line.newNumber > 0 ? QString::number(line.newNumber)
                                                       : QString();
            const int height = static_cast<int>(blockBoundingRect(block).height());
            painter.drawText(QRect(2, static_cast<int>(top), half, height),
                             Qt::AlignRight | Qt::AlignVCenter, oldText);
            painter.drawText(QRect(4 + half, static_cast<int>(top), half, height),
                             Qt::AlignRight | Qt::AlignVCenter, newText);
        }
        block = block.next();
        top = bottom;
        ++blockNumber;
    }
}

void DiffView::paintEvent(QPaintEvent *event) {
    const ThemePalette &palette = Theme::instance()->palette();
    QPainter painter(viewport());
    painter.fillRect(event->rect(), palette.surface);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();

    while (block.isValid() && top <= event->rect().bottom()) {
        const qreal bottom = top + blockBoundingRect(block).height();
        if (block.isVisible() && bottom >= event->rect().top()
            && blockNumber < document_.lines().size()) {
            const DiffLine &line = document_.lines().at(blockNumber);
            QColor background;
            switch (line.type) {
                case DiffLine::Type::Added: background = palette.addedBackground; break;
                case DiffLine::Type::Removed: background = palette.removedBackground; break;
                case DiffLine::Type::HunkHeader: background = palette.hunkBackground; break;
                case DiffLine::Type::FileHeader:
                case DiffLine::Type::Meta: background = palette.surfaceAlternate; break;
                default: break;
            }
            if (background.isValid()) {
                painter.fillRect(QRectF(0, top, viewport()->width(),
                                        blockBoundingRect(block).height()),
                                 background);
            }
        }
        block = block.next();
        top = bottom;
        ++blockNumber;
    }
    painter.end();

    QPlainTextEdit::paintEvent(event);
}

int DiffView::currentHunkIndex() const {
    const int blockNumber = textCursor().blockNumber();
    if (blockNumber < 0 || blockNumber >= document_.lines().size()) {
        return -1;
    }
    return document_.lines().at(blockNumber).hunkIndex;
}

QList<int> DiffView::selectedLineIndices() const {
    QList<int> indices;
    const QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()) {
        if (cursor.blockNumber() >= 0) {
            indices.append(cursor.blockNumber());
        }
        return indices;
    }

    QTextCursor start(cursor);
    start.setPosition(cursor.selectionStart());
    QTextCursor end(cursor);
    end.setPosition(cursor.selectionEnd());
    for (int block = start.blockNumber(); block <= end.blockNumber(); ++block) {
        indices.append(block);
    }
    return indices;
}

void DiffView::requestHunk(const DiffAction action) {
    const QByteArray patch = document_.patchForHunk(currentHunkIndex(),
                                                    action != DiffAction::Stage);
    if (!patch.isEmpty()) {
        Q_EMIT patchRequested(patch, action);
    }
}

void DiffView::requestLines(const DiffAction action) {
    const QByteArray patch = document_.patchForLines(selectedLineIndices(),
                                                     action != DiffAction::Stage);
    if (!patch.isEmpty()) {
        Q_EMIT patchRequested(patch, action);
    }
}

void DiffView::contextMenuEvent(QContextMenuEvent *event) {
    QMenu *menu = createStandardContextMenu(event->pos());
    const int hunkIndex = currentHunkIndex();

    if (mode_ != Mode::ReadOnly && hunkIndex >= 0) {
        auto *first = menu->actions().isEmpty() ? nullptr : menu->actions().constFirst();
        const bool staged = mode_ == Mode::Staged;

        auto *hunkAction = new QAction(staged ? tr("Убрать ханк из индекса")
                                              : tr("Добавить ханк в индекс"), menu);
        connect(hunkAction, &QAction::triggered, this, [this, staged] {
            requestHunk(staged ? DiffAction::Unstage : DiffAction::Stage);
        });

        auto *linesAction = new QAction(staged ? tr("Убрать выбранные строки")
                                               : tr("Добавить выбранные строки"), menu);
        connect(linesAction, &QAction::triggered, this, [this, staged] {
            requestLines(staged ? DiffAction::Unstage : DiffAction::Stage);
        });

        menu->insertAction(first, hunkAction);
        menu->insertAction(first, linesAction);

        if (!staged) {
            auto *discardHunk = new QAction(tr("Откатить ханк"), menu);
            connect(discardHunk, &QAction::triggered, this,
                    [this] { requestHunk(DiffAction::Discard); });
            auto *discardLines = new QAction(tr("Откатить выбранные строки"), menu);
            connect(discardLines, &QAction::triggered, this,
                    [this] { requestLines(DiffAction::Discard); });
            menu->insertAction(first, discardHunk);
            menu->insertAction(first, discardLines);
        }
        menu->insertSeparator(first);
    }

    menu->exec(event->globalPos());
    delete menu;
}
