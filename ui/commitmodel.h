// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include "core/gittypes.h"
#include "core/graphlayout.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QList>
#include <QString>

[[nodiscard]] QString formatCommitTimestamp(const QDateTime &moment);

/// Model backing the commit history view.
class CommitModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        Graph = 0,
        Message,
        Date,
        Author,
        Commit,
        ColumnCount
    };

    explicit CommitModel(QObject *parent = nullptr);

    /// Optionally prepends the working copy above the newest commit.
    void setHistory(const QList<GitCommitInfo> &commits, bool leadingUncommittedRow,
                    const QString &headHash);

    [[nodiscard]] bool isUncommittedRow(int row) const;
    [[nodiscard]] QString hashAt(int row) const;
    [[nodiscard]] int rowForHash(const QString &hash) const;
    [[nodiscard]] const GitCommitInfo *commitAt(int row) const;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role) const override;

private:
    QList<GitCommitInfo> commits_;
    QList<GraphRow> rows_;
    QString headHash_;
    bool leadingUncommittedRow_ = false;
};
