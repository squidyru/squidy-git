// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "commitgraph.h"

#include "icons.h"
#include "theme.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>

namespace {

constexpr int LaneWidth = 14;
constexpr int LaneOffset = 12;
constexpr qreal NodeRadius = 4.5;

QColor laneColor(const int lane) {
    const QList<QColor> &colors = Theme::instance()->palette().laneColors;
    return colors.at(lane % colors.size());
}

struct ReferenceChip {
    QString text;
    QColor accent;
    Icons::Glyph glyph = Icons::Glyph::Branch;
    bool current = false;
};

ReferenceChip chipFor(const QString &reference) {
    const ThemePalette &palette = Theme::instance()->palette();
    ReferenceChip chip;
    chip.text = reference;

    if (reference.startsWith(QStringLiteral("tag: "))) {
        chip.text = reference.mid(5);
        chip.accent = palette.warning;
        chip.glyph = Icons::Glyph::Tag;
    } else if (reference == QStringLiteral("HEAD")) {
        chip.accent = palette.danger;
        chip.current = true;
    } else if (reference.startsWith(QStringLiteral("HEAD -> "))) {
        chip.text = reference.mid(8);
        chip.accent = palette.success;
        chip.current = true;
    } else if (reference.contains(u'/')) {
        chip.accent = palette.mutedText;
        chip.glyph = Icons::Glyph::Remote;
    } else {
        chip.accent = palette.accent;
    }
    return chip;
}

}

QStringList splitReferences(const QString &decoration) {
    QStringList references;
    const QStringList parts = decoration.split(QStringLiteral(", "), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            references.append(trimmed);
        }
    }
    return references;
}

QList<GraphRow> computeCommitGraph(const QList<GitCommitInfo> &commits,
                                   const bool leadingUncommittedRow,
                                   const QString &headHash) {
    QList<GraphRow> rows;
    rows.reserve(commits.size() + (leadingUncommittedRow ? 1 : 0));
    QStringList activeLanes;

    if (leadingUncommittedRow) {
        GraphRow row;
        row.lane = 0;
        row.laneCount = 1;
        row.hasIncoming = false;
        if (!headHash.isEmpty()) {
            activeLanes.append(headHash);
            row.parentLanes.append(0);
        }
        rows.append(row);
    }

    for (const GitCommitInfo &commit : commits) {
        const QStringList lanesBefore = activeLanes;
        GraphRow row;

        int nodeLane = static_cast<int>(activeLanes.indexOf(commit.hash));
        row.hasIncoming = nodeLane >= 0;
        if (nodeLane < 0) {
            nodeLane = static_cast<int>(activeLanes.size());
            activeLanes.append(commit.hash);
        }
        row.lane = nodeLane;
        row.isMerge = commit.parents.size() > 1;

        // Replace this commit's lane with its first parent, appending extra
        // parents in fresh lanes so merges fan out to the right.
        activeLanes.removeAt(nodeLane);
        int insertionLane = nodeLane;
        for (const QString &parent : commit.parents) {
            if (!activeLanes.contains(parent)) {
                activeLanes.insert(qMin(insertionLane, static_cast<int>(activeLanes.size())),
                                   parent);
                ++insertionLane;
            }
        }

        for (int lane = 0; lane < lanesBefore.size(); ++lane) {
            if (lane == nodeLane) {
                continue;
            }
            const int targetLane = static_cast<int>(activeLanes.indexOf(lanesBefore.at(lane)));
            if (targetLane >= 0) {
                row.passEdges.append(QPoint(lane, targetLane));
            }
        }

        for (const QString &parent : commit.parents) {
            const int parentLane = static_cast<int>(activeLanes.indexOf(parent));
            if (parentLane >= 0 && !row.parentLanes.contains(parentLane)) {
                row.parentLanes.append(parentLane);
            }
        }

        row.laneCount = static_cast<int>(qMax(lanesBefore.size(), activeLanes.size()));
        rows.append(row);
    }

    int maximumLanes = 1;
    for (const GraphRow &row : rows) {
        maximumLanes = qMax(maximumLanes, row.laneCount);
    }
    for (GraphRow &row : rows) {
        row.laneCount = maximumLanes;
    }
    return rows;
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
    size.setHeight(qMax(size.height(), 19));
    return size;
}

void CommitGraphDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const {
    const ThemePalette &palette = Theme::instance()->palette();

    if (index.column() != 0) {
        const QStringList references = index.data(CommitRoles::References).toStringList();
        if (references.isEmpty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        // Draw the row background without its text so the chips can be laid out
        // before the commit subject.
        QStyleOptionViewItem chipOption(option);
        initStyleOption(&chipOption, index);
        chipOption.text.clear();
        QStyle *style = chipOption.widget != nullptr ? chipOption.widget->style()
                                                     : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &chipOption, painter, chipOption.widget);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setClipRect(option.rect);

        QFont chipFont = option.font;
        chipFont.setPointSizeF(qMax(6.5, option.font.pointSizeF() - 1.0));
        chipFont.setBold(true);
        const QFontMetrics chipMetrics(chipFont);

        int x = option.rect.left() + 4;
        const int chipHeight = qMin(option.rect.height() - 6, chipMetrics.height() + 4);
        const int chipTop = option.rect.top() + (option.rect.height() - chipHeight) / 2;

        const int glyphSize = qMax(9, chipHeight - 5);
        for (const QString &reference : references) {
            const ReferenceChip chip = chipFor(reference);
            const int width = chipMetrics.horizontalAdvance(chip.text) + glyphSize + 14;
            if (x + width > option.rect.right() - 40) {
                break;
            }

            // Outlined pill with a leading glyph for reference labels.
            const QRectF chipRect(x + 0.5, chipTop + 0.5, width - 1.0, chipHeight - 1.0);
            painter->setPen(QPen(chip.accent, 1.0));
            painter->setBrush(palette.surface);
            painter->drawRoundedRect(chipRect, 2.5, 2.5);

            painter->drawPixmap(QRect(x + 4, chipTop + (chipHeight - glyphSize) / 2,
                                      glyphSize, glyphSize),
                                Icons::pixmap(chip.glyph, glyphSize, chip.accent));

            painter->setPen(chip.current ? chip.accent : palette.text);
            painter->setFont(chipFont);
            painter->drawText(QRect(x + glyphSize + 7, chipTop, width - glyphSize - 10,
                                    chipHeight),
                              Qt::AlignVCenter | Qt::AlignLeft, chip.text);
            x += width + 4;
        }

        const QString subject = index.data(Qt::DisplayRole).toString();
        if (!subject.isEmpty()) {
            QFont subjectFont = option.font;
            if (option.state.testFlag(QStyle::State_Selected)) {
                subjectFont.setBold(true);
            }
            painter->setFont(subjectFont);
            painter->setPen(option.state.testFlag(QStyle::State_Selected)
                                ? option.palette.color(QPalette::HighlightedText)
                                : palette.text);
            const QRect textRect(x + 2, option.rect.top(),
                                 option.rect.right() - x - 6, option.rect.height());
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                              QFontMetrics(subjectFont).elidedText(subject, Qt::ElideRight,
                                                                  textRect.width()));
        }
        painter->restore();
        return;
    }

    QStyledItemDelegate::paint(painter, option, index);

    const int nodeLane = index.data(CommitRoles::Lane).toInt();
    const QVariantList passEdges = index.data(CommitRoles::PassEdges).toList();
    const QVariantList parentLanes = index.data(CommitRoles::ParentLanes).toList();
    const bool hasIncoming = index.data(CommitRoles::HasIncoming).toBool();
    const bool uncommitted = index.data(CommitRoles::IsUncommitted).toBool();
    const bool isMerge = index.data(CommitRoles::IsMerge).toBool();

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
                              const int toLane, const int toY) {
        QPen pen(laneColor(qMax(fromLane, toLane)), 1.8);
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

    for (const QVariant &edgeValue : passEdges) {
        const QPoint edge = edgeValue.toPoint();
        drawEdge(edge.x(), topY, edge.y(), bottomY);
    }
    if (hasIncoming) {
        drawEdge(nodeLane, topY, nodeLane, centerY);
    }
    for (const QVariant &laneValue : parentLanes) {
        drawEdge(nodeLane, centerY, laneValue.toInt(), bottomY);
    }

    const QPointF center(laneX(nodeLane), centerY);
    if (uncommitted) {
        QPen pen(palette.mutedText, 1.6);
        pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(palette.surface);
        painter->drawEllipse(center, NodeRadius, NodeRadius);
    } else {
        painter->setPen(QPen(palette.graphNodeBorder, 1.6));
        painter->setBrush(laneColor(nodeLane));
        painter->drawEllipse(center, NodeRadius, NodeRadius);
        if (isMerge) {
            painter->setBrush(palette.graphNodeBorder);
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(center, NodeRadius - 2.4, NodeRadius - 2.4);
        }
    }
    painter->restore();
}
