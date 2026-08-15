// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "graphlayout.h"

#include <QHash>

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
    // Lanes are reused, so colours stay attached to commit lines instead.
    QHash<QString, int> lineColors;
    int nextColor = 0;

    if (leadingUncommittedRow) {
        GraphRow row;
        row.lane = 0;
        row.laneCount = 1;
        row.hasIncoming = false;
        row.colorIndex = nextColor;
        if (!headHash.isEmpty()) {
            activeLanes.append(headHash);
            lineColors.insert(headHash, nextColor++);
            row.parentLanes.append(0);
            row.parentColors.append(row.colorIndex);
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
            lineColors.insert(commit.hash, nextColor++);
        }
        row.lane = nodeLane;
        row.colorIndex = lineColors.value(commit.hash);
        row.isMerge = commit.parents.size() > 1;

        // Replace this commit's lane with its first parent, appending extra
        // parents in fresh lanes so merges fan out to the right.
        lineColors.remove(commit.hash);
        activeLanes.removeAt(nodeLane);
        int insertionLane = nodeLane;
        bool firstParent = true;
        for (const QString &parent : commit.parents) {
            if (!activeLanes.contains(parent)) {
                activeLanes.insert(qMin(insertionLane, static_cast<int>(activeLanes.size())),
                                   parent);
                ++insertionLane;
                lineColors.insert(parent, firstParent ? row.colorIndex : nextColor++);
            }
            firstParent = false;
        }

        for (int lane = 0; lane < lanesBefore.size(); ++lane) {
            if (lane == nodeLane) {
                continue;
            }
            const int targetLane = static_cast<int>(activeLanes.indexOf(lanesBefore.at(lane)));
            if (targetLane >= 0) {
                row.passEdges.append(QPoint(lane, targetLane));
                row.passColors.append(lineColors.value(lanesBefore.at(lane)));
            }
        }

        for (const QString &parent : commit.parents) {
            const int parentLane = static_cast<int>(activeLanes.indexOf(parent));
            if (parentLane >= 0 && !row.parentLanes.contains(parentLane)) {
                row.parentLanes.append(parentLane);
                row.parentColors.append(lineColors.value(parent));
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
