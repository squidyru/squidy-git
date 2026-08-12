// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

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
};

enum class GitSearchMode {
    Message,
    Author,
    FileContents,
    FilePath,
    Hash
};

/// Broadcasts every git invocation for display in the command log.
class GitLog final : public QObject {
    Q_OBJECT

public:
    static GitLog *instance();
    void record(const QString &workingDirectory, const QStringList &arguments,
                const GitCommandResult &result);

Q_SIGNALS:
    void commandRecorded(const QString &workingDirectory, const QString &command,
                         const QString &output, bool succeeded);

private:
    explicit GitLog(QObject *parent = nullptr);
};

class GitClient {
public:
    [[nodiscard]] GitCommandResult openRepository(const QString &directory);
    [[nodiscard]] bool hasRepository() const;
    [[nodiscard]] const QString &repositoryRoot() const;
    [[nodiscard]] QString repositoryName() const;

    [[nodiscard]] static bool isRepository(const QString &directory);
    [[nodiscard]] static GitCommandResult initRepository(const QString &directory, bool bare);
    [[nodiscard]] static QString gitExecutable();

    // --- Inspection -------------------------------------------------------
    [[nodiscard]] QList<GitFileStatus> status(QString *errorMessage = nullptr) const;
    [[nodiscard]] QString currentBranch(QString *errorMessage = nullptr) const;
    [[nodiscard]] QString headHash() const;
    [[nodiscard]] bool hasCommits() const;
    [[nodiscard]] GitRepositoryState repositoryState() const;
    [[nodiscard]] QList<GitBranchInfo> branches(QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<GitRemoteInfo> remotes(QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<GitTagInfo> tags(QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<GitStashInfo> stashes(QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<GitSubmoduleInfo> submodules() const;
    [[nodiscard]] QList<GitCommitInfo> history(const GitHistoryOptions &options,
                                               QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<GitCommitInfo> search(GitSearchMode mode, const QString &query,
                                              int maximumCount,
                                              QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<GitChangedFile> commitFiles(const QString &hash) const;
    [[nodiscard]] QList<GitChangedFile> stashFiles(int index) const;
    [[nodiscard]] QString upstreamOf(const QString &branch) const;
    [[nodiscard]] QString userName() const;
    [[nodiscard]] QString userEmail() const;
    [[nodiscard]] QString lastCommitMessage() const;

    // --- Diffs ------------------------------------------------------------
    [[nodiscard]] GitCommandResult diff(const QString &path, bool staged, bool untracked,
                                        int contextLines = 3) const;
    [[nodiscard]] GitCommandResult commitDiff(const QString &hash, const QString &path) const;
    [[nodiscard]] GitCommandResult stashDiff(int index, const QString &path) const;
    [[nodiscard]] GitCommandResult showCommit(const QString &hash) const;

    // --- Index and working tree -------------------------------------------
    [[nodiscard]] GitCommandResult stage(const QStringList &paths) const;
    [[nodiscard]] GitCommandResult unstage(const QStringList &paths) const;
    [[nodiscard]] GitCommandResult stageAll() const;
    [[nodiscard]] GitCommandResult unstageAll() const;
    [[nodiscard]] GitCommandResult discard(const QStringList &paths, bool untracked) const;
    [[nodiscard]] GitCommandResult applyPatch(const QByteArray &patch, bool cached,
                                              bool reverse) const;
    [[nodiscard]] GitCommandResult resolveWith(const QStringList &paths, bool useMine) const;
    [[nodiscard]] GitCommandResult ignore(const QStringList &patterns) const;
    [[nodiscard]] GitCommandResult commit(const QString &message, bool amend,
                                          const QString &author = {}) const;

    // --- Branches, tags, history rewriting --------------------------------
    [[nodiscard]] GitCommandResult checkoutBranch(const QString &name) const;
    [[nodiscard]] GitCommandResult checkoutRevision(const QString &revision) const;
    [[nodiscard]] GitCommandResult checkoutRemoteBranch(const QString &remoteBranch,
                                                        const QString &localName) const;
    [[nodiscard]] GitCommandResult createBranch(const QString &name, const QString &startPoint,
                                                bool checkout) const;
    [[nodiscard]] GitCommandResult deleteBranch(const QString &name, bool force) const;
    [[nodiscard]] GitCommandResult deleteRemoteBranch(const QString &remote,
                                                      const QString &branch) const;
    [[nodiscard]] GitCommandResult renameBranch(const QString &oldName,
                                                const QString &newName) const;
    [[nodiscard]] GitCommandResult merge(const QString &revision, bool noFastForward,
                                         bool squash, bool commitResult) const;
    [[nodiscard]] GitCommandResult rebase(const QString &revision) const;
    [[nodiscard]] GitCommandResult cherryPick(const QString &hash) const;
    [[nodiscard]] GitCommandResult revert(const QString &hash) const;
    [[nodiscard]] GitCommandResult reset(const QString &revision, GitResetMode mode) const;
    [[nodiscard]] GitCommandResult abortOperation() const;
    [[nodiscard]] GitCommandResult continueOperation() const;
    [[nodiscard]] GitCommandResult createTag(const QString &name, const QString &revision,
                                             const QString &message) const;
    [[nodiscard]] GitCommandResult deleteTag(const QString &name) const;

    // --- Stash ------------------------------------------------------------
    [[nodiscard]] GitCommandResult stashSave(const QString &message, bool keepStaged,
                                             bool includeUntracked) const;
    [[nodiscard]] GitCommandResult stashApply(int index, bool drop) const;
    [[nodiscard]] GitCommandResult stashDrop(int index) const;

    // --- Remote operations ------------------------------------------------
    [[nodiscard]] GitCommandResult fetch(const QString &remote, bool prune, bool fetchTags) const;
    [[nodiscard]] GitCommandResult pull(const QString &remote, const QString &branch,
                                        bool rebase) const;
    [[nodiscard]] GitCommandResult push(const QString &remote, const QStringList &branches,
                                        bool setUpstream, bool pushTags, bool force) const;
    [[nodiscard]] GitCommandResult addRemote(const QString &name, const QString &url) const;
    [[nodiscard]] GitCommandResult removeRemote(const QString &name) const;

    [[nodiscard]] GitCommandResult runCustom(const QStringList &arguments,
                                             int timeoutMs = 60'000) const;

private:
    [[nodiscard]] GitCommandResult run(const QStringList &arguments,
                                       int timeoutMs = 30'000) const;
    [[nodiscard]] GitCommandResult runWithInput(const QStringList &arguments,
                                                const QByteArray &input) const;
    [[nodiscard]] static GitCommandResult runAt(const QString &directory,
                                                const QStringList &arguments,
                                                int timeoutMs = 30'000,
                                                const QByteArray *input = nullptr);
    [[nodiscard]] QString configValue(const QString &key) const;

    QString repositoryRoot_;
};
