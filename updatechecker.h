// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QObject>

class QNetworkAccessManager;
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
    void openDownloadedPackage(const QString &path) const;
    void finishWithError(const QString &message, bool showMessage);

    QWidget *dialogParent_ = nullptr;
    QNetworkAccessManager *network_ = nullptr;
    bool busy_ = false;
};
