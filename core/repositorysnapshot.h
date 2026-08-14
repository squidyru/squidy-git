// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include "gitclient.h"
#include "gittypes.h"

#include <QList>
#include <QString>

/// Data collected during one background refresh.
/// Individual Git commands are not an atomic repository snapshot.
struct RepositorySnapshot {
    /// Identifies the refresh request that produced this result.
    quint64 generation = 0;
    GitHistoryOptions historyOptions;

    QString headHash;
    QString currentBranch;
    QString userName;
    QString userEmail;
    GitRepositoryState state;
    int ahead = 0;
    int behind = 0;

    QList<GitBranchInfo> branches;
    QList<GitTagInfo> tags;
    QList<GitRemoteInfo> remotes;
    QList<GitStashInfo> stashes;
    QList<GitSubmoduleInfo> submodules;
    QList<GitFileStatus> files;
    QList<GitCommitInfo> commits;

    QString statusError;
    QString historyError;
};

/// Collects repository data on a worker thread.
[[nodiscard]] RepositorySnapshot collectRepositorySnapshot(GitClient git,
                                                           GitHistoryOptions options,
                                                           quint64 generation);
