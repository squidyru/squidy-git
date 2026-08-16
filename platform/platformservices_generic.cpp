// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "platformservices.h"

#include <QFileInfo>

namespace {

class GenericPlatformServices final : public PlatformServices {
public:
    [[nodiscard]] PlatformKind kind() const override {
        return PlatformKind::Other;
    }

    [[nodiscard]] QString gitExecutable() const override {
        static const QString executable = firstExecutable({QStringLiteral("git")});
        return executable;
    }

    [[nodiscard]] bool openTerminal(const QString &) const override {
        return false;
    }

    [[nodiscard]] bool revealInFileManager(const QString &path) const override {
        if (path.isEmpty()) {
            return false;
        }
        const QFileInfo info(path);
        return openPath(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    }

    [[nodiscard]] QString updateAssetSuffix() const override {
        return {};
    }

    [[nodiscard]] UpdatePackageKind
    updatePackageKind(const QString &path) const override {
        return path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)
                   ? UpdatePackageKind::ManualArchive
                   : UpdatePackageKind::OpenWithSystem;
    }
};

}

PlatformServices &PlatformServices::instance() {
    static GenericPlatformServices services;
    return services;
}
