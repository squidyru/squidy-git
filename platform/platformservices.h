// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

enum class PlatformKind {
    Linux,
    Windows,
    MacOS,
    Other
};

enum class UpdatePackageKind {
    SystemInstaller,
    NativeInstaller,
    ManualExecutable,
    ManualArchive,
    OpenWithSystem
};

enum class InstallerLaunchResult {
    Started,
    Cancelled,
    Failed
};

struct PlatformCommand {
    QString program;
    QStringList arguments;
    QHash<QString, QString> environment;
};

class PlatformServices {
public:
    virtual ~PlatformServices();

    PlatformServices(const PlatformServices &) = delete;
    PlatformServices &operator=(const PlatformServices &) = delete;

    [[nodiscard]] static PlatformServices &instance();

    [[nodiscard]] virtual PlatformKind kind() const = 0;
    [[nodiscard]] virtual QString gitExecutable() const = 0;
    [[nodiscard]] virtual bool openTerminal(const QString &directory) const = 0;
    [[nodiscard]] virtual bool revealInFileManager(const QString &path) const = 0;

    /// The credential helper this system can store secrets with, or empty when
    /// none is installed. The application never writes one to disk itself.
    [[nodiscard]] virtual QString preferredCredentialHelper() const;

    virtual void configureApplication() const;
    [[nodiscard]] bool openPath(const QString &path) const;
    [[nodiscard]] QString downloadDirectory() const;
    [[nodiscard]] virtual bool restartApplication() const;

    [[nodiscard]] virtual QString updateAssetSuffix() const = 0;
    [[nodiscard]] virtual UpdatePackageKind updatePackageKind(const QString &path) const = 0;
    virtual void prepareDownloadedPackage(const QString &path) const;

    [[nodiscard]] virtual InstallerLaunchResult
    launchUpdateInstaller(const QString &path) const;
    [[nodiscard]] virtual std::optional<PlatformCommand>
    systemPackageInstaller(const QString &path) const;
    [[nodiscard]] virtual QString installedPackageVersion() const;
    [[nodiscard]] virtual bool isSystemInstallerCancellation(int exitCode) const;

protected:
    PlatformServices() = default;

    [[nodiscard]] static QString firstExecutable(const QStringList &candidates);
};
