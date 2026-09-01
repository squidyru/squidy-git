// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "platform/platformservices.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <QTest>

class TestPlatformServices final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void findsGitFromThePlatformEnvironment();
    void providesADownloadDirectory();
    void providesANullDevice();
    void rejectsEmptyPaths();
    void classifiesUpdatePackagesForTheCurrentPlatform();
    void selectsAReleaseAssetForSupportedArchitectures();
};

void TestPlatformServices::findsGitFromThePlatformEnvironment() {
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (fromPath.isEmpty()) {
        QSKIP("git is not available in PATH");
    }

    const QString executable = PlatformServices::instance().gitExecutable();
    QVERIFY(!executable.isEmpty());
    QVERIFY(QFileInfo::exists(executable));
}

void TestPlatformServices::providesADownloadDirectory() {
    QVERIFY(!PlatformServices::instance().downloadDirectory().isEmpty());
}

void TestPlatformServices::providesANullDevice() {
}

void TestPlatformServices::rejectsEmptyPaths() {
    const PlatformServices &services = PlatformServices::instance();
    QVERIFY(!services.openPath({}));
    QVERIFY(!services.openTerminal({}));
    QVERIFY(!services.revealInFileManager({}));
}

void TestPlatformServices::classifiesUpdatePackagesForTheCurrentPlatform() {
    const PlatformServices &services = PlatformServices::instance();
    QCOMPARE(services.updatePackageKind(QStringLiteral("release.zip")),
             UpdatePackageKind::ManualArchive);

    switch (services.kind()) {
        case PlatformKind::Linux:
            QCOMPARE(services.updatePackageKind(QStringLiteral("release.deb")),
                     UpdatePackageKind::SystemInstaller);
            QCOMPARE(services.updatePackageKind(QStringLiteral("release.AppImage")),
                     UpdatePackageKind::ManualExecutable);
            QVERIFY(services.isSystemInstallerCancellation(126));
            break;
        case PlatformKind::Windows:
            QCOMPARE(services.updatePackageKind(QStringLiteral("release.exe")),
                     UpdatePackageKind::NativeInstaller);
            QVERIFY(!services.isSystemInstallerCancellation(126));
            break;
        case PlatformKind::MacOS:
            QCOMPARE(services.updatePackageKind(QStringLiteral("release.dmg")),
                     UpdatePackageKind::NativeInstaller);
            QVERIFY(!services.isSystemInstallerCancellation(126));
            break;
        case PlatformKind::Other:
            QCOMPARE(services.updatePackageKind(QStringLiteral("release.dmg")),
                     UpdatePackageKind::OpenWithSystem);
            QVERIFY(!services.isSystemInstallerCancellation(126));
            break;
    }
}

void TestPlatformServices::selectsAReleaseAssetForSupportedArchitectures() {
    const PlatformServices &services = PlatformServices::instance();
    const QString suffix = services.updateAssetSuffix();

    switch (services.kind()) {
        case PlatformKind::Linux:
            QVERIFY(suffix.isEmpty() || suffix == QStringLiteral(".deb")
                    || suffix.endsWith(QStringLiteral(".AppImage")));
            break;
        case PlatformKind::Windows:
            QVERIFY(suffix.endsWith(QStringLiteral(".exe"))
                    || suffix.endsWith(QStringLiteral(".zip")));
            break;
        case PlatformKind::MacOS:
            QVERIFY(suffix.isEmpty() || suffix.endsWith(QStringLiteral(".dmg")));
            break;
        case PlatformKind::Other:
            QVERIFY(suffix.isEmpty());
            break;
    }
}

QTEST_APPLESS_MAIN(TestPlatformServices)

#include "tst_platformservices.moc"
