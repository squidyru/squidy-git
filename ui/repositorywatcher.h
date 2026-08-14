// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QFileSystemWatcher;
class QTimer;

/// Watches Git metadata for external index, ref and operation-state changes.
/// Working-tree edits are discovered on the next refresh.
class RepositoryWatcher final : public QObject {
    Q_OBJECT

public:
    explicit RepositoryWatcher(QObject *parent = nullptr);

    void setRepository(const QString &gitDirectory);

    /// Suppresses notifications while the application runs a Git command.
    void setSuspended(bool suspended);

Q_SIGNALS:
    void repositoryChangedOnDisk();

private:
    void handleChange();
    void rearm();

    QFileSystemWatcher *watcher_ = nullptr;
    QTimer *settleTimer_ = nullptr;
    QString gitDirectory_;
    QStringList wantedPaths_;
    bool suspended_ = false;
};
