// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "gitclient.h"

#include "gitparse.h"
#include "gitredact.h"
#include "platform/platformservices.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {
constexpr qint64 MaximumPreviewSize = 2 * 1024 * 1024;
}

bool GitCommandResult::succeeded() const {
    return processError.isEmpty() && exitCode == 0;
}

QString GitCommandResult::outputText() const {
    return QString::fromUtf8(output);
}

// Reaches the command log and the error dialog, so credentials are stripped.
// outputText() stays verbatim: the parsers read it.
QString GitCommandResult::errorText() const {
    if (!processError.isEmpty()) {
        return redactCredentials(processError);
    }

    const QString gitError = QString::fromUtf8(errorOutput).trimmed();
    if (!gitError.isEmpty()) {
        return redactCredentials(gitError);
    }

    const QString gitOutput = outputText().trimmed();
    if (!gitOutput.isEmpty()) {
        return redactCredentials(gitOutput);
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
    return redactCredentials(parts.join(u'\n'));
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

bool GitFileRevision::isAddition() const {
    return status == u'A';
}

bool GitFileRevision::isDeletion() const {
    return status == u'D';
}

bool GitFileRevision::isRename() const {
    return status == u'R' || status == u'C';
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

GitClient::GitClient()
    : runner_(defaultGitRunner()) {
}

GitClient::GitClient(GitProcessRunner *runner)
    : runner_(runner != nullptr ? runner : defaultGitRunner()) {
}

void GitClient::setCancellation(GitCancellationPtr cancellation) {
    cancellation_ = std::move(cancellation);
}

GitCommandResult GitClient::openRepository(const QString &directory) {
    GitCommandResult result = runner_->run(directory, {
        QStringLiteral("rev-parse"),
        QStringLiteral("--show-toplevel")
    }, 30'000, nullptr, cancellation_);

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
    return GitProcess::executable();
}

bool GitClient::configureCredentialHelper(const QString &helper) {
    if (helper.isEmpty()) {
        return false;
    }
    return runAt(QDir::homePath(), {
        QStringLiteral("config"),
        QStringLiteral("--global"),
        QStringLiteral("credential.helper"),
        helper
    }).succeeded();
}

QList<GitFileStatus> GitClient::status(QString *errorMessage) const {
    const GitCommandResult result = run({
        QStringLiteral("status"),
        QStringLiteral("--porcelain=v2"),
        QStringLiteral("-z"),
        QStringLiteral("--untracked-files=all")
    });

    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorText();
        }
        return {};
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return GitParse::parseStatus(result.output);
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

QString GitClient::gitDirectory() const {
    const GitCommandResult result = run({
        QStringLiteral("rev-parse"),
        QStringLiteral("--absolute-git-dir")
    });
    return result.succeeded() ? result.outputText().trimmed() : QString();
}

GitRepositoryState GitClient::repositoryState() const {
    GitRepositoryState state;
    const QString gitDirectoryPath = gitDirectory();
    if (gitDirectoryPath.isEmpty()) {
        return state;
    }

    const QDir gitDirectory(gitDirectoryPath);
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

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return GitParse::parseBranches(result.outputText());
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

    QList<GitRemoteInfo> remotes = GitParse::parseRemotes(result.outputText());

    const GitCommandResult branchResult = run({
        QStringLiteral("for-each-ref"),
        QStringLiteral("--sort=refname"),
        QStringLiteral("--format=%(refname:short)"),
        QStringLiteral("refs/remotes")
    });
    if (branchResult.succeeded()) {
        GitParse::assignRemoteBranches(remotes, branchResult.outputText());
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

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return GitParse::parseTags(result.outputText());
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

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return GitParse::parseStashes(result.outputText());
}

QList<GitSubmoduleInfo> GitClient::submodules() const {
    const GitCommandResult result = run({
        QStringLiteral("submodule"),
        QStringLiteral("status")
    });
    if (!result.succeeded()) {
        return {};
    }

    return GitParse::parseSubmodules(result.outputText());
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
    arguments.append(GitParse::commitFormatArguments());

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
    return GitParse::parseCommits(result.output);
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
    arguments.append(GitParse::commitFormatArguments());

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
    return GitParse::parseCommits(result.output);
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

    QList<GitChangedFile> files = GitParse::parseNameStatus(nameStatus.output, true);

    const GitCommandResult numStat = run({
        QStringLiteral("show"),
        QStringLiteral("--format="),
        QStringLiteral("--numstat"),
        QStringLiteral("-z"),
        QStringLiteral("--find-renames"),
        QStringLiteral("--no-ext-diff"),
        QStringLiteral("-m"),
        QStringLiteral("--first-parent"),
        hash
    });
    if (numStat.succeeded()) {
        GitParse::assignChangeCounts(files, numStat.output);
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

    return GitParse::parseNameStatus(result.output, false);
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

QList<GitTreeEntry> GitClient::treeEntries(const QString &revision, const QString &directory,
                                           QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (revision.isEmpty()) {
        return workingCopyEntries(directory, errorMessage);
    }

    QStringList arguments{
        QStringLiteral("ls-tree"),
        QStringLiteral("-z"),
        QStringLiteral("--long"),
        revision
    };
    if (!directory.isEmpty()) {
        arguments.append(QStringLiteral("--"));
        arguments.append(directory + u'/');
    }

    const GitCommandResult result = run(arguments);
    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorText();
        }
        return {};
    }

    return GitParse::parseTreeEntries(result.output);
}

QList<GitTreeEntry> GitClient::workingCopyEntries(const QString &directory,
                                                  QString *errorMessage) const {
    QStringList arguments{
        QStringLiteral("ls-files"),
        QStringLiteral("-z"),
        QStringLiteral("--cached")
    };
    if (!directory.isEmpty()) {
        arguments.append(QStringLiteral("--"));
        arguments.append(directory + u'/');
    }

    const GitCommandResult result = run(arguments);
    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorText();
        }
        return {};
    }

    // The index lists paths recursively, so the level below "directory" has to
    // be folded out of them.
    const qsizetype prefixLength = directory.isEmpty() ? 0 : directory.size() + 1;
    const QDir root(repositoryRoot_);
    QList<GitTreeEntry> entries;
    QSet<QString> seenDirectories;

    for (const QByteArray &record : GitParse::splitNulRecords(result.output)) {
        const QString path = QString::fromUtf8(record);
        if (path.size() <= prefixLength) {
            continue;
        }

        const QString relative = path.sliced(prefixLength);
        const qsizetype separator = relative.indexOf(u'/');

        GitTreeEntry entry;
        entry.directory = separator >= 0;
        entry.name = entry.directory ? relative.first(separator) : relative;
        entry.path = directory.isEmpty() ? entry.name : directory + u'/' + entry.name;

        if (entry.directory) {
            if (seenDirectories.contains(entry.name)) {
                continue;
            }
            seenDirectories.insert(entry.name);
        } else {
            const QFileInfo info(root.filePath(entry.path));
            if (info.exists()) {
                entry.size = info.size();
            }
        }

        entries.append(entry);
    }

    GitParse::sortTreeEntries(entries);
    return entries;
}

QList<GitTreeEntry> GitClient::allFiles(const QString &revision,
                                        QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (revision.isEmpty()) {
        const GitCommandResult result = run({
            QStringLiteral("ls-files"),
            QStringLiteral("-z"),
            QStringLiteral("--cached")
        });
        if (!result.succeeded()) {
            if (errorMessage != nullptr) {
                *errorMessage = result.errorText();
            }
            return {};
        }

        const QDir root(repositoryRoot_);
        QList<GitTreeEntry> entries;
        for (const QByteArray &record : GitParse::splitNulRecords(result.output)) {
            GitTreeEntry entry;
            entry.path = QString::fromUtf8(record);
            entry.name = entry.path.section(u'/', -1);
            if (const QFileInfo info(root.filePath(entry.path)); info.exists()) {
                entry.size = info.size();
            }
            entries.append(entry);
        }
        return entries;
    }

    const GitCommandResult result = run({
        QStringLiteral("ls-tree"),
        QStringLiteral("-r"),
        QStringLiteral("-z"),
        QStringLiteral("--long"),
        revision
    });
    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorText();
        }
        return {};
    }

    // The parser orders one level for display; a flat listing is more useful
    // grouped by path, the way Git itself reports it.
    QList<GitTreeEntry> entries = GitParse::parseTreeEntries(result.output);
    std::sort(entries.begin(), entries.end(),
              [](const GitTreeEntry &left, const GitTreeEntry &right) {
                  return left.path < right.path;
              });
    return entries;
}

QList<GitFileRevision> GitClient::fileHistory(const QString &path, const QString &revision,
                                              const int maximumCount,
                                              QString *errorMessage) const {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (path.isEmpty() || !hasCommits()) {
        return {};
    }

    QStringList arguments{
        QStringLiteral("log"),
        QStringLiteral("--follow"),
        QStringLiteral("--find-renames"),
        QStringLiteral("--name-status"),
        QStringLiteral("--no-color"),
        QStringLiteral("-z"),
        QStringLiteral("--max-count=%1").arg(qMax(1, maximumCount))
    };
    arguments.append(GitParse::commitFormatArguments());
    arguments.append(revision.isEmpty() ? QStringLiteral("HEAD") : revision);
    arguments.append(QStringLiteral("--"));
    arguments.append(path);

    const GitCommandResult result = run(arguments);
    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorText();
        }
        return {};
    }

    return GitParse::parseFileHistory(result.output);
}

GitCommandResult GitClient::fileContent(const QString &revision, const QString &path) const {
    if (!revision.isEmpty()) {
        return run({
            QStringLiteral("show"),
            QStringLiteral("%1:%2").arg(revision, path)
        });
    }

    GitCommandResult result;
    QFile file(QDir(repositoryRoot_).filePath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        result.processError = tr("“%1” cannot be read.").arg(path);
        return result;
    }

    result.exitCode = 0;
    result.output = file.readAll();
    return result;
}

qint64 GitClient::fileSize(const QString &revision, const QString &path) const {
    if (revision.isEmpty()) {
        const QFileInfo info(QDir(repositoryRoot_).filePath(path));
        return info.exists() ? info.size() : -1;
    }

    const GitCommandResult result = run({
        QStringLiteral("cat-file"),
        QStringLiteral("-s"),
        QStringLiteral("%1:%2").arg(revision, path)
    });
    if (!result.succeeded()) {
        return -1;
    }

    bool parsed = false;
    const qint64 size = result.outputText().trimmed().toLongLong(&parsed);
    return parsed ? size : -1;
}

GitCommandResult GitClient::fileDiff(const QString &fromRevision, const QString &fromPath,
                                     const QString &toRevision, const QString &toPath,
                                     const int contextLines) const {
    // A blob pair keeps the patch correct when the file moved between the two
    // revisions. An empty revision would name the index blob instead, so the
    // committed version is used for it.
    const QString from = fromRevision.isEmpty() ? QStringLiteral("HEAD") : fromRevision;
    const QString to = toRevision.isEmpty() ? QStringLiteral("HEAD") : toRevision;

    return run({
        QStringLiteral("diff"),
        QStringLiteral("--no-ext-diff"),
        QStringLiteral("--no-color"),
        QStringLiteral("--unified=%1").arg(qMax(0, contextLines)),
        QStringLiteral("%1:%2").arg(from, fromPath),
        QStringLiteral("%1:%2").arg(to, toPath)
    });
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
            PlatformServices::instance().nullDevicePath(),
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
                                  const bool fetchTags, const int timeoutMs) const {
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
    return run(arguments, timeoutMs);
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
    return runner_->run(repositoryRoot_, arguments, timeoutMs, nullptr, cancellation_);
}

GitCommandResult GitClient::runWithInput(const QStringList &arguments,
                                         const QByteArray &input) const {
    if (!hasRepository()) {
        GitCommandResult result;
        result.processError = tr("No repository is open.");
        return result;
    }
    return runner_->run(repositoryRoot_, arguments, 30'000, &input, cancellation_);
}

GitCommandResult GitClient::runAt(const QString &directory, const QStringList &arguments,
                                  const int timeoutMs, const QByteArray *input) {
    return defaultGitRunner()->run(directory, arguments, timeoutMs, input, {});
}
