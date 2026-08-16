// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "platformservices.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

PlatformServices::~PlatformServices() = default;

void PlatformServices::configureApplication() const {
}

QString PlatformServices::preferredCredentialHelper() const {
    return {};
}

bool PlatformServices::openPath(const QString &path) const {
    return !path.isEmpty() && QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString PlatformServices::downloadDirectory() const {
    const QString downloads =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return downloads.isEmpty() ? QDir::tempPath() : downloads;
}

bool PlatformServices::restartApplication() const {
    QStringList arguments = QCoreApplication::arguments();
    if (!arguments.isEmpty()) {
        arguments.removeFirst();
    }
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments,
                                   QDir::currentPath());
}

void PlatformServices::prepareDownloadedPackage(const QString &) const {
}

InstallerLaunchResult PlatformServices::launchUpdateInstaller(const QString &) const {
    return InstallerLaunchResult::Failed;
}

std::optional<PlatformCommand>
PlatformServices::systemPackageInstaller(const QString &) const {
    return std::nullopt;
}

QString PlatformServices::installedPackageVersion() const {
    return {};
}

bool PlatformServices::isSystemInstallerCancellation(const int) const {
    return false;
}

QString PlatformServices::firstExecutable(const QStringList &candidates) {
    for (const QString &candidate : candidates) {
        if (candidate.isEmpty()) {
            continue;
        }

        const QFileInfo info(candidate);
        if (info.isAbsolute()) {
            if (info.isFile() && info.isExecutable()) {
                return info.absoluteFilePath();
            }
            continue;
        }

        const QString executable = QStandardPaths::findExecutable(candidate);
        if (!executable.isEmpty()) {
            return executable;
        }
    }
    return {};
}
