// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "platformservices.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QSysInfo>

namespace {

class LinuxPlatformServices final : public PlatformServices {
public:
    [[nodiscard]] PlatformKind kind() const override {
        return PlatformKind::Linux;
    }

    [[nodiscard]] QString gitExecutable() const override {
        static const QString executable = firstExecutable({QStringLiteral("git")});
        return executable;
    }

    [[nodiscard]] bool openTerminal(const QString &directory) const override {
        if (directory.isEmpty()) {
            return false;
        }

        struct TerminalCommand {
            QString program;
            QStringList arguments;
        };

        const QList<TerminalCommand> candidates{
            {QStringLiteral("ptyxis"),
             {QStringLiteral("--new-window"), QStringLiteral("--working-directory"),
              directory}},
            {QStringLiteral("gnome-terminal"),
             {QStringLiteral("--working-directory=%1").arg(directory)}},
            {QStringLiteral("konsole"),
             {QStringLiteral("--workdir"), directory}},
            {QStringLiteral("xfce4-terminal"),
             {QStringLiteral("--working-directory"), directory}},
            {QStringLiteral("mate-terminal"),
             {QStringLiteral("--working-directory=%1").arg(directory)}},
            {QStringLiteral("tilix"),
             {QStringLiteral("--working-directory=%1").arg(directory)}},
            {QStringLiteral("alacritty"),
             {QStringLiteral("--working-directory"), directory}},
            {QStringLiteral("kitty"),
             {QStringLiteral("--directory"), directory}},
            {QStringLiteral("xterm"), {}},
            {QStringLiteral("x-terminal-emulator"), {}}
        };

        for (const TerminalCommand &candidate : candidates) {
            if (QProcess::startDetached(candidate.program, candidate.arguments, directory)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool revealInFileManager(const QString &path) const override {
        if (path.isEmpty()) {
            return false;
        }
        const QFileInfo info(path);
        return openPath(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    }

    void configureApplication() const override {
        QGuiApplication::setDesktopFileName(QStringLiteral("squidygit"));
    }

    [[nodiscard]] bool restartApplication() const override {
        const QString appImage = qEnvironmentVariable("APPIMAGE");
        if (appImage.isEmpty()) {
            return PlatformServices::restartApplication();
        }

        // applicationFilePath() points inside the temporary AppImage mount.
        QStringList arguments = QCoreApplication::arguments();
        if (!arguments.isEmpty()) {
            arguments.removeFirst();
        }
        return QProcess::startDetached(appImage, arguments, QDir::currentPath());
    }

    [[nodiscard]] QString updateAssetSuffix() const override {
        const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
        if (architecture != QStringLiteral("x86_64")
            && architecture != QStringLiteral("amd64")) {
            return {};
        }
        if (!qEnvironmentVariableIsEmpty("APPIMAGE")
            || !QFileInfo::exists(QStringLiteral("/etc/debian_version"))) {
            return QStringLiteral("-linux-x86_64.AppImage");
        }
        return QStringLiteral(".deb");
    }

    [[nodiscard]] UpdatePackageKind
    updatePackageKind(const QString &path) const override {
        if (path.endsWith(QStringLiteral(".deb"), Qt::CaseInsensitive)) {
            return UpdatePackageKind::SystemInstaller;
        }
        if (path.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive)) {
            return UpdatePackageKind::ManualExecutable;
        }
        if (path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
            return UpdatePackageKind::ManualArchive;
        }
        return UpdatePackageKind::OpenWithSystem;
    }

    void prepareDownloadedPackage(const QString &path) const override {
        if (!path.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive)) {
            return;
        }
        QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ExeOwner
                                    | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    }

    [[nodiscard]] std::optional<PlatformCommand>
    systemPackageInstaller(const QString &path) const override {
        const QString pkexec = firstExecutable({QStringLiteral("pkexec")});
        const QString aptGet = firstExecutable({QStringLiteral("apt-get")});
        if (pkexec.isEmpty() || aptGet.isEmpty()) {
            return std::nullopt;
        }

        PlatformCommand command;
        command.program = pkexec;
        command.arguments = {aptGet, QStringLiteral("install"), QStringLiteral("--yes"), path};
        command.environment.insert(QStringLiteral("DEBIAN_FRONTEND"),
                                   QStringLiteral("noninteractive"));
        return command;
    }

    [[nodiscard]] QString installedPackageVersion() const override {
        const QString dpkgQuery = firstExecutable({QStringLiteral("dpkg-query")});
        if (dpkgQuery.isEmpty()) {
            return {};
        }

        QProcess query;
        query.start(dpkgQuery,
                    {QStringLiteral("-W"), QStringLiteral("-f=${Version}"),
                     QStringLiteral("squidygit")});
        if (!query.waitForFinished(5000) || query.exitStatus() != QProcess::NormalExit
            || query.exitCode() != 0) {
            return {};
        }
        return QString::fromLocal8Bit(query.readAllStandardOutput()).trimmed();
    }

    [[nodiscard]] bool isSystemInstallerCancellation(const int exitCode) const override {
        return exitCode == 126 || exitCode == 127;
    }
};

}

PlatformServices &PlatformServices::instance() {
    static LinuxPlatformServices services;
    return services;
}
