// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QObject>

class QNetworkAccessManager;
class QProcess;
class QWidget;

class UpdateChecker final : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QWidget *dialogParent);

    void checkForUpdates(bool userInitiated);

private:
    struct ReleaseAsset;

    void requestChecksums(const ReleaseAsset &asset, const QString &checksumsUrl);
    void downloadAsset(const ReleaseAsset &asset);
    void openDownloadedPackage(const QString &path);
#if defined(Q_OS_LINUX)
    void installDebPackage(const QString &path);
#elif defined(Q_OS_WIN)
    void installWindowsPackage(const QString &path);
#endif
    void finishWithError(const QString &message, bool showMessage);

    QWidget *dialogParent_ = nullptr;
    QNetworkAccessManager *network_ = nullptr;
    QProcess *packageInstaller_ = nullptr;
    bool busy_ = false;
};
