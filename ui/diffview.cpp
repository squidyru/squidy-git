// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "diffview.h"

#include "icons.h"
#include "theme.h"

#include <QAction>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include <memory>
#include <utility>

namespace {

enum class SideRowType {
    Context,
    Added,
    Removed,
    Empty,
    Hunk,
    FileHeader,
    Meta
};

struct SideBySideRow {
    QString oldText;
    QString newText;
    int oldNumber = -1;
    int newNumber = -1;
    SideRowType oldType = SideRowType::Context;
    SideRowType newType = SideRowType::Context;
};

struct SideDiffRowSets {
    QList<SideBySideRow> hidden;
    QList<SideBySideRow> full;
};

QString contentWithoutMarker(const QString &text) {
    return text.isEmpty() ? QString() : text.mid(1);
}

QString cleanHeaderPath(QString path) {
    path = path.section(u'\t', 0, 0).trimmed();
    if (path.size() >= 2 && path.startsWith(u'"') && path.endsWith(u'"')) {
        path = path.mid(1, path.size() - 2);
    }
    if (path.startsWith(QStringLiteral("a/"))
        || path.startsWith(QStringLiteral("b/"))) {
        path.remove(0, 2);
    }
    return path;
}

QString displayPath(const DiffDocument &document) {
    QString oldPath;
    for (const DiffLine &line : document.lines()) {
        if (line.text.startsWith(QStringLiteral("+++ "))) {
            const QString path = cleanHeaderPath(line.text.mid(4));
            if (path != QStringLiteral("/dev/null")) {
                return path;
            }
        } else if (line.text.startsWith(QStringLiteral("--- "))) {
            const QString path = cleanHeaderPath(line.text.mid(4));
            if (path != QStringLiteral("/dev/null")) {
                oldPath = path;
            }
        }
    }
    return oldPath;
}

QList<SideBySideRow> sideBySideRows(const DiffDocument &document,
                                    const QString &hiddenLinesTemplate,
                                    const bool showHiddenRows = true) {
    QList<SideBySideRow> rows;
    const QList<DiffLine> &lines = document.lines();
    int fileCount = 0;
    for (int lineIndex = 0; lineIndex + 1 < lines.size(); ++lineIndex) {
        if (lines.at(lineIndex).type == DiffLine::Type::FileHeader
            && lines.at(lineIndex).text.startsWith(QStringLiteral("--- "))
            && lines.at(lineIndex + 1).text.startsWith(QStringLiteral("+++ "))) {
            ++fileCount;
        }
    }
    int nextOldNumber = 1;
    int nextNewNumber = 1;

    int index = 0;
    while (index < lines.size()) {
        const DiffLine &line = lines.at(index);

        if (line.type == DiffLine::Type::Removed
            || line.type == DiffLine::Type::Added) {
            QList<DiffLine> removed;
            QList<DiffLine> added;
            while (index < lines.size()) {
                const DiffLine &changedLine = lines.at(index);
                if (changedLine.type == DiffLine::Type::Removed) {
                    removed.append(changedLine);
                } else if (changedLine.type == DiffLine::Type::Added) {
                    added.append(changedLine);
                } else if (changedLine.type != DiffLine::Type::NoNewline) {
                    break;
                }
                ++index;
            }

            const int rowCount = qMax(removed.size(), added.size());
            for (int changedIndex = 0; changedIndex < rowCount; ++changedIndex) {
                SideBySideRow row;
                if (changedIndex < removed.size()) {
                    const DiffLine &removedLine = removed.at(changedIndex);
                    row.oldText = contentWithoutMarker(removedLine.text);
                    row.oldNumber = removedLine.oldNumber;
                    row.oldType = SideRowType::Removed;
                    nextOldNumber = removedLine.oldNumber + 1;
                } else {
                    row.oldType = SideRowType::Empty;
                }
                if (changedIndex < added.size()) {
                    const DiffLine &addedLine = added.at(changedIndex);
                    row.newText = contentWithoutMarker(addedLine.text);
                    row.newNumber = addedLine.newNumber;
                    row.newType = SideRowType::Added;
                    nextNewNumber = addedLine.newNumber + 1;
                } else {
                    row.newType = SideRowType::Empty;
                }
                rows.append(row);
            }
            continue;
        }

        if (line.type == DiffLine::Type::Context) {
            SideBySideRow row;
            row.oldText = contentWithoutMarker(line.text);
            row.newText = row.oldText;
            row.oldNumber = line.oldNumber;
            row.newNumber = line.newNumber;
            rows.append(row);
            nextOldNumber = line.oldNumber + 1;
            nextNewNumber = line.newNumber + 1;
            ++index;
            continue;
        }

        if (line.type == DiffLine::Type::HunkHeader) {
            const DiffHunk &hunk = document.hunks().at(line.hunkIndex);
            if (showHiddenRows) {
                SideBySideRow row;
                row.oldType = SideRowType::Hunk;
                row.newType = SideRowType::Hunk;
                const int hiddenOldLines = qMax(0, hunk.oldStart - nextOldNumber);
                const int hiddenNewLines = qMax(0, hunk.newStart - nextNewNumber);
                row.oldText = hiddenOldLines > 0
                                  ? hiddenLinesTemplate.arg(hiddenOldLines)
                                  : QString();
                row.newText = hiddenNewLines > 0
                                  ? hiddenLinesTemplate.arg(hiddenNewLines)
                                  : QString();
                if (row.oldText.isEmpty() && row.newText.isEmpty()) {
                    row.oldText = line.text;
                    row.newText = line.text;
                }
                rows.append(row);
            }
            nextOldNumber = hunk.oldStart;
            nextNewNumber = hunk.newStart;
            ++index;
            continue;
        }

        if (line.type == DiffLine::Type::FileHeader
            && line.text.startsWith(QStringLiteral("--- "))
            && index + 1 < lines.size()
            && lines.at(index + 1).text.startsWith(QStringLiteral("+++ "))) {
            SideBySideRow row;
            row.oldText = cleanHeaderPath(line.text.mid(4));
            row.newText = cleanHeaderPath(lines.at(index + 1).text.mid(4));
            row.oldType = SideRowType::FileHeader;
            row.newType = SideRowType::FileHeader;
            if (fileCount > 1) {
                rows.append(row);
            }
            nextOldNumber = 1;
            nextNewNumber = 1;
            index += 2;
            continue;
        }

        const bool redundantHeader = line.type == DiffLine::Type::FileHeader
                                     && (DiffDocument::startsFileHeader(line.text)
                                         || line.text.startsWith(QStringLiteral("index "))
                                         || line.text.startsWith(QStringLiteral("+++ ")));
        if (!redundantHeader && line.type != DiffLine::Type::NoNewline) {
            SideBySideRow row;
            row.oldText = line.text;
            row.newText = line.text;
            row.oldType = line.type == DiffLine::Type::FileHeader
                              ? SideRowType::FileHeader
                              : SideRowType::Meta;
            row.newType = row.oldType;
            rows.append(row);
        }
        ++index;
    }

    return rows;
}

class SideDiffPane;

class SideDiffGutter final : public QWidget {
public:
    explicit SideDiffGutter(SideDiffPane *view);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    SideDiffPane *view_ = nullptr;
};

class SideDiffPane final : public QPlainTextEdit {
public:
    explicit SideDiffPane(bool oldSide, QWidget *parent = nullptr);

    void setRows(const QList<SideBySideRow> &rows, bool alignChanges = true);
    [[nodiscard]] int gutterWidth() const;
    void gutterPaintEvent(QPaintEvent *event);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateGutterWidth();
    void updateGutter(const QRect &rect, int scrollY);
    [[nodiscard]] SideRowType rowType(int row) const;
    [[nodiscard]] int lineNumber(int row) const;

    bool oldSide_ = false;
    QList<SideBySideRow> rows_;
    SideDiffGutter *gutter_ = nullptr;
};

SideDiffGutter::SideDiffGutter(SideDiffPane *view)
    : QWidget(view), view_(view) {
}

QSize SideDiffGutter::sizeHint() const {
    return {view_->gutterWidth(), 0};
}

void SideDiffGutter::paintEvent(QPaintEvent *event) {
    view_->gutterPaintEvent(event);
}

SideDiffPane::SideDiffPane(const bool oldSide, QWidget *parent)
    : QPlainTextEdit(parent), oldSide_(oldSide) {
    setObjectName(QStringLiteral("sideDiffPane"));
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);
    QFont monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    setFont(monospace);
    document()->setDocumentMargin(0.0);
    setTabStopDistance(fontMetrics().horizontalAdvance(u' ') * 4);

    gutter_ = new SideDiffGutter(this);
    connect(this, &QPlainTextEdit::blockCountChanged,
            this, [this] { updateGutterWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            [this](const QRect &rect, const int scrollY) {
                updateGutter(rect, scrollY);
            });
    connect(Theme::instance(), &Theme::changed, this, [this] {
        viewport()->update();
        gutter_->update();
    });
    updateGutterWidth();
}

void SideDiffPane::setRows(const QList<SideBySideRow> &rows,
                           const bool alignChanges) {
    rows_.clear();
    rows_.reserve(rows.size());
    for (const SideBySideRow &row : rows) {
        const SideRowType type = oldSide_ ? row.oldType : row.newType;
        if (alignChanges || type != SideRowType::Empty) {
            rows_.append(row);
        }
    }
    QStringList text;
    text.reserve(rows_.size());
    for (const SideBySideRow &row : rows_) {
        text.append(oldSide_ ? row.oldText : row.newText);
    }
    setPlainText(text.join(u'\n'));
    updateGutterWidth();
    viewport()->update();
    gutter_->update();
}

SideRowType SideDiffPane::rowType(const int row) const {
    if (row < 0 || row >= rows_.size()) {
        return SideRowType::Context;
    }
    return oldSide_ ? rows_.at(row).oldType : rows_.at(row).newType;
}

int SideDiffPane::lineNumber(const int row) const {
    if (row < 0 || row >= rows_.size()) {
        return -1;
    }
    return oldSide_ ? rows_.at(row).oldNumber : rows_.at(row).newNumber;
}

int SideDiffPane::gutterWidth() const {
    int maximumNumber = 1;
    for (int row = 0; row < rows_.size(); ++row) {
        maximumNumber = qMax(maximumNumber, lineNumber(row));
    }
    const int digits = qMax(3, QString::number(maximumNumber).size());
    return 10 + fontMetrics().horizontalAdvance(u'9') * digits;
}

void SideDiffPane::updateGutterWidth() {
    setViewportMargins(gutterWidth(), 0, 0, 0);
    gutter_->setGeometry(contentsRect().left(), contentsRect().top(),
                         gutterWidth(), contentsRect().height());
}

void SideDiffPane::updateGutter(const QRect &rect, const int scrollY) {
    if (scrollY != 0) {
        gutter_->scroll(0, scrollY);
    } else {
        gutter_->update(0, rect.y(), gutter_->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        updateGutterWidth();
    }
}

void SideDiffPane::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);
    gutter_->setGeometry(contentsRect().left(), contentsRect().top(),
                         gutterWidth(), contentsRect().height());
}

void SideDiffPane::gutterPaintEvent(QPaintEvent *event) {
    const ThemePalette &palette = Theme::instance()->palette();
    QPainter painter(gutter_);
    // The gutter is a plain widget and takes its font from the style sheet,
    // which is not the fixed font the width was measured with.
    painter.setFont(font());
    painter.fillRect(event->rect(), palette.surfaceAlternate);
    painter.setPen(palette.border);
    painter.drawLine(gutter_->width() - 1, event->rect().top(),
                     gutter_->width() - 1, event->rect().bottom());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    while (block.isValid() && top <= event->rect().bottom()) {
        const qreal bottom = top + blockBoundingRect(block).height();
        if (block.isVisible() && bottom >= event->rect().top()) {
            const int number = lineNumber(blockNumber);
            painter.setPen(rowType(blockNumber) == SideRowType::Hunk
                               ? palette.hunkText
                               : palette.mutedText);
            if (number > 0) {
                painter.drawText(QRect(2, static_cast<int>(top), gutter_->width() - 7,
                                       static_cast<int>(blockBoundingRect(block).height())),
                                 Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(number));
            }
        }
        block = block.next();
        top = bottom;
        ++blockNumber;
    }
}

void SideDiffPane::paintEvent(QPaintEvent *event) {
    const ThemePalette &palette = Theme::instance()->palette();
    QPainter painter(viewport());
    painter.fillRect(event->rect(), palette.surface);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    while (block.isValid() && top <= event->rect().bottom()) {
        const qreal bottom = top + blockBoundingRect(block).height();
        if (block.isVisible() && bottom >= event->rect().top()) {
            QColor background;
            switch (rowType(blockNumber)) {
                case SideRowType::Added: background = palette.addedBackground; break;
                case SideRowType::Removed: background = palette.removedBackground; break;
                case SideRowType::Hunk: background = palette.hunkBackground; break;
                case SideRowType::FileHeader:
                case SideRowType::Meta:
                case SideRowType::Empty: background = palette.surfaceAlternate; break;
                case SideRowType::Context: break;
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

constexpr int SideDiffTitleBarHeight = Icons::WindowControlButtonHeight;
constexpr int SideDiffShadowMargin = 10;
constexpr int SideDiffResizeMargin = SideDiffShadowMargin;

class SideBySideDiffDialog final : public QDialog {
public:
    SideBySideDiffDialog(const QString &title,
                         const QString &minimizeTip,
                         const QString &maximizeTip,
                         const QString &restoreTip,
                         const QString &closeTip,
                         QWidget *parent)
        : QDialog(parent, Qt::Window | Qt::FramelessWindowHint),
          maximizeTip_(maximizeTip), restoreTip_(restoreTip) {
        setObjectName(QStringLiteral("sideBySideDiffDialog"));
        setWindowTitle(title);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setMouseTracking(true);

        outerLayout_ = new QVBoxLayout(this);
        outerLayout_->setContentsMargins(SideDiffShadowMargin, SideDiffShadowMargin,
                                         SideDiffShadowMargin, SideDiffShadowMargin);
        outerLayout_->setSpacing(0);

        windowFrame_ = new QFrame;
        windowFrame_->setObjectName(QStringLiteral("sideDiffWindowFrame"));
        windowShadow_ = new QGraphicsDropShadowEffect(windowFrame_);
        windowShadow_->setBlurRadius(22.0);
        windowShadow_->setOffset(0.0, 2.0);
        windowShadow_->setColor(QColor(0, 0, 0, 145));
        windowFrame_->setGraphicsEffect(windowShadow_);
        outerLayout_->addWidget(windowFrame_);

        frameLayout_ = new QVBoxLayout(windowFrame_);
        frameLayout_->setContentsMargins(0, 0, 0, 0);
        frameLayout_->setSpacing(0);

        titleBar_ = new QWidget;
        titleBar_->setObjectName(QStringLiteral("sideDiffTitleBar"));
        titleBar_->setFixedHeight(SideDiffTitleBarHeight);
        titleBar_->setMouseTracking(true);
        titleBar_->installEventFilter(this);

        auto *titleLayout = new QHBoxLayout(titleBar_);
        titleLayout->setContentsMargins(8, 0, 0, 0);
        titleLayout->setSpacing(0);

        auto *logo = new QLabel;
        logo->setPixmap(Icons::pixmap(Icons::Glyph::Ghost, 16, Qt::white));
        logo->setFixedWidth(28);
        logo->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        titleLayout->addWidget(logo);

        auto *titleLabel = new QLabel(title);
        titleLabel->setObjectName(QStringLiteral("sideDiffWindowTitle"));
        titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        titleLayout->addWidget(titleLabel, 1);

        const auto addWindowButton = [titleLayout](const Icons::Glyph glyph,
                                                    const QString &tip,
                                                    const bool closeButton) {
            auto *button = new QToolButton;
            button->setObjectName(closeButton
                                      ? QStringLiteral("sideDiffCloseButton")
                                      : QStringLiteral("sideDiffWindowButton"));
            button->setIcon(Icons::icon(glyph, Qt::white));
            button->setIconSize(QSize(Icons::WindowControlIconSize,
                                      Icons::WindowControlIconSize));
            button->setFixedSize(Icons::WindowControlButtonWidth,
                                  Icons::WindowControlButtonHeight);
            button->setToolTip(tip);
            titleLayout->addWidget(button);
            return button;
        };

        QToolButton *minimizeButton = addWindowButton(
            Icons::Glyph::WindowMinimize, minimizeTip, false);
        maximizeButton_ = addWindowButton(
            Icons::Glyph::WindowMaximize, maximizeTip_, false);
        QToolButton *closeButton = addWindowButton(
            Icons::Glyph::WindowClose, closeTip, true);

        connect(minimizeButton, &QToolButton::clicked,
                this, [this] { showMinimized(); });
        connect(maximizeButton_, &QToolButton::clicked,
                this, [this] { toggleMaximized(); });
        connect(closeButton, &QToolButton::clicked,
                this, [this] { close(); });
        frameLayout_->addWidget(titleBar_);
        updateWindowFrame();
    }

    [[nodiscard]] QVBoxLayout *bodyLayout() const {
        return frameLayout_;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == titleBar_) {
            if (event->type() == QEvent::MouseButtonPress) {
                const auto *mouseEvent = static_cast<QMouseEvent *>(event);
                if (mouseEvent->button() == Qt::LeftButton
                    && windowHandle() != nullptr) {
                    windowHandle()->startSystemMove();
                    return true;
                }
            } else if (event->type() == QEvent::MouseButtonDblClick) {
                toggleMaximized();
                return true;
            }
        }
        return QDialog::eventFilter(watched, event);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        updateResizeCursor(event->position().toPoint());
        QDialog::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override {
        const Qt::Edges edges = resizeEdgesAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && edges != Qt::Edges()
            && windowHandle() != nullptr) {
            windowHandle()->startSystemResize(edges);
            event->accept();
            return;
        }
        QDialog::mousePressEvent(event);
    }

    void changeEvent(QEvent *event) override {
        QDialog::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange) {
            updateWindowFrame();
        }
    }

    void showEvent(QShowEvent *event) override {
        QDialog::showEvent(event);
        updateWindowFrame();
    }

private:
    void toggleMaximized() {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
    }

    void updateWindowFrame() {
        const bool maximized = isMaximized();
        const int margin = maximized ? 0 : SideDiffShadowMargin;
        outerLayout_->setContentsMargins(margin, margin, margin, margin);
        windowShadow_->setEnabled(!maximized);
        maximizeButton_->setIcon(Icons::icon(maximized
                                                 ? Icons::Glyph::WindowRestore
                                                 : Icons::Glyph::WindowMaximize,
                                             Qt::white));
        maximizeButton_->setToolTip(maximized ? restoreTip_ : maximizeTip_);
        if (maximized) {
            unsetCursor();
        }
    }

    [[nodiscard]] Qt::Edges resizeEdgesAt(const QPoint &position) const {
        if (isMaximized()) {
            return {};
        }
        Qt::Edges edges;
        if (position.x() <= SideDiffResizeMargin) {
            edges |= Qt::LeftEdge;
        } else if (position.x() >= width() - SideDiffResizeMargin) {
            edges |= Qt::RightEdge;
        }
        if (position.y() <= SideDiffResizeMargin) {
            edges |= Qt::TopEdge;
        } else if (position.y() >= height() - SideDiffResizeMargin) {
            edges |= Qt::BottomEdge;
        }
        return edges;
    }

    void updateResizeCursor(const QPoint &position) {
        const Qt::Edges edges = resizeEdgesAt(position);
        if ((edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::TopEdge))
            || (edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::BottomEdge))) {
            setCursor(Qt::SizeFDiagCursor);
        } else if ((edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::TopEdge))
                   || (edges.testFlag(Qt::LeftEdge)
                       && edges.testFlag(Qt::BottomEdge))) {
            setCursor(Qt::SizeBDiagCursor);
        } else if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge)) {
            setCursor(Qt::SizeHorCursor);
        } else if (edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge)) {
            setCursor(Qt::SizeVerCursor);
        } else {
            unsetCursor();
        }
    }

    QVBoxLayout *outerLayout_ = nullptr;
    QVBoxLayout *frameLayout_ = nullptr;
    QFrame *windowFrame_ = nullptr;
    QGraphicsDropShadowEffect *windowShadow_ = nullptr;
    QWidget *titleBar_ = nullptr;
    QToolButton *maximizeButton_ = nullptr;
    QString maximizeTip_;
    QString restoreTip_;
};

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

DiffView::DiffView(QWidget *parent)
    : QPlainTextEdit(parent) {
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);
    setObjectName(QStringLiteral("diffView"));
    QFont monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    setFont(monospace);
    document()->setDocumentMargin(0.0);
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

void DiffView::setPatch(const QString &patch,
                        std::function<QString()> fullPatchProvider) {
    document_.parse(patch);
    fullPatchProvider_ = std::move(fullPatchProvider);
    setPlainText(document_.text());
    updateGutterWidth();
    viewport()->update();
    gutter_->update();
}

void DiffView::setPlaceholderMessage(const QString &message) {
    placeholderMessage_ = message;
    document_.clear();
    fullPatchProvider_ = {};
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
    // The width follows the largest number on display, not the length of the
    // patch: a hunk of a long file starts far beyond its own line count.
    int maximumNumber = 1;
    for (const DiffLine &line : document_.lines()) {
        maximumNumber = qMax(maximumNumber, qMax(line.oldNumber, line.newNumber));
    }
    const int digits = qMax(3, QString::number(maximumNumber).size());
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
    painter.setFont(font());
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

void DiffView::openSideBySideDiff() {
    if (document_.isEmpty()) {
        return;
    }

    QWidget *hostWindow = window();
    const QString path = displayPath(document_);
    const QString title = path.isEmpty() ? tr("Diff")
                                          : tr("Diff — %1").arg(path);
    auto *dialog = new SideBySideDiffDialog(
        title, tr("Minimize"), tr("Maximize"), tr("Restore"), tr("Close"),
        hostWindow);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setWindowIcon(hostWindow->windowIcon());
    dialog->setMinimumSize(760, 420);

    const int dialogWidth = qBound(860, hostWindow->width() - 80, 1'500);
    const int dialogHeight = qBound(500, hostWindow->height() - 80, 900);
    dialog->resize(dialogWidth, dialogHeight);
    dialog->move(hostWindow->frameGeometry().center()
                 - QPoint(dialogWidth / 2, dialogHeight / 2));

    const QString hiddenLinesTemplate = tr("%1 lines hidden");
    auto rowSets = std::make_shared<SideDiffRowSets>();
    rowSets->hidden = sideBySideRows(document_, hiddenLinesTemplate);
    if (fullPatchProvider_) {
        const QString fullPatch = fullPatchProvider_();
        DiffDocument fullDocument;
        fullDocument.parse(fullPatch);
        if (!fullDocument.isEmpty()) {
            rowSets->full = sideBySideRows(fullDocument, hiddenLinesTemplate, false);
        }
    }

    auto *controls = new QWidget;
    controls->setObjectName(QStringLiteral("sideDiffControls"));
    controls->setFixedHeight(32);
    auto *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(8, 0, 8, 0);
    controlsLayout->setSpacing(16);
    controlsLayout->addStretch(1);

    auto *hideUnchanged = new QCheckBox(tr("Hide unchanged lines"));
    hideUnchanged->setChecked(true);
    hideUnchanged->setEnabled(!rowSets->full.isEmpty());
    hideUnchanged->setToolTip(
        tr("Show only the changed fragments or the complete old and new files"));
    controlsLayout->addWidget(hideUnchanged);

    auto *noEmptyLeft = new QCheckBox(tr("Do not add empty rows on the left"));
    noEmptyLeft->setToolTip(
        tr("Do not insert empty rows on the left to align added lines"));
    controlsLayout->addWidget(noEmptyLeft);
    dialog->bodyLayout()->addWidget(controls);

    auto *oldPane = new SideDiffPane(true);
    auto *newPane = new SideDiffPane(false);
    oldPane->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    const auto buildPanel = [](const QString &title, SideDiffPane *pane) {
        auto *panel = new QWidget;
        panel->setObjectName(QStringLiteral("sideDiffPanel"));
        auto *panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(0, 0, 0, 0);
        panelLayout->setSpacing(0);

        auto *header = new QLabel(title.toUpper());
        header->setObjectName(QStringLiteral("sideDiffHeader"));
        header->setFixedHeight(28);
        panelLayout->addWidget(header);
        panelLayout->addWidget(pane, 1);
        return panel;
    };

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName(QStringLiteral("sideDiffSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(3);
    splitter->addWidget(buildPanel(tr("Old version"), oldPane));
    splitter->addWidget(buildPanel(tr("New version"), newPane));
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({1, 1});
    dialog->bodyLayout()->addWidget(splitter, 1);

    const auto refreshPanes = [oldPane, newPane, hideUnchanged, noEmptyLeft,
                               rowSets] {
        QScrollBar *oldScroll = oldPane->verticalScrollBar();
        QScrollBar *newScroll = newPane->verticalScrollBar();
        const int previousMaximum = oldScroll->maximum();
        const qreal scrollRatio = previousMaximum > 0
                                      ? oldScroll->value()
                                            / static_cast<qreal>(previousMaximum)
                                      : 0.0;
        const QSignalBlocker oldBlocker(oldScroll);
        const QSignalBlocker newBlocker(newScroll);

        const QList<SideBySideRow> &rows =
            !hideUnchanged->isChecked() && !rowSets->full.isEmpty()
                ? rowSets->full
                : rowSets->hidden;
        oldPane->setRows(rows, !noEmptyLeft->isChecked());
        newPane->setRows(rows, true);

        oldScroll->setValue(qRound(scrollRatio * oldScroll->maximum()));
        newScroll->setValue(qRound(scrollRatio * newScroll->maximum()));
    };
    refreshPanes();

    connect(oldPane->verticalScrollBar(), &QScrollBar::valueChanged,
            dialog, [newPane, noEmptyLeft](const int value) {
                if (!noEmptyLeft->isChecked()) {
                    newPane->verticalScrollBar()->setValue(value);
                }
            });
    connect(newPane->verticalScrollBar(), &QScrollBar::valueChanged,
            dialog, [oldPane, noEmptyLeft](const int value) {
                if (!noEmptyLeft->isChecked()) {
                    oldPane->verticalScrollBar()->setValue(value);
                }
            });
    connect(hideUnchanged, &QCheckBox::toggled,
            dialog, [refreshPanes] { refreshPanes(); });
    connect(noEmptyLeft, &QCheckBox::toggled,
            dialog, [refreshPanes] { refreshPanes(); });

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    oldPane->setFocus();
}

void DiffView::contextMenuEvent(QContextMenuEvent *event) {
    QMenu *menu = createStandardContextMenu(event->pos());

    if (!document_.isEmpty()) {
        QAction *first = menu->actions().isEmpty()
                             ? nullptr
                             : menu->actions().constFirst();
        auto *openDiffAction = new QAction(tr("Open Diff Window"), menu);
        connect(openDiffAction, &QAction::triggered,
                this, [this] { openSideBySideDiff(); });
        menu->insertAction(first, openDiffAction);
        menu->insertSeparator(first);
    }

    const int hunkIndex = currentHunkIndex();

    if (mode_ != Mode::ReadOnly && hunkIndex >= 0) {
        auto *first = menu->actions().isEmpty() ? nullptr : menu->actions().constFirst();
        const bool staged = mode_ == Mode::Staged;

        auto *hunkAction = new QAction(staged ? tr("Unstage hunk")
                                              : tr("Stage hunk"), menu);
        connect(hunkAction, &QAction::triggered, this, [this, staged] {
            requestHunk(staged ? DiffAction::Unstage : DiffAction::Stage);
        });

        auto *linesAction = new QAction(staged ? tr("Unstage selected lines")
                                               : tr("Stage selected lines"), menu);
        connect(linesAction, &QAction::triggered, this, [this, staged] {
            requestLines(staged ? DiffAction::Unstage : DiffAction::Stage);
        });

        menu->insertAction(first, hunkAction);
        menu->insertAction(first, linesAction);

        if (!staged) {
            auto *discardHunk = new QAction(tr("Discard hunk"), menu);
            connect(discardHunk, &QAction::triggered, this,
                    [this] { requestHunk(DiffAction::Discard); });
            auto *discardLines = new QAction(tr("Discard selected lines"), menu);
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
