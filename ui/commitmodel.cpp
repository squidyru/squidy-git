// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "commitmodel.h"

#include "commitgraph.h"
#include "theme.h"

#include <QCoreApplication>
#include <QLocale>

namespace {

QVariantList toVariantList(const QList<int> &values) {
    QVariantList result;
    result.reserve(values.size());
    for (const int value : values) {
        result.append(value);
    }
    return result;
}

}

// lupdate requires literal translate calls, so keep the history labels explicit.

QString formatCommitTimestamp(const QDateTime &moment) {
    if (!moment.isValid()) {
        return {};
    }
    return QLocale::system().toString(moment.toLocalTime(),
                                      QStringLiteral("d MMM yyyy H:mm"));
}

CommitModel::CommitModel(QObject *parent)
    : QAbstractTableModel(parent) {
}

void CommitModel::setHistory(const QList<GitCommitInfo> &commits,
                             const bool leadingUncommittedRow, const QString &headHash) {
    beginResetModel();
    commits_ = commits;
    headHash_ = headHash;
    leadingUncommittedRow_ = leadingUncommittedRow;
    rows_ = computeCommitGraph(commits_, leadingUncommittedRow_, headHash_);
    endResetModel();
}

bool CommitModel::isUncommittedRow(const int row) const {
    return leadingUncommittedRow_ && row == 0;
}

QString CommitModel::hashAt(const int row) const {
    const GitCommitInfo *commit = commitAt(row);
    return commit != nullptr ? commit->hash : QString();
}

int CommitModel::rowForHash(const QString &hash) const {
    if (hash.isEmpty()) {
        return -1;
    }
    const int offset = leadingUncommittedRow_ ? 1 : 0;
    for (int index = 0; index < commits_.size(); ++index) {
        if (commits_.at(index).hash == hash) {
            return index + offset;
        }
    }
    return -1;
}

const GitCommitInfo *CommitModel::commitAt(const int row) const {
    const int index = row - (leadingUncommittedRow_ ? 1 : 0);
    if (index < 0 || index >= commits_.size()) {
        return nullptr;
    }
    return &commits_.at(index);
}

int CommitModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(commits_.size()) + (leadingUncommittedRow_ ? 1 : 0);
}

int CommitModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant CommitModel::data(const QModelIndex &index, const int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }

    const int row = index.row();
    const bool uncommitted = isUncommittedRow(row);
    const GitCommitInfo *commit = commitAt(row);

    switch (role) {
        case CommitRoles::IsUncommitted:
            return uncommitted;
        case CommitRoles::Hash:
            return commit != nullptr ? commit->hash : QString();
        case CommitRoles::IsHead:
            return commit != nullptr && commit->hash == headHash_;
        case CommitRoles::References:
            return index.column() == Message && commit != nullptr
                       ? splitReferences(commit->references)
                       : QStringList();
        case CommitRoles::Subject:
            return commit != nullptr ? commit->subject : QString();
        default:
            break;
    }

    if (row < rows_.size()) {
        const GraphRow &graph = rows_.at(row);
        switch (role) {
            case CommitRoles::Lane:
                return graph.lane;
            case CommitRoles::LaneCount:
                return graph.laneCount;
            case CommitRoles::HasIncoming:
                return graph.hasIncoming;
            case CommitRoles::IsMerge:
                return graph.isMerge;
            case CommitRoles::PassEdges: {
                QVariantList edges;
                edges.reserve(graph.passEdges.size());
                for (const QPoint &edge : graph.passEdges) {
                    edges.append(edge);
                }
                return edges;
            }
            case CommitRoles::ParentLanes: {
                QVariantList lanes;
                lanes.reserve(graph.parentLanes.size());
                for (const int lane : graph.parentLanes) {
                    lanes.append(lane);
                }
                return lanes;
            }
            case CommitRoles::ColorIndex:
                return graph.colorIndex;
            case CommitRoles::PassColors:
                return toVariantList(graph.passColors);
            case CommitRoles::ParentColors:
                return toVariantList(graph.parentColors);
            default:
                break;
        }
    }

    if (role == Qt::ForegroundRole && index.column() == Commit) {
        return Theme::instance()->palette().mutedText;
    }

    if (role == Qt::ToolTipRole && index.column() == Message && commit != nullptr) {
        return commit->body.isEmpty()
                   ? commit->subject
                   : QStringLiteral("%1\n\n%2").arg(commit->subject, commit->body);
    }

    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
        case Message:
            return uncommitted
                       ? QCoreApplication::translate("RepositoryView", "Uncommitted changes")
                       : (commit != nullptr ? commit->subject : QString());
        case Date:
            return uncommitted ? formatCommitTimestamp(QDateTime::currentDateTime())
                               : (commit != nullptr ? formatCommitTimestamp(commit->authoredAt)
                                                    : QString());
        case Author:
            if (uncommitted) {
                return QStringLiteral("*");
            }
            if (commit == nullptr) {
                return QString();
            }
            return commit->authorEmail.isEmpty()
                       ? commit->author
                       : QStringLiteral("%1 <%2>").arg(commit->author, commit->authorEmail);
        case Commit:
            return uncommitted ? QStringLiteral("*")
                               : (commit != nullptr ? commit->shortHash : QString());
        default:
            return {};
    }
}

QVariant CommitModel::headerData(const int section, const Qt::Orientation orientation,
                                 const int role) const {
    if (orientation != Qt::Horizontal) {
        return {};
    }
    if (role == Qt::TextAlignmentRole) {
        return QVariant::fromValue(Qt::AlignCenter);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
        case Graph:
            return QCoreApplication::translate("RepositoryView", "Graph");
        case Message:
            return QCoreApplication::translate("RepositoryView", "Message");
        case Date:
            return QCoreApplication::translate("RepositoryView", "Date");
        case Author:
            return QCoreApplication::translate("RepositoryView", "Author");
        case Commit:
            return QCoreApplication::translate("RepositoryView", "Commit", "noun");
        default:
            return {};
    }
}
