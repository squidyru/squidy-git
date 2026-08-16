// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QByteArray>
#include <QChar>
#include <QDateTime>
#include <QString>
#include <QStringList>

// Value types shared by GitClient and the UI.

struct GitCommandResult {
    int exitCode = -1;
    QByteArray output;
    QByteArray errorOutput;
    QString processError;

    [[nodiscard]] bool succeeded() const;
    [[nodiscard]] QString outputText() const;
    [[nodiscard]] QString errorText() const;
    [[nodiscard]] QString reportText() const;
};

struct GitFileStatus {
    QString path;
    QString originalPath;
    QChar indexStatus = u' ';
    QChar workTreeStatus = u' ';

    [[nodiscard]] bool isUntracked() const;
    [[nodiscard]] bool isConflicted() const;
    [[nodiscard]] bool hasStagedChanges() const;
    [[nodiscard]] bool hasWorkingTreeChanges() const;
};

struct GitBranchInfo {
    QString name;
    QString upstream;
    QString hash;
    QString subject;
    QDateTime committedAt;
    int ahead = 0;
    int behind = 0;
    bool current = false;
};

struct GitRemoteInfo {
    QString name;
    QString url;
    QStringList branches;
};

struct GitTagInfo {
    QString name;
    QString hash;
    QString subject;
    QDateTime taggedAt;
};

struct GitStashInfo {
    int index = 0;
    QString reference;
    QString branch;
    QString message;
    QDateTime createdAt;
};

struct GitSubmoduleInfo {
    QString path;
    QString hash;
    QString describe;
};

struct GitCommitInfo {
    QString hash;
    QString shortHash;
    QStringList parents;
    QString author;
    QString authorEmail;
    QString committer;
    QString committerEmail;
    QDateTime authoredAt;
    QDateTime committedAt;
    QString references;
    QString subject;
    QString body;
};

/// One entry of a repository tree at a revision.
struct GitTreeEntry {
    QString name;
    /// Path from the repository root.
    QString path;
    QString hash;
    /// Blob size in bytes, -1 when unknown.
    qint64 size = -1;
    bool directory = false;
    bool submodule = false;
};

/// One point of a file timeline. The path is the one the file carried at that
/// commit, which is not necessarily the path it carries today.
struct GitFileRevision {
    GitCommitInfo commit;
    QString path;
    /// Set when the commit renamed or copied the file.
    QString previousPath;
    QChar status = u'M';

    [[nodiscard]] bool isAddition() const;
    [[nodiscard]] bool isDeletion() const;
    [[nodiscard]] bool isRename() const;
};

struct GitChangedFile {
    QString path;
    QString originalPath;
    QChar status = u'M';
    int additions = 0;
    int deletions = 0;
    bool binary = false;
};

struct GitRepositoryState {
    bool merging = false;
    bool rebasing = false;
    bool cherryPicking = false;
    bool reverting = false;
    bool bisecting = false;
    bool detachedHead = false;

    [[nodiscard]] bool isBusy() const;
    [[nodiscard]] QString description() const;
};

enum class GitResetMode {
    Soft,
    Mixed,
    Hard
};

enum class GitHistoryScope {
    CurrentBranch,
    AllBranches
};

struct GitHistoryOptions {
    GitHistoryScope scope = GitHistoryScope::AllBranches;
    int maximumCount = 500;
    QString revision;
    bool includeRemotes = true;
    bool dateOrder = true;

    [[nodiscard]] bool operator==(const GitHistoryOptions &other) const = default;
};

enum class GitSearchMode {
    Message,
    Author,
    FileContents,
    FilePath,
    Hash
};
