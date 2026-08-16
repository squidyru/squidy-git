// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "platformservices.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>

namespace {

InstallerLaunchResult shellExecute(const QString &program, const QString &arguments,
                                   const QString &directory, const bool elevated) {
    const QString nativeDirectory = QDir::toNativeSeparators(directory);
    SHELLEXECUTEINFOW request{};
    request.cbSize = sizeof(request);
    request.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    request.lpVerb = elevated ? L"runas" : L"open";
    request.lpFile = reinterpret_cast<const wchar_t *>(program.utf16());
    request.lpParameters = arguments.isEmpty()
                               ? nullptr
                               : reinterpret_cast<const wchar_t *>(arguments.utf16());
    request.lpDirectory = nativeDirectory.isEmpty()
                              ? nullptr
                              : reinterpret_cast<const wchar_t *>(nativeDirectory.utf16());
    request.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&request)) {
        return InstallerLaunchResult::Started;
    }
    return GetLastError() == ERROR_CANCELLED ? InstallerLaunchResult::Cancelled
                                             : InstallerLaunchResult::Failed;
}

class WindowsPlatformServices final : public PlatformServices {
public:
    [[nodiscard]] PlatformKind kind() const override {
        return PlatformKind::Windows;
    }

    [[nodiscard]] QString gitExecutable() const override {
        static const QString executable = [] {
            QStringList candidates{QStringLiteral("git.exe")};
            const auto appendGit = [&candidates](const QString &root) {
                if (!root.isEmpty()) {
                    candidates.append(QDir(root).filePath(QStringLiteral("Git/cmd/git.exe")));
                }
            };
            appendGit(qEnvironmentVariable("ProgramFiles"));
            appendGit(qEnvironmentVariable("ProgramFiles(x86)"));
            if (const QString local = qEnvironmentVariable("LOCALAPPDATA"); !local.isEmpty()) {
                candidates.append(
                    QDir(local).filePath(QStringLiteral("Programs/Git/cmd/git.exe")));
            }
            return firstExecutable(candidates);
        }();
        return executable;
    }

    /// Git for Windows ships the credential manager, and falls back to the
    /// built-in store on installations without it.
    [[nodiscard]] QString preferredCredentialHelper() const override {
        return QStringLiteral("manager");
    }

    [[nodiscard]] bool openTerminal(const QString &directory) const override {
        if (directory.isEmpty()) {
            return false;
        }
        const QString nativeDirectory = QDir::toNativeSeparators(directory);
        const QString powerShell = QStringLiteral("powershell.exe");
        const QString arguments =
            QStringLiteral("-d \"%1\" %2 -NoExit").arg(nativeDirectory, powerShell);
        if (shellExecute(QStringLiteral("wt.exe"), arguments, nativeDirectory, false)
            == InstallerLaunchResult::Started) {
            return true;
        }
        return shellExecute(powerShell, QStringLiteral("-NoExit"), nativeDirectory, false)
               == InstallerLaunchResult::Started;
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
        const QString nativePath = QDir::toNativeSeparators(info.absoluteFilePath());
        const QString arguments = QStringLiteral("/select,\"%1\"").arg(nativePath);
        return shellExecute(QStringLiteral("explorer.exe"), arguments, info.absolutePath(), false)
               == InstallerLaunchResult::Started;
    }

    [[nodiscard]] QString updateAssetSuffix() const override {
        const bool portable = QFileInfo::exists(
            QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("portable.marker")));
        return portable ? QStringLiteral("-windows-x64-portable.zip")
                        : QStringLiteral("-windows-x64.exe");
    }

    [[nodiscard]] UpdatePackageKind
    updatePackageKind(const QString &path) const override {
        if (path.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            return UpdatePackageKind::NativeInstaller;
        }
        if (path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
            return UpdatePackageKind::ManualArchive;
        }
        return UpdatePackageKind::OpenWithSystem;
    }

    [[nodiscard]] InstallerLaunchResult
    launchUpdateInstaller(const QString &path) const override {
        return shellExecute(QDir::toNativeSeparators(path),
                            QStringLiteral("/SILENT /CLOSEAPPLICATIONS"),
                            QFileInfo(path).absolutePath(), true);
    }
};

}

PlatformServices &PlatformServices::instance() {
    static WindowsPlatformServices services;
    return services;
}
