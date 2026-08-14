// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include "core/graphlayout.h"

#include <QList>
#include <QPoint>
#include <QStyledItemDelegate>

namespace CommitRoles {
inline constexpr int Hash = Qt::UserRole + 1;
inline constexpr int Lane = Qt::UserRole + 2;
inline constexpr int PassEdges = Qt::UserRole + 3;
inline constexpr int ParentLanes = Qt::UserRole + 4;
inline constexpr int HasIncoming = Qt::UserRole + 5;
inline constexpr int LaneCount = Qt::UserRole + 6;
inline constexpr int IsUncommitted = Qt::UserRole + 7;
inline constexpr int References = Qt::UserRole + 8;
inline constexpr int Subject = Qt::UserRole + 9;
inline constexpr int IsMerge = Qt::UserRole + 10;
inline constexpr int IsHead = Qt::UserRole + 11;
}

class CommitGraphDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit CommitGraphDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const override;

    [[nodiscard]] static int laneWidth();
    [[nodiscard]] static int graphWidthForLanes(int lanes);
};
