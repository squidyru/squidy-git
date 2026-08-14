// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "repositorysnapshot.h"

RepositorySnapshot collectRepositorySnapshot(GitClient git, GitHistoryOptions options,
                                             const quint64 generation) {
    RepositorySnapshot snapshot;
    snapshot.generation = generation;
    snapshot.historyOptions = options;

    if (!git.hasRepository()) {
        return snapshot;
    }

    snapshot.headHash = git.headHash();
    snapshot.currentBranch = git.currentBranch();
    snapshot.state = git.repositoryState();
    snapshot.userName = git.userName();
    snapshot.userEmail = git.userEmail();

    // Reuse the branch result for the header counters and sidebar.
    snapshot.branches = git.branches();
    for (const GitBranchInfo &branch : snapshot.branches) {
        if (branch.current) {
            snapshot.ahead = branch.ahead;
            snapshot.behind = branch.behind;
            break;
        }
    }

    snapshot.tags = git.tags();
    snapshot.remotes = git.remotes();
    snapshot.stashes = git.stashes();
    snapshot.submodules = git.submodules();
    snapshot.files = git.status(&snapshot.statusError);
    snapshot.commits = git.history(options, &snapshot.historyError);

    return snapshot;
}
