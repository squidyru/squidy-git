// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "gitclient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr int GitStartTimeoutMs = 5'000;
constexpr int GitNetworkTimeoutMs = 10 * 60'000;
constexpr qint64 MaximumPreviewSize = 2 * 1024 * 1024;

const QChar FieldSeparator = QChar(0x1f);
const char RecordSeparator = '\x1e';

QStringList commitFormatArguments() {
    return {
        QStringLiteral("--date=iso-strict"),
        QStringLiteral("--decorate=short"),
        QStringLiteral("--pretty=format:%x1e%H%x1f%h%x1f%P%x1f%an%x1f%ae%x1f%cn%x1f%ce"
                       "%x1f%aI%x1f%cI%x1f%D%x1f%s%x1f%b")
    };
}

QList<GitCommitInfo> parseCommits(const QByteArray &payload) {
    QList<GitCommitInfo> commits;
    const QList<QByteArray> records = payload.split(RecordSeparator);
    for (const QByteArray &record : records) {
        if (record.trimmed().isEmpty()) {
            continue;
        }

        const QStringList fields = QString::fromUtf8(record).split(FieldSeparator,
                                                                   Qt::KeepEmptyParts);
        if (fields.size() < 12) {
            continue;
        }

        GitCommitInfo commit;
        commit.hash = fields.at(0).trimmed();
        commit.shortHash = fields.at(1);
        commit.parents = fields.at(2).split(u' ', Qt::SkipEmptyParts);
        commit.author = fields.at(3);
        commit.authorEmail = fields.at(4);
        commit.committer = fields.at(5);
        commit.committerEmail = fields.at(6);
        commit.authoredAt = QDateTime::fromString(fields.at(7), Qt::ISODate);
        commit.committedAt = QDateTime::fromString(fields.at(8), Qt::ISODate);
        commit.references = fields.at(9);
        commit.subject = fields.at(10);
        commit.body = fields.mid(11).join(FieldSeparator).trimmed();
        commits.append(commit);
    }
    return commits;
}

void parseTrackInformation(const QString &track, int *ahead, int *behind) {
    *ahead = 0;
    *behind = 0;
    if (track.isEmpty()) {
        return;
    }

    static const QRegularExpression aheadPattern(QStringLiteral("ahead (\\d+)"));
    static const QRegularExpression behindPattern(QStringLiteral("behind (\\d+)"));
    const QRegularExpressionMatch aheadMatch = aheadPattern.match(track);
    if (aheadMatch.hasMatch()) {
        *ahead = aheadMatch.captured(1).toInt();
    }
    const QRegularExpressionMatch behindMatch = behindPattern.match(track);
    if (behindMatch.hasMatch()) {
        *behind = behindMatch.captured(1).toInt();
    }
}

/// Splits a NUL separated payload, dropping the trailing empty record.
QList<QByteArray> splitNulRecords(const QByteArray &payload) {
    QList<QByteArray> records = payload.split('\0');
    while (!records.isEmpty() && records.constLast().isEmpty()) {
        records.removeLast();
    }
    return records;
}
}

bool GitCommandResult::succeeded() const {
    return processError.isEmpty() && exitCode == 0;
}

QString GitCommandResult::outputText() const {
    return QString::fromUtf8(output);
}

QString GitCommandResult::errorText() const {
    if (!processError.isEmpty()) {
        return processError;
    }

    const QString gitError = QString::fromUtf8(errorOutput).trimmed();
    if (!gitError.isEmpty()) {
        return gitError;
    }

    const QString gitOutput = outputText().trimmed();
    if (!gitOutput.isEmpty()) {
        return gitOutput;
    }

    return GitClient::tr("Git exited with code %1").arg(exitCode);
}

QString GitCommandResult::reportText() const {
    QStringList parts;
    const QString standardOutput = outputText().trimmed();
    const QString standardError = QString::fromUtf8(errorOutput).trimmed();
    if (!standardOutput.isEmpty()) {
        parts.append(standardOutput);
    }
    if (!standardError.isEmpty()) {
        parts.append(standardError);
    }
    if (!processError.isEmpty()) {
        parts.append(processError);
    }
    return parts.join(u'\n');
}

bool GitFileStatus::isUntracked() const {
    return indexStatus == u'?' && workTreeStatus == u'?';
}

bool GitFileStatus::isConflicted() const {
    return indexStatus == u'U' || workTreeStatus == u'U'
           || (indexStatus == u'A' && workTreeStatus == u'A')
           || (indexStatus == u'D' && workTreeStatus == u'D');
}

bool GitFileStatus::hasStagedChanges() const {
    return indexStatus != u' ' && indexStatus != u'?' && !isConflicted();
}

bool GitFileStatus::hasWorkingTreeChanges() const {
    return workTreeStatus != u' ' || isUntracked() || isConflicted();
}

bool GitRepositoryState::isBusy() const {
    return merging || rebasing || cherryPicking || reverting || bisecting;
}

QString GitRepositoryState::description() const {
    if (merging) return GitClient::tr("Merge in progress");
    if (rebasing) return GitClient::tr("Rebase in progress");
    if (cherryPicking) return GitClient::tr("Cherry-pick in progress");
    if (reverting) return GitClient::tr("Revert in progress");
    if (bisecting) return GitClient::tr("Bisect in progress");
    if (detachedHead) return GitClient::tr("Detached HEAD");
    return {};
}

GitLog::GitLog(QObject *parent)
    : QObject(parent) {
}

GitLog *GitLog::instance() {
    static GitLog log;
    return &log;
}

void GitLog::record(const QString &workingDirectory, const QStringList &arguments,
                    const GitCommandResult &result) {
    Q_EMIT commandRecorded(workingDirectory,
                           QStringLiteral("git %1").arg(arguments.join(u' ')),
                           result.reportText(), result.succeeded());
}

GitCommandResult GitClient::openRepository(const QString &directory) {
    GitCommandResult result = runAt(directory, {
        QStringLiteral("rev-parse"),
        QStringLiteral("--show-toplevel")
    });

    if (result.succeeded()) {
        repositoryRoot_ = QDir::cleanPath(result.outputText().trimmed());
    }

    return result;
}

bool GitClient::hasRepository() const {
    return !repositoryRoot_.isEmpty();
}

const QString &GitClient::repositoryRoot() const {
    return repositoryRoot_;
}

QString GitClient::repositoryName() const {
    const QFileInfo info(repositoryRoot_);
    return info.fileName().isEmpty() ? repositoryRoot_ : info.fileName();
}

bool GitClient::isRepository(const QString &directory) {
    if (directory.isEmpty() || !QDir(directory).exists()) {
        return false;
    }
    return runAt(directory, {
        QStringLiteral("rev-parse"),
        QStringLiteral("--is-inside-work-tree")
    }).succeeded();
}

GitCommandResult GitClient::initRepository(const QString &directory, const bool bare) {
    QDir().mkpath(directory);
    QStringList arguments{QStringLiteral("init")};
    if (bare) {
        arguments.append(QStringLiteral("--bare"));
    }
    return runAt(directory, arguments);
}

QString GitClient::gitExecutable() {
    static const QString executable = QStandardPaths::findExecutable(QStringLiteral("git"));
    return executable;
}

QList<GitFileStatus> GitClient::status(QString *errorMessage) const {
    const GitCommandResult result = run({
        QStringLiteral("status"),
        QStringLiteral("--porcelain=v1"),
        QStringLiteral("-z"),
        QStringLiteral("--untracked-files=all")
    });

    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorText();
        }
        return {};
    }

    QList<GitFileStatus> files;
    const QList<QByteArray> records = splitNulRecords(result.output);

    for (qsizetype index = 0; index < records.size(); ++index) {
        const QByteArray &record = records.at(index);
        if (record.size() < 4) {
            continue;
        }

        GitFileStatus file;
        file.indexStatus = QChar::fromLatin1(record.at(0));
        file.workTreeStatus = QChar::fromLatin1(record.at(1));
        file.path = QString::fromUtf8(record.mid(3));

        const bool isRenameOrCopy = file.indexStatus == u'R' || file.indexStatus == u'C'
                                    || file.workTreeStatus == u'R' || file.workTreeStatus == u'C';
        if (isRenameOrCopy && index + 1 < records.size()) {
            file.originalPath = QString::fromUtf8(records.at(++index));
        }

        files.append(file);
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return files;
}

QString GitClient::currentBranch(QString *errorMessage) const {
    GitCommandResult result = run({
        QStringLiteral("symbolic-ref"),
        QStringLiteral("--quiet"),
        QStringLiteral("--short"),
        QStringLiteral("HEAD")
    });

    if (result.succeeded()) {
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return result.outputText().trimmed();
    }

    result = run({
        QStringLiteral("rev-parse"),
        QStringLiteral("--short"),
        QStringLiteral("HEAD")
    });

    if (result.succeeded()) {
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return QStringLiteral("(detached %1)").arg(result.outputText().trimmed());
    }

    if (errorMessage != nullptr) {
        *errorMessage = result.errorText();
    }
    return {};
}

QString GitClient::headHash() const {
    const GitCommandResult result = run({
        QStringLiteral("rev-parse"),
        QStringLiteral("--verify"),
        QStringLiteral("HEAD")
    });
    return result.succeeded() ? result.outputText().trimmed() : QString();
}

bool GitClient::hasCommits() const {
    return !headHash().isEmpty();
}

GitRepositoryState GitClient::repositoryState() const {
    GitRepositoryState state;
    const GitCommandResult gitDirResult = run({
        QStringLiteral("rev-parse"),
        QStringLiteral("--absolute-git-dir")
    });
    if (!gitDirResult.succeeded()) {
        return state;
    }

    const QDir gitDirectory(gitDirResult.outputText().trimmed());
    state.merging = QFile::exists(gitDirectory.filePath(QStringLiteral("MERGE_HEAD")));
    state.rebasing = QFile::exists(gitDirectory.filePath(QStringLiteral("rebase-merge")))
                     || QFile::exists(gitDirectory.filePath(QStringLiteral("rebase-apply")));
    state.cherryPicking = QFile::exists(gitDirectory.filePath(QStringLiteral("CHERRY_PICK_HEAD")));
    state.reverting = QFile::exists(gitDirectory.filePath(QStringLiteral("REVERT_HEAD")));
    state.bisecting = QFile::exists(gitDirectory.filePath(QStringLiteral("BISECT_LOG")));
    state.detachedHead = !run({
        QStringLiteral("symbolic-ref"),
        QStringLiteral("--quiet"),
        QStringLiteral("HEAD")
    }).succeeded() && hasCommits();
    return state;
}

QList<GitBranchInfo> GitClient::branches(QString *errorMessage) const {
    const GitCommandResult result = run({
        QStringLiteral("for-each-ref"),
        QStringLiteral("--sort=refname"),
        QStringLiteral("--format=%(HEAD)%01%(refname:short)%01%(objectname)%01"
                       "%(upstream:short)%01%(upstream:track)%01%(contents:subject)%01"
                       "%(committerdate:iso-strict)"),
        QStringLiteral("refs/heads")
    });

    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorText();
        }
        return {};
    }

    QList<GitBranchInfo> branches;
    const QStringList lines = result.outputText().split(u'\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QChar(0x01));
        if (fields.size() < 7) {
            continue;
        }

        GitBranchInfo branch;
        branch.current = fields.at(0).trimmed() == QStringLiteral("*");
        branch.name = fields.at(1);
        branch.hash = fields.at(2);
        branch.upstream = fields.at(3);
        parseTrackInformation(fields.at(4), &branch.ahead, &branch.behind);
        branch.subject = fields.at(5);
        branch.committedAt = QDateTime::fromString(fields.at(6), Qt::ISODate);
        if (!branch.name.isEmpty()) {
            branches.append(branch);
        }
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return branches;
}

QList<GitRemoteInfo> GitClient::remotes(QString *errorMessage) const {
    const GitCommandResult result = run({
        QStringLiteral("remote"),
        QStringLiteral("--verbose")
    });
    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorText();
        }
        return {};
    }

    QList<GitRemoteInfo> remotes;
    const QStringList lines = result.outputText().split(u'\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                              Qt::SkipEmptyParts);
        if (fields.size() < 2) {
            continue;
        }
        const auto existing = std::find_if(remotes.begin(), remotes.end(),
                                           [&fields](const GitRemoteInfo &remote) {
                                               return remote.name == fields.at(0);
                                           });
        if (existing == remotes.end()) {
            GitRemoteInfo remote;
            remote.name = fields.at(0);
            remote.url = fields.at(1);
            remotes.append(remote);
        }
    }

    const GitCommandResult branchResult = run({
        QStringLiteral("for-each-ref"),
        QStringLiteral("--sort=refname"),
        QStringLiteral("--format=%(refname:short)"),
        QStringLiteral("refs/remotes")
    });
    if (branchResult.succeeded()) {
        const QStringList references = branchResult.outputText().split(u'\n', Qt::SkipEmptyParts);
        for (const QString &reference : references) {
            if (reference.endsWith(QStringLiteral("/HEAD"))) {
                continue;
            }
            const qsizetype separator = reference.indexOf(u'/');
            if (separator <= 0) {
                continue;
            }
            const QString remoteName = reference.left(separator);
            for (GitRemoteInfo &remote : remotes) {
                if (remote.name == remoteName) {
                    remote.branches.append(reference.mid(separator + 1));
                    break;
                }
            }
        }
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return remotes;
}

QList<GitTagInfo> GitClient::tags(QString *errorMessage) const {
    const GitCommandResult result = run({
        QStringLiteral("for-each-ref"),
        QStringLiteral("--sort=-creatordate"),
        QStringLiteral("--format=%(refname:short)%01%(objectname)%01%(contents:subject)%01"
                       "%(creatordate:iso-strict)"),
        QStringLiteral("refs/tags")
    });

    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorText();
        }
        return {};
    }

    QList<GitTagInfo> tags;
    const QStringList lines = result.outputText().split(u'\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QChar(0x01));
        if (fields.size() < 4) {
            continue;
        }
        GitTagInfo tag;
        tag.name = fields.at(0);
        tag.hash = fields.at(1);
        tag.subject = fields.at(2);
        tag.taggedAt = QDateTime::fromString(fields.at(3), Qt::ISODate);
        tags.append(tag);
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return tags;
}

QList<GitStashInfo> GitClient::stashes(QString *errorMessage) const {
    const GitCommandResult result = run({
        QStringLiteral("stash"),
        QStringLiteral("list"),
        QStringLiteral("--pretty=format:%gd%01%gs%01%aI")
    });

    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorText();
        }
        return {};
    }

    QList<GitStashInfo> stashes;
    const QStringList lines = result.outputText().split(u'\n', Qt::SkipEmptyParts);
    int index = 0;
    for (const QString &line : lines) {
        const QStringList fields = line.split(QChar(0x01));
        if (fields.size() < 3) {
            continue;
        }

        GitStashInfo stash;
        stash.index = index++;
        stash.reference = fields.at(0);
        stash.createdAt = QDateTime::fromString(fields.at(2), Qt::ISODate);

        QString subject = fields.at(1);
        static const QRegularExpression prefixPattern(
            QStringLiteral("^(?:WIP on|On) ([^:]+):\\s*"));
        const QRegularExpressionMatch match = prefixPattern.match(subject);
        if (match.hasMatch()) {
            stash.branch = match.captured(1);
            subject = subject.mid(match.capturedLength());
        }
        stash.message = subject;
        stashes.append(stash);
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return stashes;
}

QList<GitSubmoduleInfo> GitClient::submodules() const {
    const GitCommandResult result = run({
        QStringLiteral("submodule"),
        QStringLiteral("status")
    });
    if (!result.succeeded()) {
        return {};
    }

    QList<GitSubmoduleInfo> submodules;
    const QStringList lines = result.outputText().split(u'\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.trimmed().split(QRegularExpression(QStringLiteral("\\s+")),
                                                        Qt::SkipEmptyParts);
        if (fields.size() < 2) {
            continue;
        }
        GitSubmoduleInfo submodule;
        submodule.hash = fields.at(0);
        submodule.path = fields.at(1);
        if (fields.size() > 2) {
            submodule.describe = fields.at(2);
        }
        submodules.append(submodule);
    }
    return submodules;
}

QList<GitCommitInfo> GitClient::history(const GitHistoryOptions &options,
                                        QString *errorMessage) const {
    if (!hasCommits()) {
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return {};
    }

    QStringList arguments{
        QStringLiteral("log"),
        options.dateOrder ? QStringLiteral("--date-order") : QStringLiteral("--topo-order"),
        QStringLiteral("--max-count=%1").arg(qMax(1, options.maximumCount))
    };
    arguments.append(commitFormatArguments());

    if (!options.revision.isEmpty()) {
        arguments.append(options.revision);
    } else {
        arguments.append(QStringLiteral("HEAD"));
        if (options.scope == GitHistoryScope::AllBranches) {
            arguments.append(QStringLiteral("--branches"));
            if (options.includeRemotes) {
                arguments.append(QStringLiteral("--remotes"));
            }
        }
    }

    const GitCommandResult result = run(arguments);
    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorText();
        }
        return {};
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return parseCommits(result.output);
}

QList<GitCommitInfo> GitClient::search(const GitSearchMode mode, const QString &query,
                                       const int maximumCount, QString *errorMessage) const {
    if (query.trimmed().isEmpty() || !hasCommits()) {
        return {};
    }

    QStringList arguments{
        QStringLiteral("log"),
        QStringLiteral("--topo-order"),
        QStringLiteral("--max-count=%1").arg(qMax(1, maximumCount))
    };
    arguments.append(commitFormatArguments());

    switch (mode) {
        case GitSearchMode::Message:
            arguments.append(QStringLiteral("--all"));
            arguments.append(QStringLiteral("--regexp-ignore-case"));
            arguments.append(QStringLiteral("--grep=%1").arg(query));
            break;
        case GitSearchMode::Author:
            arguments.append(QStringLiteral("--all"));
            arguments.append(QStringLiteral("--regexp-ignore-case"));
            arguments.append(QStringLiteral("--author=%1").arg(query));
            break;
        case GitSearchMode::FileContents:
            arguments.append(QStringLiteral("--all"));
            arguments.append(QStringLiteral("-S%1").arg(query));
            break;
        case GitSearchMode::FilePath:
            arguments.append(QStringLiteral("--all"));
            arguments.append(QStringLiteral("--"));
            arguments.append(QStringLiteral(":(icase,glob)**%1**").arg(query));
            break;
        case GitSearchMode::Hash: {
            const GitCommandResult verification = run({
                QStringLiteral("rev-parse"),
                QStringLiteral("--verify"),
                QStringLiteral("--quiet"),
                QStringLiteral("%1^{commit}").arg(query.trimmed())
            });
            if (!verification.succeeded()) {
                if (errorMessage != nullptr) {
                    *errorMessage = tr("Commit “%1” was not found.").arg(query.trimmed());
                }
                return {};
            }
            arguments.append(verification.outputText().trimmed());
            break;
        }
    }

    const GitCommandResult result = run(arguments);
    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorText();
        }
        return {};
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return parseCommits(result.output);
}

QList<GitChangedFile> GitClient::commitFiles(const QString &hash) const {
    if (hash.isEmpty()) {
        return {};
    }

    const GitCommandResult nameStatus = run({
        QStringLiteral("show"),
        QStringLiteral("--format="),
        QStringLiteral("--name-status"),
        QStringLiteral("--find-renames"),
        QStringLiteral("--no-ext-diff"),
        QStringLiteral("-m"),
        QStringLiteral("--first-parent"),
        QStringLiteral("-z"),
        hash
    });
    if (!nameStatus.succeeded()) {
        return {};
    }

    QList<GitChangedFile> files;
    const QList<QByteArray> records = splitNulRecords(nameStatus.output);
    for (qsizetype index = 0; index < records.size(); ++index) {
        const QString marker = QString::fromUtf8(records.at(index)).trimmed();
        if (marker.isEmpty()) {
            continue;
        }

        GitChangedFile file;
        file.status = marker.at(0);
        const bool isRenameOrCopy = file.status == u'R' || file.status == u'C';
        if (isRenameOrCopy) {
            if (index + 2 >= records.size()) {
                break;
            }
            file.originalPath = QString::fromUtf8(records.at(++index));
            file.path = QString::fromUtf8(records.at(++index));
        } else {
            if (index + 1 >= records.size()) {
                break;
            }
            file.path = QString::fromUtf8(records.at(++index));
        }
        files.append(file);
    }

    const GitCommandResult numStat = run({
        QStringLiteral("show"),
        QStringLiteral("--format="),
        QStringLiteral("--numstat"),
        QStringLiteral("--find-renames"),
        QStringLiteral("--no-ext-diff"),
        QStringLiteral("-m"),
        QStringLiteral("--first-parent"),
        hash
    });
    if (numStat.succeeded()) {
        const QStringList lines = numStat.outputText().split(u'\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList fields = line.split(u'\t');
            if (fields.size() < 3) {
                continue;
            }
            const QString path = fields.constLast();
            for (GitChangedFile &file : files) {
                if (file.path != path && !path.contains(QStringLiteral(" => "))) {
                    continue;
                }
                if (path.contains(QStringLiteral(" => ")) && !path.contains(file.path)) {
                    continue;
                }
                file.binary = fields.at(0) == QStringLiteral("-");
                file.additions = fields.at(0).toInt();
                file.deletions = fields.at(1).toInt();
                break;
            }
        }
    }

    return files;
}

QList<GitChangedFile> GitClient::stashFiles(const int index) const {
    const GitCommandResult result = run({
        QStringLiteral("stash"),
        QStringLiteral("show"),
        QStringLiteral("--name-status"),
        QStringLiteral("--no-ext-diff"),
        QStringLiteral("-z"),
        QStringLiteral("stash@{%1}").arg(index)
    });
    if (!result.succeeded()) {
        return {};
    }

    QList<GitChangedFile> files;
    const QList<QByteArray> records = splitNulRecords(result.output);
    for (qsizetype position = 0; position + 1 < records.size(); position += 2) {
        const QString marker = QString::fromUtf8(records.at(position)).trimmed();
        if (marker.isEmpty()) {
            continue;
        }
        GitChangedFile file;
        file.status = marker.at(0);
        file.path = QString::fromUtf8(records.at(position + 1));
        files.append(file);
    }
    return files;
}

QString GitClient::upstreamOf(const QString &branch) const {
    const GitCommandResult result = run({
        QStringLiteral("rev-parse"),
        QStringLiteral("--abbrev-ref"),
        QStringLiteral("--symbolic-full-name"),
        QStringLiteral("%1@{upstream}").arg(branch)
    });
    return result.succeeded() ? result.outputText().trimmed() : QString();
}

QString GitClient::userName() const {
    return configValue(QStringLiteral("user.name"));
}

QString GitClient::userEmail() const {
    return configValue(QStringLiteral("user.email"));
}

QString GitClient::lastCommitMessage() const {
    const GitCommandResult result = run({
        QStringLiteral("log"),
        QStringLiteral("-1"),
        QStringLiteral("--pretty=format:%B")
    });
    return result.succeeded() ? result.outputText().trimmed() : QString();
}

GitCommandResult GitClient::diff(const QString &path, const bool staged, const bool untracked,
                                 const int contextLines) const {
    if (!staged && untracked) {
        GitCommandResult result;
        const QFileInfo fileInfo(QDir(repositoryRoot_).filePath(path));

        if (!fileInfo.exists() || !fileInfo.isFile()) {
            result.processError = tr("The file no longer exists: %1").arg(path);
            return result;
        }

        if (fileInfo.size() > MaximumPreviewSize) {
            result.exitCode = 0;
            result.output = tr("The new file is too large to preview (%1 MB).")
                                .arg(fileInfo.size() / (1024.0 * 1024.0), 0, 'f', 1)
                                .toUtf8();
            return result;
        }

        // Render untracked files through git so the viewer receives a real patch.
        // "diff --no-index" reports differences with exit code 1, which is not a failure here.
        GitCommandResult patch = run({
            QStringLiteral("diff"),
            QStringLiteral("--no-ext-diff"),
            QStringLiteral("--no-color"),
            QStringLiteral("--no-index"),
            QStringLiteral("--unified=%1").arg(qMax(0, contextLines)),
            QStringLiteral("--"),
            QStringLiteral("/dev/null"),
            path
        });
        if (patch.exitCode == 1 && patch.processError.isEmpty() && !patch.output.isEmpty()) {
            patch.exitCode = 0;
        }
        return patch;
    }

    QStringList arguments{
        QStringLiteral("diff"),
        QStringLiteral("--no-ext-diff"),
        QStringLiteral("--no-color"),
        QStringLiteral("--find-renames"),
        QStringLiteral("--unified=%1").arg(qMax(0, contextLines))
    };
    if (staged) {
        arguments.append(QStringLiteral("--cached"));
    }
    arguments.append(QStringLiteral("--"));
    arguments.append(path);
    return run(arguments);
}

GitCommandResult GitClient::commitDiff(const QString &hash, const QString &path,
                                       const int contextLines) const {
    QStringList arguments{
        QStringLiteral("show"),
        QStringLiteral("--format="),
        QStringLiteral("--patch"),
        QStringLiteral("--find-renames"),
        QStringLiteral("--no-ext-diff"),
        QStringLiteral("--no-color"),
        QStringLiteral("--unified=%1").arg(qMax(0, contextLines)),
        QStringLiteral("-m"),
        QStringLiteral("--first-parent"),
        hash
    };
    if (!path.isEmpty()) {
        arguments.append(QStringLiteral("--"));
        arguments.append(path);
    }
    return run(arguments);
}

GitCommandResult GitClient::stashDiff(const int index, const QString &path,
                                      const int contextLines) const {
    QStringList arguments{
        QStringLiteral("diff"),
        QStringLiteral("--no-ext-diff"),
        QStringLiteral("--no-color"),
        QStringLiteral("--unified=%1").arg(qMax(0, contextLines)),
        QStringLiteral("stash@{%1}^1").arg(index),
        QStringLiteral("stash@{%1}").arg(index)
    };
    if (!path.isEmpty()) {
        arguments.append(QStringLiteral("--"));
        arguments.append(path);
    }
    return run(arguments);
}

GitCommandResult GitClient::showCommit(const QString &hash) const {
    return run({
        QStringLiteral("show"),
        QStringLiteral("--max-count=1"),
        QStringLiteral("--format=fuller"),
        QStringLiteral("--decorate=short"),
        QStringLiteral("--stat"),
        QStringLiteral("--no-patch"),
        hash
    });
}

GitCommandResult GitClient::stage(const QStringList &paths) const {
    if (paths.isEmpty()) {
        GitCommandResult result;
        result.processError = tr("No files are selected for staging.");
        return result;
    }

    QStringList arguments{QStringLiteral("add"), QStringLiteral("--all"), QStringLiteral("--")};
    arguments.append(paths);
    return run(arguments);
}

GitCommandResult GitClient::unstage(const QStringList &paths) const {
    if (paths.isEmpty()) {
        GitCommandResult result;
        result.processError = tr("No files are selected for unstaging.");
        return result;
    }

    QStringList arguments;
    if (hasCommits()) {
        arguments = {
            QStringLiteral("restore"),
            QStringLiteral("--staged"),
            QStringLiteral("--")
        };
    } else {
        // In a repository without commits there is no HEAD to restore from.
        // Removing only the index entries leaves working-tree files untouched.
        arguments = {
            QStringLiteral("rm"),
            QStringLiteral("--cached"),
            QStringLiteral("--quiet"),
            QStringLiteral("-r"),
            QStringLiteral("-f"),
            QStringLiteral("--")
        };
    }
    arguments.append(paths);
    return run(arguments);
}

GitCommandResult GitClient::stageAll() const {
    return run({QStringLiteral("add"), QStringLiteral("--all")});
}

GitCommandResult GitClient::unstageAll() const {
    if (hasCommits()) {
        return run({QStringLiteral("reset"), QStringLiteral("--mixed"), QStringLiteral("HEAD")});
    }
    return run({QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"),
                QStringLiteral("-f"), QStringLiteral("--quiet"), QStringLiteral(".")});
}

GitCommandResult GitClient::discard(const QStringList &paths, const bool untracked) const {
    if (paths.isEmpty()) {
        GitCommandResult result;
        result.processError = tr("No files are selected.");
        return result;
    }

    QStringList arguments;
    if (untracked) {
        arguments = {
            QStringLiteral("clean"),
            QStringLiteral("--force"),
            QStringLiteral("-d"),
            QStringLiteral("--")
        };
    } else if (hasCommits()) {
        arguments = {
            QStringLiteral("restore"),
            QStringLiteral("--worktree"),
            QStringLiteral("--staged"),
            QStringLiteral("--source=HEAD"),
            QStringLiteral("--")
        };
    } else {
        arguments = {
            QStringLiteral("rm"),
            QStringLiteral("--cached"),
            QStringLiteral("--quiet"),
            QStringLiteral("-r"),
            QStringLiteral("-f"),
            QStringLiteral("--")
        };
    }
    arguments.append(paths);
    return run(arguments);
}

GitCommandResult GitClient::applyPatch(const QByteArray &patch, const bool cached,
                                       const bool reverse) const {
    QStringList arguments{
        QStringLiteral("apply"),
        QStringLiteral("--whitespace=nowarn"),
        QStringLiteral("--recount")
    };
    if (cached) {
        arguments.append(QStringLiteral("--cached"));
    }
    if (reverse) {
        arguments.append(QStringLiteral("--reverse"));
    }
    arguments.append(QStringLiteral("-"));

    QByteArray payload = patch;
    if (!payload.endsWith('\n')) {
        payload.append('\n');
    }
    return runWithInput(arguments, payload);
}

GitCommandResult GitClient::resolveWith(const QStringList &paths, const bool useMine) const {
    if (paths.isEmpty()) {
        GitCommandResult result;
        result.processError = tr("No files are selected.");
        return result;
    }

    QStringList arguments{
        QStringLiteral("checkout"),
        useMine ? QStringLiteral("--ours") : QStringLiteral("--theirs"),
        QStringLiteral("--")
    };
    arguments.append(paths);
    const GitCommandResult checkout = run(arguments);
    if (!checkout.succeeded()) {
        return checkout;
    }
    return stage(paths);
}

GitCommandResult GitClient::ignore(const QStringList &patterns) const {
    GitCommandResult result;
    if (patterns.isEmpty()) {
        result.processError = tr("There is nothing to add to .gitignore.");
        return result;
    }

    QFile ignoreFile(QDir(repositoryRoot_).filePath(QStringLiteral(".gitignore")));
    if (!ignoreFile.open(QIODevice::ReadWrite | QIODevice::Text)) {
        result.processError = ignoreFile.errorString();
        return result;
    }

    const QString existing = QString::fromUtf8(ignoreFile.readAll());
    const QStringList existingLines = existing.split(u'\n');
    QString appended;
    if (!existing.isEmpty() && !existing.endsWith(u'\n')) {
        appended.append(u'\n');
    }
    for (const QString &pattern : patterns) {
        if (!existingLines.contains(pattern)) {
            appended.append(pattern).append(u'\n');
        }
    }

    if (!appended.isEmpty()) {
        ignoreFile.write(appended.toUtf8());
    }
    ignoreFile.close();

    result.exitCode = 0;
    result.output = tr("Updated .gitignore").toUtf8();
    return result;
}

GitCommandResult GitClient::commit(const QString &message, const bool amend,
                                   const QString &author) const {
    QStringList arguments{QStringLiteral("commit")};
    if (amend) {
        arguments.append(QStringLiteral("--amend"));
    }
    if (!author.trimmed().isEmpty()) {
        arguments.append(QStringLiteral("--author=%1").arg(author.trimmed()));
    }
    arguments.append(QStringLiteral("--file=-"));
    return runWithInput(arguments, message.toUtf8());
}

GitCommandResult GitClient::checkoutBranch(const QString &name) const {
    return run({QStringLiteral("checkout"), name});
}

GitCommandResult GitClient::checkoutRevision(const QString &revision) const {
    return run({QStringLiteral("checkout"), QStringLiteral("--detach"), revision});
}

GitCommandResult GitClient::checkoutRemoteBranch(const QString &remoteBranch,
                                                 const QString &localName) const {
    return run({
        QStringLiteral("checkout"),
        QStringLiteral("-b"),
        localName,
        QStringLiteral("--track"),
        remoteBranch
    });
}

GitCommandResult GitClient::createBranch(const QString &name, const QString &startPoint,
                                         const bool checkout) const {
    const GitCommandResult validation = run({
        QStringLiteral("check-ref-format"),
        QStringLiteral("--branch"),
        name
    });
    if (!validation.succeeded()) {
        GitCommandResult result = validation;
        result.processError = tr("Invalid branch name: %1").arg(name);
        return result;
    }

    QStringList arguments;
    if (checkout) {
        arguments = {QStringLiteral("checkout"), QStringLiteral("-b"), name};
    } else {
        arguments = {QStringLiteral("branch"), name};
    }
    if (!startPoint.isEmpty()) {
        arguments.append(startPoint);
    }
    return run(arguments);
}

GitCommandResult GitClient::deleteBranch(const QString &name, const bool force) const {
    return run({
        QStringLiteral("branch"),
        force ? QStringLiteral("-D") : QStringLiteral("-d"),
        name
    });
}

GitCommandResult GitClient::deleteRemoteBranch(const QString &remote,
                                               const QString &branch) const {
    return run({
        QStringLiteral("push"),
        remote,
        QStringLiteral("--delete"),
        branch
    }, GitNetworkTimeoutMs);
}

GitCommandResult GitClient::renameBranch(const QString &oldName, const QString &newName) const {
    return run({QStringLiteral("branch"), QStringLiteral("-m"), oldName, newName});
}

GitCommandResult GitClient::merge(const QString &revision, const bool noFastForward,
                                  const bool squash, const bool commitResult) const {
    QStringList arguments{QStringLiteral("merge")};
    if (squash) {
        arguments.append(QStringLiteral("--squash"));
    } else if (noFastForward) {
        arguments.append(QStringLiteral("--no-ff"));
    }
    if (!squash) {
        arguments.append(commitResult ? QStringLiteral("--commit") : QStringLiteral("--no-commit"));
    }
    arguments.append(revision);
    return run(arguments, 120'000);
}

GitCommandResult GitClient::rebase(const QString &revision) const {
    return run({QStringLiteral("rebase"), revision}, 120'000);
}

GitCommandResult GitClient::cherryPick(const QString &hash) const {
    return run({QStringLiteral("cherry-pick"), hash}, 120'000);
}

GitCommandResult GitClient::revert(const QString &hash) const {
    return run({QStringLiteral("revert"), QStringLiteral("--no-edit"), hash}, 120'000);
}

GitCommandResult GitClient::reset(const QString &revision, const GitResetMode mode) const {
    QString modeArgument = QStringLiteral("--mixed");
    switch (mode) {
        case GitResetMode::Soft: modeArgument = QStringLiteral("--soft"); break;
        case GitResetMode::Mixed: modeArgument = QStringLiteral("--mixed"); break;
        case GitResetMode::Hard: modeArgument = QStringLiteral("--hard"); break;
    }
    return run({QStringLiteral("reset"), modeArgument, revision});
}

GitCommandResult GitClient::abortOperation() const {
    const GitRepositoryState state = repositoryState();
    if (state.rebasing) {
        return run({QStringLiteral("rebase"), QStringLiteral("--abort")});
    }
    if (state.cherryPicking) {
        return run({QStringLiteral("cherry-pick"), QStringLiteral("--abort")});
    }
    if (state.reverting) {
        return run({QStringLiteral("revert"), QStringLiteral("--abort")});
    }
    if (state.merging) {
        return run({QStringLiteral("merge"), QStringLiteral("--abort")});
    }

    GitCommandResult result;
    result.processError = tr("There is no operation in progress.");
    return result;
}

GitCommandResult GitClient::continueOperation() const {
    const GitRepositoryState state = repositoryState();
    if (state.rebasing) {
        return run({QStringLiteral("rebase"), QStringLiteral("--continue")}, 120'000);
    }
    if (state.cherryPicking) {
        return run({QStringLiteral("cherry-pick"), QStringLiteral("--continue"),
                    QStringLiteral("--no-edit")}, 120'000);
    }
    if (state.reverting) {
        return run({QStringLiteral("revert"), QStringLiteral("--continue"),
                    QStringLiteral("--no-edit")}, 120'000);
    }
    if (state.merging) {
        return run({QStringLiteral("commit"), QStringLiteral("--no-edit")});
    }

    GitCommandResult result;
    result.processError = tr("There is no operation in progress.");
    return result;
}

GitCommandResult GitClient::createTag(const QString &name, const QString &revision,
                                      const QString &message) const {
    QStringList arguments{QStringLiteral("tag")};
    if (!message.trimmed().isEmpty()) {
        arguments.append(QStringLiteral("--annotate"));
        arguments.append(QStringLiteral("--message=%1").arg(message));
    }
    arguments.append(name);
    if (!revision.isEmpty()) {
        arguments.append(revision);
    }
    return run(arguments);
}

GitCommandResult GitClient::deleteTag(const QString &name) const {
    return run({QStringLiteral("tag"), QStringLiteral("--delete"), name});
}

GitCommandResult GitClient::stashSave(const QString &message, const bool keepStaged,
                                      const bool includeUntracked) const {
    QStringList arguments{QStringLiteral("stash"), QStringLiteral("push")};
    if (keepStaged) {
        arguments.append(QStringLiteral("--keep-index"));
    }
    if (includeUntracked) {
        arguments.append(QStringLiteral("--include-untracked"));
    }
    if (!message.trimmed().isEmpty()) {
        arguments.append(QStringLiteral("--message"));
        arguments.append(message.trimmed());
    }
    return run(arguments);
}

GitCommandResult GitClient::stashApply(const int index, const bool drop) const {
    return run({
        QStringLiteral("stash"),
        drop ? QStringLiteral("pop") : QStringLiteral("apply"),
        QStringLiteral("stash@{%1}").arg(index)
    });
}

GitCommandResult GitClient::stashDrop(const int index) const {
    return run({
        QStringLiteral("stash"),
        QStringLiteral("drop"),
        QStringLiteral("stash@{%1}").arg(index)
    });
}

GitCommandResult GitClient::fetch(const QString &remote, const bool prune,
                                  const bool fetchTags) const {
    QStringList arguments{QStringLiteral("fetch")};
    if (remote.isEmpty()) {
        arguments.append(QStringLiteral("--all"));
    } else {
        arguments.append(remote);
    }
    if (prune) {
        arguments.append(QStringLiteral("--prune"));
    }
    if (fetchTags) {
        arguments.append(QStringLiteral("--tags"));
    }
    return run(arguments, GitNetworkTimeoutMs);
}

GitCommandResult GitClient::pull(const QString &remote, const QString &branch,
                                 const bool rebase) const {
    QStringList arguments{QStringLiteral("pull")};
    arguments.append(rebase ? QStringLiteral("--rebase") : QStringLiteral("--no-rebase"));
    if (!remote.isEmpty()) {
        arguments.append(remote);
        if (!branch.isEmpty()) {
            arguments.append(branch);
        }
    }
    return run(arguments, GitNetworkTimeoutMs);
}

GitCommandResult GitClient::push(const QString &remote, const QStringList &branches,
                                 const bool setUpstream, const bool pushTags,
                                 const bool force) const {
    QStringList arguments{QStringLiteral("push")};
    if (setUpstream) {
        arguments.append(QStringLiteral("--set-upstream"));
    }
    if (pushTags) {
        arguments.append(QStringLiteral("--tags"));
    }
    if (force) {
        arguments.append(QStringLiteral("--force-with-lease"));
    }
    if (!remote.isEmpty()) {
        arguments.append(remote);
    }
    arguments.append(branches);
    return run(arguments, GitNetworkTimeoutMs);
}

GitCommandResult GitClient::addRemote(const QString &name, const QString &url) const {
    return run({QStringLiteral("remote"), QStringLiteral("add"), name, url});
}

GitCommandResult GitClient::removeRemote(const QString &name) const {
    return run({QStringLiteral("remote"), QStringLiteral("remove"), name});
}

GitCommandResult GitClient::runCustom(const QStringList &arguments, const int timeoutMs) const {
    return run(arguments, timeoutMs);
}

QString GitClient::configValue(const QString &key) const {
    const GitCommandResult result = run({QStringLiteral("config"), QStringLiteral("--get"), key});
    return result.succeeded() ? result.outputText().trimmed() : QString();
}

GitCommandResult GitClient::run(const QStringList &arguments, const int timeoutMs) const {
    if (!hasRepository()) {
        GitCommandResult result;
        result.processError = tr("No repository is open.");
        return result;
    }
    return runAt(repositoryRoot_, arguments, timeoutMs);
}

GitCommandResult GitClient::runWithInput(const QStringList &arguments,
                                         const QByteArray &input) const {
    if (!hasRepository()) {
        GitCommandResult result;
        result.processError = tr("No repository is open.");
        return result;
    }
    return runAt(repositoryRoot_, arguments, 30'000, &input);
}

GitCommandResult GitClient::runAt(const QString &directory, const QStringList &arguments,
                                  const int timeoutMs, const QByteArray *input) {
    GitCommandResult result;
    const QString executable = gitExecutable();
    if (executable.isEmpty()) {
        result.processError = tr("Git was not found. Install Git and add it to the system "
                                 "PATH.");
        GitLog::instance()->record(directory, arguments, result);
        return result;
    }

    QProcess process;
    process.setProgram(executable);
    process.setWorkingDirectory(directory);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // Never let git block the UI waiting for an interactive credential prompt.
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    process.setProcessEnvironment(environment);

    QStringList completeArguments{
        QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
        QStringLiteral("-c"), QStringLiteral("color.ui=false"),
        QStringLiteral("-c"), QStringLiteral("advice.detachedHead=false")
    };
    completeArguments.append(arguments);
    process.setArguments(completeArguments);
    process.start();

    if (!process.waitForStarted(GitStartTimeoutMs)) {
        result.processError = process.errorString();
        GitLog::instance()->record(directory, arguments, result);
        return result;
    }

    if (input != nullptr) {
        process.write(*input);
    }
    process.closeWriteChannel();

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        result.processError = tr("Git did not finish the operation in time.");
        GitLog::instance()->record(directory, arguments, result);
        return result;
    }

    result.exitCode = process.exitCode();
    result.output = process.readAllStandardOutput();
    result.errorOutput = process.readAllStandardError();
    GitLog::instance()->record(directory, arguments, result);
    return result;
}
