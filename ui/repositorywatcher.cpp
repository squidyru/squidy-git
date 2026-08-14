// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "repositorywatcher.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

namespace {

// Coalesce bursts of Git metadata changes.
constexpr int SettleDelayMs = 400;

}

RepositoryWatcher::RepositoryWatcher(QObject *parent)
    : QObject(parent),
      watcher_(new QFileSystemWatcher(this)),
      settleTimer_(new QTimer(this)) {
    settleTimer_->setSingleShot(true);
    settleTimer_->setInterval(SettleDelayMs);

    connect(watcher_, &QFileSystemWatcher::fileChanged, this,
            [this] { handleChange(); });
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this,
            [this] { handleChange(); });
    connect(settleTimer_, &QTimer::timeout, this, [this] {
        // Replacements can drop file watches.
        rearm();
        if (!suspended_) {
            Q_EMIT repositoryChangedOnDisk();
        }
    });
}

void RepositoryWatcher::setRepository(const QString &gitDirectory) {
    gitDirectory_ = gitDirectory;
    wantedPaths_.clear();

    if (!gitDirectory_.isEmpty()) {
        const QDir directory(gitDirectory_);
        wantedPaths_.append(gitDirectory_);
        wantedPaths_.append(directory.filePath(QStringLiteral("refs")));
        wantedPaths_.append(directory.filePath(QStringLiteral("refs/heads")));
        wantedPaths_.append(directory.filePath(QStringLiteral("refs/tags")));
        wantedPaths_.append(directory.filePath(QStringLiteral("refs/remotes")));
        wantedPaths_.append(directory.filePath(QStringLiteral("HEAD")));
        wantedPaths_.append(directory.filePath(QStringLiteral("index")));
        wantedPaths_.append(directory.filePath(QStringLiteral("packed-refs")));
    }

    const QStringList watched = watcher_->files() + watcher_->directories();
    if (!watched.isEmpty()) {
        watcher_->removePaths(watched);
    }
    rearm();
}

void RepositoryWatcher::setSuspended(const bool suspended) {
    if (suspended_ == suspended) {
        return;
    }
    suspended_ = suspended;
    if (suspended_) {
        return;
    }

    settleTimer_->stop();
    rearm();
}

void RepositoryWatcher::handleChange() {
    if (gitDirectory_.isEmpty()) {
        return;
    }
    settleTimer_->start();
}

void RepositoryWatcher::rearm() {
    const QStringList watched = watcher_->files() + watcher_->directories();
    QStringList missing;
    for (const QString &path : std::as_const(wantedPaths_)) {
        if (!watched.contains(path) && QFileInfo::exists(path)) {
            missing.append(path);
        }
    }
    if (!missing.isEmpty()) {
        watcher_->addPaths(missing);
    }
}
