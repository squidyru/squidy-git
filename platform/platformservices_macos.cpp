// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "platformservices.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSysInfo>

namespace {

class MacOSPlatformServices final : public PlatformServices {
public:
    [[nodiscard]] PlatformKind kind() const override {
        return PlatformKind::MacOS;
    }

    [[nodiscard]] QString gitExecutable() const override {
        static const QString executable = firstExecutable({
            QStringLiteral("git"), QStringLiteral("/usr/bin/git"),
            QStringLiteral("/opt/homebrew/bin/git"),
            QStringLiteral("/usr/local/bin/git")
        });
        return executable;
    }

    /// Bundled with Git on macOS, so it is always available.
    [[nodiscard]] QString preferredCredentialHelper() const override {
        return QStringLiteral("osxkeychain");
    }

    [[nodiscard]] bool openTerminal(const QString &directory) const override {
        if (directory.isEmpty()) {
            return false;
        }
        return QProcess::startDetached(
            QStringLiteral("/usr/bin/open"),
            {QStringLiteral("-a"), QStringLiteral("Terminal"), directory}, directory);
    }

    [[nodiscard]] bool revealInFileManager(const QString &path) const override {
        if (path.isEmpty()) {
            return false;
        }
        const QFileInfo info(path);
        if (info.isDir()) {
            return openPath(info.absoluteFilePath());
        }
        if (!info.exists()) {
            return openPath(info.absolutePath());
        }
        return QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                       {QStringLiteral("-R"), info.absoluteFilePath()},
                                       info.absolutePath());
    }

    [[nodiscard]] bool restartApplication() const override {
        QDir bundle(QCoreApplication::applicationDirPath());
        if (bundle.dirName() != QStringLiteral("MacOS") || !bundle.cdUp()
            || bundle.dirName() != QStringLiteral("Contents") || !bundle.cdUp()
            || !bundle.dirName().endsWith(QStringLiteral(".app"), Qt::CaseInsensitive)) {
            return PlatformServices::restartApplication();
        }

        QStringList arguments{QStringLiteral("-n"), bundle.absolutePath()};
        QStringList applicationArguments = QCoreApplication::arguments();
        if (!applicationArguments.isEmpty()) {
            applicationArguments.removeFirst();
        }
        if (!applicationArguments.isEmpty()) {
            arguments.append(QStringLiteral("--args"));
            arguments.append(applicationArguments);
        }
        return QProcess::startDetached(QStringLiteral("/usr/bin/open"), arguments,
                                       QFileInfo(bundle.absolutePath()).absolutePath());
    }

    [[nodiscard]] QString updateAssetSuffix() const override {
        const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
        if (architecture == QStringLiteral("arm64")
            || architecture == QStringLiteral("aarch64")) {
            return QStringLiteral("-macos-arm64.dmg");
        }
        if (architecture == QStringLiteral("x86_64")
            || architecture == QStringLiteral("amd64")) {
            return QStringLiteral("-macos-x86_64.dmg");
        }
        return {};
    }

    [[nodiscard]] UpdatePackageKind
    updatePackageKind(const QString &path) const override {
        if (path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
            return UpdatePackageKind::ManualArchive;
        }
        return UpdatePackageKind::OpenWithSystem;
    }
};

}

PlatformServices &PlatformServices::instance() {
    static MacOSPlatformServices services;
    return services;
}
