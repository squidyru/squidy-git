// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "commitgraph.h"

#include "icons.h"
#include "theme.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>

namespace {

constexpr int LaneWidth = 12;
constexpr int LaneOffset = 10;
constexpr qreal NodeRadius = 4.0;

QColor laneColor(const int lane) {
    const QList<QColor> &colors = Theme::instance()->palette().laneColors;
    return colors.at(lane % colors.size());
}

struct ReferenceChip {
    QString text;
    Icons::Glyph glyph = Icons::Glyph::Branch;
};

ReferenceChip chipFor(const QString &reference) {
    ReferenceChip chip;
    chip.text = reference;

    if (reference.startsWith(QStringLiteral("tag: "))) {
        chip.text = reference.mid(5);
        chip.glyph = Icons::Glyph::Tag;
    } else if (reference.startsWith(QStringLiteral("HEAD -> "))) {
        chip.text = reference.mid(8);
    }
    return chip;
}

}

CommitGraphDelegate::CommitGraphDelegate(QObject *parent)
    : QStyledItemDelegate(parent) {
}

int CommitGraphDelegate::laneWidth() {
    return LaneWidth;
}

int CommitGraphDelegate::graphWidthForLanes(const int lanes) {
    return LaneOffset * 2 + qMax(1, lanes) * LaneWidth;
}

QSize CommitGraphDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(23);
    return size;
}

void CommitGraphDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const {
    const ThemePalette &palette = Theme::instance()->palette();
    const QModelIndex graphIndex = index.sibling(index.row(), 0);
    const bool isHead = graphIndex.data(CommitRoles::IsHead).toBool();
    const bool uncommitted = graphIndex.data(CommitRoles::IsUncommitted).toBool();
    const bool selected = option.state.testFlag(QStyle::State_Selected);

    if (index.column() != 0) {
        QStyleOptionViewItem rowOption(option);
        initStyleOption(&rowOption, index);
        rowOption.font.setBold(isHead || uncommitted || selected);

        const QStringList references = index.data(CommitRoles::References).toStringList();
        if (references.isEmpty()) {
            QStyle *style = rowOption.widget != nullptr ? rowOption.widget->style()
                                                        : QApplication::style();
            style->drawControl(QStyle::CE_ItemViewItem, &rowOption, painter,
                               rowOption.widget);
            return;
        }

        // Draw the row background without its text so the chips can be laid out
        // before the commit subject.
        QStyleOptionViewItem chipOption(rowOption);
        chipOption.text.clear();
        QStyle *style = chipOption.widget != nullptr ? chipOption.widget->style()
                                                     : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &chipOption, painter, chipOption.widget);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setClipRect(option.rect);

        QFont chipFont = option.font;
        chipFont.setPixelSize(11);
        chipFont.setBold(false);
        const QFontMetrics chipMetrics(chipFont);

        int x = option.rect.left() + 4;
        const int chipHeight = qMin(18, option.rect.height() - 3);
        const int chipTop = option.rect.top() + (option.rect.height() - chipHeight) / 2;
        const int iconCellWidth = 18;
        const int chipGap = 6;
        const int subjectReserve = 80;
        const int referencesRight = option.rect.right() - subjectReserve;
        const QColor referenceColor =
            laneColor(graphIndex.data(CommitRoles::ColorIndex).toInt());
        const QColor chipSurface = Theme::instance()->mode() == Theme::Mode::Light
                                       ? QColor(QStringLiteral("#EAF2FF"))
                                       : palette.surface;
        const QColor chipBorder = Theme::instance()->mode() == Theme::Mode::Light
                                      ? QColor(QStringLiteral("#AFC8F3"))
                                      : palette.border;

        const auto drawChip = [&](const ReferenceChip &chip, const int left,
                                  const int width) {
            const QRectF chipRect(left + 0.5, chipTop + 0.5,
                                  width - 1.0, chipHeight - 1.0);
            QPainterPath pill;
            pill.addRoundedRect(chipRect, 4.5, 4.5);
            painter->fillPath(pill, chipSurface);

            painter->save();
            painter->setClipPath(pill);
            painter->fillRect(QRect(left, chipTop, iconCellWidth, chipHeight),
                              referenceColor);
            painter->restore();

            painter->setPen(QPen(chipBorder, 1.0));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(pill);

            const int glyphSize = 14;
            painter->drawPixmap(QRect(left + (iconCellWidth - glyphSize) / 2,
                                      chipTop + (chipHeight - glyphSize) / 2,
                                      glyphSize, glyphSize),
                                Icons::pixmap(chip.glyph, glyphSize, Qt::white));

            painter->setPen(palette.text);
            painter->setFont(chipFont);
            painter->drawText(QRect(left + iconCellWidth + 4, chipTop,
                                    width - iconCellWidth - 8, chipHeight),
                              Qt::AlignVCenter | Qt::AlignLeft, chip.text);
        };

        const auto drawOverflowChip = [&](const int left) {
            constexpr int width = 18;
            const QRectF rect(left + 0.5, chipTop + 0.5,
                              width - 1.0, chipHeight - 1.0);
            painter->setPen(Qt::NoPen);
            painter->setBrush(referenceColor);
            painter->drawRoundedRect(rect, 4.5, 4.5);
            painter->setBrush(Qt::white);
            const qreal dotY = rect.center().y() + 2.0;
            for (const qreal dotX : {rect.center().x() - 4.0,
                                     rect.center().x(), rect.center().x() + 4.0}) {
                painter->drawEllipse(QPointF(dotX, dotY), 1.0, 1.0);
            }
        };

        int shown = 0;
        const int maximumVisible = qMin(2, static_cast<int>(references.size()));
        for (int referenceIndex = 0; referenceIndex < maximumVisible; ++referenceIndex) {
            const QString &reference = references.at(referenceIndex);
            const ReferenceChip chip = chipFor(reference);
            const int width = chipMetrics.horizontalAdvance(chip.text) + iconCellWidth + 9;
            const bool hasMore = referenceIndex + 1 < references.size();
            const int overflowSpace = hasMore ? 18 + chipGap : 0;
            if (x + width + overflowSpace > referencesRight) {
                break;
            }
            drawChip(chip, x, width);
            x += width + chipGap;
            ++shown;
        }
        if (shown < references.size() && x + 18 <= referencesRight) {
            drawOverflowChip(x);
            x += 18 + chipGap;
        }

        const QString subject = index.data(Qt::DisplayRole).toString();
        if (!subject.isEmpty()) {
            QFont subjectFont = option.font;
            subjectFont.setBold(isHead || uncommitted || selected);
            painter->setFont(subjectFont);
            painter->setPen(selected
                                ? option.palette.color(QPalette::HighlightedText)
                                : palette.text);
            const QRect textRect(x, option.rect.top(),
                                 qMax(0, option.rect.right() - x - 4),
                                 option.rect.height());
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                              QFontMetrics(subjectFont).elidedText(subject, Qt::ElideRight,
                                                                  textRect.width()));
        }
        painter->restore();
        return;
    }

    QStyledItemDelegate::paint(painter, option, index);

    const int nodeLane = index.data(CommitRoles::Lane).toInt();
    const int nodeColor = index.data(CommitRoles::ColorIndex).toInt();
    const QVariantList passEdges = index.data(CommitRoles::PassEdges).toList();
    const QVariantList passColors = index.data(CommitRoles::PassColors).toList();
    const QVariantList parentLanes = index.data(CommitRoles::ParentLanes).toList();
    const QVariantList parentColors = index.data(CommitRoles::ParentColors).toList();
    const bool hasIncoming = index.data(CommitRoles::HasIncoming).toBool();
    const bool graphUncommitted = index.data(CommitRoles::IsUncommitted).toBool();
    const bool isMerge = index.data(CommitRoles::IsMerge).toBool();

    // Hollow nodes are punched out of the row they sit on, which is the stripe
    // colour on every second row.
    const QColor rowBackground = option.features.testFlag(QStyleOptionViewItem::Alternate)
                                     ? palette.rowStripe
                                     : palette.surface;

    const auto laneX = [&option](const int lane) {
        return option.rect.left() + LaneOffset + lane * LaneWidth;
    };
    const int centerY = option.rect.center().y();
    const int topY = option.rect.top();
    const int bottomY = option.rect.bottom() + 1;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(option.rect);

    const auto drawEdge = [&](const int fromLane, const int fromY,
                              const int toLane, const int toY, const int color) {
        QPen pen(laneColor(color), 1.35);
        pen.setCapStyle(Qt::RoundCap);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        QPainterPath path(QPointF(laneX(fromLane), fromY));
        if (fromLane == toLane) {
            path.lineTo(laneX(toLane), toY);
        } else {
            const qreal middleY = (fromY + toY) / 2.0;
            path.cubicTo(laneX(fromLane), middleY,
                         laneX(toLane), middleY,
                         laneX(toLane), toY);
        }
        painter->drawPath(path);
    };

    for (int position = 0; position < passEdges.size(); ++position) {
        const QPoint edge = passEdges.at(position).toPoint();
        drawEdge(edge.x(), topY, edge.y(), bottomY,
                 position < passColors.size() ? passColors.at(position).toInt() : nodeColor);
    }
    if (hasIncoming) {
        drawEdge(nodeLane, topY, nodeLane, centerY, nodeColor);
    }
    for (int position = 0; position < parentLanes.size(); ++position) {
        drawEdge(nodeLane, centerY, parentLanes.at(position).toInt(), bottomY,
                 position < parentColors.size() ? parentColors.at(position).toInt()
                                                : nodeColor);
    }

    const QPointF center(laneX(nodeLane), centerY);
    if (graphUncommitted) {
        QPen pen(palette.mutedText, 1.6);
        pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(rowBackground);
        painter->drawEllipse(center, NodeRadius, NodeRadius);
    } else if (!hasIncoming || isHead) {
        painter->setPen(QPen(isHead ? palette.text : laneColor(nodeColor), 1.6));
        painter->setBrush(rowBackground);
        painter->drawEllipse(center, NodeRadius, NodeRadius);
    } else {
        painter->setPen(Qt::NoPen);
        painter->setBrush(laneColor(nodeColor));
        painter->drawEllipse(center, NodeRadius, NodeRadius);
        if (isMerge) {
            painter->setBrush(rowBackground);
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(center, NodeRadius - 2.4, NodeRadius - 2.4);
        }
    }
    painter->restore();
}
