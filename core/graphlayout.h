// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include "gittypes.h"

#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>

struct GraphRow {
    int lane = 0;
    int laneCount = 1;
    bool hasIncoming = false;
    bool isMerge = false;
    QList<QPoint> passEdges;
    QList<int> parentLanes;
    int colorIndex = 0;
    // Parallel to passEdges and parentLanes.
    QList<int> passColors;
    QList<int> parentColors;
};

/// Computes the lane layout drawn next to each commit.
[[nodiscard]] QList<GraphRow> computeCommitGraph(const QList<GitCommitInfo> &commits,
                                                 bool leadingUncommittedRow,
                                                 const QString &headHash);

/// Splits the raw "%D" decoration into individual refs.
[[nodiscard]] QStringList splitReferences(const QString &decoration);
