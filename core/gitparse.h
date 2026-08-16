// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include "gittypes.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

/// Parsers for machine-readable Git output.
namespace GitParse {

/// Must stay in sync with parseCommits().
[[nodiscard]] QStringList commitFormatArguments();

[[nodiscard]] QList<GitCommitInfo> parseCommits(const QByteArray &payload);

/// Reads "git ls-tree -z --long" for one directory level.
[[nodiscard]] QList<GitTreeEntry> parseTreeEntries(const QByteArray &payload);

/// Orders directories before files, then by name.
void sortTreeEntries(QList<GitTreeEntry> &entries);

/// Reads "git log --follow --name-status -z" with commitFormatArguments(),
/// keeping the path the file carried at each commit.
[[nodiscard]] QList<GitFileRevision> parseFileHistory(const QByteArray &payload);

/// Reads "git status --porcelain=v1 -z --untracked-files=all". Renames and
/// copies spend two records: the new path first, the original path second.
[[nodiscard]] QList<GitFileStatus> parseStatus(const QByteArray &payload);

[[nodiscard]] QList<GitBranchInfo> parseBranches(const QString &payload);
[[nodiscard]] QList<GitTagInfo> parseTags(const QString &payload);
[[nodiscard]] QList<GitStashInfo> parseStashes(const QString &payload);
[[nodiscard]] QList<GitSubmoduleInfo> parseSubmodules(const QString &payload);

/// Reads "git remote --verbose" output.
[[nodiscard]] QList<GitRemoteInfo> parseRemotes(const QString &payload);

/// Assigns short remote branch names to their remotes.
void assignRemoteBranches(QList<GitRemoteInfo> &remotes, const QString &payload);

/// Reads the "--name-status -z" output of a commit, where a rename spends
/// three records: the marker, the original path and the new path.
[[nodiscard]] QList<GitChangedFile> parseNameStatus(const QByteArray &payload,
                                                    bool renamesCarryOriginal);

/// Adds "--numstat" counts to files read from "--name-status".
void assignChangeCounts(QList<GitChangedFile> &files, const QString &payload);

/// Reads the "[ahead 1, behind 2]" field of "git for-each-ref".
void parseTrackInformation(const QString &track, int *ahead, int *behind);

/// Splits a NUL separated payload, dropping the trailing empty record.
[[nodiscard]] QList<QByteArray> splitNulRecords(const QByteArray &payload);

}
