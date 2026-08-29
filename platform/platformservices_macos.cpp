// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "platformservices.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSysInfo>

namespace {

/// The .app bundle SquidyGit is currently running from, or empty when it was
/// launched some other way (e.g. the bare binary during development).
QString currentBundlePath() {
    QDir bundle(QCoreApplication::applicationDirPath());
    if (bundle.dirName() != QStringLiteral("MacOS") || !bundle.cdUp()
        || bundle.dirName() != QStringLiteral("Contents") || !bundle.cdUp()
        || !bundle.dirName().endsWith(QStringLiteral(".app"), Qt::CaseInsensitive)) {
        return {};
    }
    return bundle.absolutePath();
}

/// Mounts a disk image and returns its mount point, or empty on failure.
QString attachDiskImage(const QString &path) {
    QProcess hdiutil;
    hdiutil.start(QStringLiteral("/usr/bin/hdiutil"),
                 {QStringLiteral("attach"), path, QStringLiteral("-nobrowse"),
                  QStringLiteral("-noautoopen"), QStringLiteral("-plist")});
    if (!hdiutil.waitForFinished(60000) || hdiutil.exitStatus() != QProcess::NormalExit
        || hdiutil.exitCode() != 0) {
        return {};
    }
    // Parsing out just the mount point spares us a full plist reader for a
    // single field.
    static const QRegularExpression mountPointTag(
        QStringLiteral("<key>mount-point</key>\\s*<string>([^<]+)</string>"));
    const QRegularExpressionMatch match =
        mountPointTag.match(QString::fromUtf8(hdiutil.readAllStandardOutput()));
    return match.hasMatch() ? match.captured(1) : QString();
}

/// The first .app bundle directly inside `directory`, or empty if there is
/// none — a disk image built by macdeployqt only ever has the one.
QString firstAppBundle(const QString &directory) {
    const QFileInfoList entries = QDir(directory).entryInfoList(
        {QStringLiteral("*.app")}, QDir::Dirs | QDir::NoDotAndDotDot);
    return entries.isEmpty() ? QString() : entries.first().absoluteFilePath();
}

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
        const QString bundlePath = currentBundlePath();
        if (bundlePath.isEmpty()) {
            return PlatformServices::restartApplication();
        }

        QStringList arguments{QStringLiteral("-n"), bundlePath};
        QStringList applicationArguments = QCoreApplication::arguments();
        if (!applicationArguments.isEmpty()) {
            applicationArguments.removeFirst();
        }
        if (!applicationArguments.isEmpty()) {
            arguments.append(QStringLiteral("--args"));
            arguments.append(applicationArguments);
        }
        return QProcess::startDetached(QStringLiteral("/usr/bin/open"), arguments,
                                       QFileInfo(bundlePath).absolutePath());
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
        return UpdatePackageKind::NativeInstaller;
    }

    /// Mounts the downloaded disk image, swaps its app bundle in for the
    /// running one, and relaunches. /Applications is group-writable for
    /// admin accounts — which covers virtually every single-user Mac — so
    /// this normally needs no elevation prompt, unlike Windows.
    [[nodiscard]] InstallerLaunchResult
    launchUpdateInstaller(const QString &path) const override {
        const QString mountPoint = attachDiskImage(path);
        if (mountPoint.isEmpty()) {
            return InstallerLaunchResult::Failed;
        }

        InstallerLaunchResult result = InstallerLaunchResult::Failed;
        const QString newBundle = firstAppBundle(mountPoint);
        const QString currentBundle = currentBundlePath();
        if (!newBundle.isEmpty() && !currentBundle.isEmpty()
            && QDir(currentBundle).removeRecursively()) {
            QProcess copy;
            copy.start(QStringLiteral("/bin/cp"),
                      {QStringLiteral("-R"), newBundle, currentBundle});
            if (copy.waitForFinished(60000) && copy.exitStatus() == QProcess::NormalExit
                && copy.exitCode() == 0 && restartApplication()) {
                result = InstallerLaunchResult::Started;
            }
        }

        QProcess::execute(QStringLiteral("/usr/bin/hdiutil"),
                          {QStringLiteral("detach"), mountPoint, QStringLiteral("-quiet")});

        if (result == InstallerLaunchResult::Started) {
            // The installer for the other platforms is a separate process
            // that closes this one; here that's still us, so we do it.
            QCoreApplication::quit();
        }
        return result;
    }
};

}

PlatformServices &PlatformServices::instance() {
    static MacOSPlatformServices services;
    return services;
}
