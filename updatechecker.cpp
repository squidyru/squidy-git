// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "updatechecker.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <QVersionNumber>

#include <optional>

namespace {
constexpr auto ReleasesApiUrl =
    "https://api.github.com/repos/squidyru/squidy-git/releases?per_page=20";
constexpr auto ReleasesPageUrl = "https://github.com/squidyru/squidy-git/releases";
constexpr qint64 AutomaticCheckIntervalSeconds = 24 * 60 * 60;

QNetworkRequest githubRequest(const QUrl &url) {
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader(
        "User-Agent",
        QStringLiteral("SquidyGit/%1").arg(QApplication::applicationVersion()).toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

bool isSafeDownloadUrl(const QUrl &url) {
    return url.isValid() && url.scheme() == QStringLiteral("https")
           && (url.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) == 0
               || url.host().endsWith(QStringLiteral(".github.com"),
                                      Qt::CaseInsensitive)
               || url.host().endsWith(QStringLiteral(".githubusercontent.com"),
                                      Qt::CaseInsensitive));
}

QString normalizedDigest(QString digest) {
    if (digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
        digest.remove(0, 7);
    }
    digest = digest.trimmed().toLower();
    static const QRegularExpression sha256Pattern(QStringLiteral("^[0-9a-f]{64}$"));
    return sha256Pattern.match(digest).hasMatch() ? digest : QString();
}

struct SemanticVersion {
    QVersionNumber core;
    QStringList prerelease;
    QString display;

    [[nodiscard]] bool isPrerelease() const { return !prerelease.isEmpty(); }
};

std::optional<SemanticVersion> parseVersion(QString version) {
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        version.remove(0, 1);
    }
    const QString display = version;
    const qsizetype prerelease = version.indexOf(QLatin1Char('-'));
    QString prereleaseText;
    if (prerelease >= 0) {
        prereleaseText = version.mid(prerelease + 1);
        version.truncate(prerelease);
    }

    qsizetype suffixIndex = 0;
    const QVersionNumber parsed = QVersionNumber::fromString(version, &suffixIndex);
    if (parsed.isNull() || suffixIndex != version.size()) {
        return std::nullopt;
    }

    QStringList identifiers;
    if (!prereleaseText.isEmpty()) {
        static const QRegularExpression identifierPattern(
            QStringLiteral("^[0-9A-Za-z-]+$"));
        identifiers = prereleaseText.split(QLatin1Char('.'));
        for (const QString &identifier : identifiers) {
            if (!identifierPattern.match(identifier).hasMatch()) {
                return std::nullopt;
            }
        }
    } else if (prerelease >= 0) {
        return std::nullopt;
    }

    return SemanticVersion{parsed, identifiers, display};
}

int compareNumericIdentifiers(QString left, QString right) {
    while (left.size() > 1 && left.startsWith(QLatin1Char('0'))) {
        left.remove(0, 1);
    }
    while (right.size() > 1 && right.startsWith(QLatin1Char('0'))) {
        right.remove(0, 1);
    }
    if (left.size() != right.size()) {
        return left.size() < right.size() ? -1 : 1;
    }
    return QString::compare(left, right, Qt::CaseSensitive);
}

int compareVersions(const SemanticVersion &left, const SemanticVersion &right) {
    const int coreComparison = QVersionNumber::compare(left.core, right.core);
    if (coreComparison != 0) {
        return coreComparison;
    }
    if (!left.isPrerelease() && !right.isPrerelease()) {
        return 0;
    }
    if (!left.isPrerelease()) {
        return 1;
    }
    if (!right.isPrerelease()) {
        return -1;
    }

    static const QRegularExpression numericPattern(QStringLiteral("^[0-9]+$"));
    const qsizetype count = qMin(left.prerelease.size(), right.prerelease.size());
    for (qsizetype index = 0; index < count; ++index) {
        const QString &leftIdentifier = left.prerelease.at(index);
        const QString &rightIdentifier = right.prerelease.at(index);
        if (leftIdentifier == rightIdentifier) {
            continue;
        }

        const bool leftIsNumeric = numericPattern.match(leftIdentifier).hasMatch();
        const bool rightIsNumeric = numericPattern.match(rightIdentifier).hasMatch();
        if (leftIsNumeric != rightIsNumeric) {
            return leftIsNumeric ? -1 : 1;
        }
        const int comparison = leftIsNumeric
                                   ? compareNumericIdentifiers(leftIdentifier, rightIdentifier)
                                   : QString::compare(leftIdentifier, rightIdentifier,
                                                      Qt::CaseSensitive);
        if (comparison != 0) {
            return comparison;
        }
    }
    if (left.prerelease.size() == right.prerelease.size()) {
        return 0;
    }
    return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
}

QString wantedAssetSuffix() {
#if defined(Q_OS_WIN)
    const bool portable = QFileInfo::exists(
        QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("portable.marker")));
    return portable ? QStringLiteral("-windows-x64-portable.zip")
                    : QStringLiteral("-windows-x64.exe");
#elif defined(Q_OS_MACOS)
    const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
    if (architecture == QStringLiteral("arm64")
        || architecture == QStringLiteral("aarch64")) {
        return QStringLiteral("-macos-arm64.dmg");
    }
    if (architecture == QStringLiteral("x86_64") || architecture == QStringLiteral("amd64")) {
        return QStringLiteral("-macos-x86_64.dmg");
    }
    return {};
#elif defined(Q_OS_LINUX)
    const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
    if (architecture != QStringLiteral("x86_64") && architecture != QStringLiteral("amd64")) {
        return {};
    }
    if (!qEnvironmentVariableIsEmpty("APPIMAGE")
        || !QFileInfo::exists(QStringLiteral("/etc/debian_version"))) {
        return QStringLiteral("-linux-x86_64.AppImage");
    }
    return QStringLiteral(".deb");
#else
    return {};
#endif
}

struct DownloadState {
    explicit DownloadState(const QString &path)
        : file(path), hash(QCryptographicHash::Sha256) {}

    QSaveFile file;
    QCryptographicHash hash;
    QPointer<QProgressDialog> progress;
    bool writeFailed = false;
};
}

struct UpdateChecker::ReleaseAsset {
    QString version;
    QString name;
    QUrl url;
    QString sha256;
};

UpdateChecker::UpdateChecker(QWidget *dialogParent)
    : QObject(dialogParent),
      dialogParent_(dialogParent),
      network_(new QNetworkAccessManager(this)) {}

void UpdateChecker::checkForUpdates(const bool userInitiated) {
    if (busy_) {
        if (userInitiated) {
            QMessageBox::information(dialogParent_, QStringLiteral("Обновление SquidyGit"),
                                     QStringLiteral("Проверка обновлений уже выполняется."));
        }
        return;
    }

    if (!userInitiated) {
        QSettings settings;
        const QDateTime lastCheck =
            settings.value(QStringLiteral("updates/lastCheck")).toDateTime();
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const qint64 secondsSinceLastCheck = lastCheck.secsTo(now);
        if (lastCheck.isValid() && secondsSinceLastCheck >= 0
            && secondsSinceLastCheck < AutomaticCheckIntervalSeconds) {
            return;
        }
        settings.setValue(QStringLiteral("updates/lastCheck"), now);
    }

    busy_ = true;
    QNetworkReply *reply =
        network_->get(githubRequest(QUrl(QString::fromLatin1(ReleasesApiUrl))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, userInitiated] {
        const QByteArray response = reply->readAll();
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString errorString = reply->errorString();
        const int statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (networkError != QNetworkReply::NoError) {
            if (statusCode == 404) {
                finishWithError(QStringLiteral("Опубликованных выпусков пока нет."),
                                userInitiated);
            } else {
                finishWithError(QStringLiteral("Не удалось проверить обновления.\n%1")
                                    .arg(errorString),
                                userInitiated);
            }
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            finishWithError(QStringLiteral("GitHub вернул некорректный список выпусков."),
                            userInitiated);
            return;
        }

        const auto currentVersion = parseVersion(QApplication::applicationVersion());
        if (!currentVersion) {
            finishWithError(QStringLiteral("Не удалось определить установленную версию."),
                            userInitiated);
            return;
        }

        std::optional<SemanticVersion> availableVersion;
        QJsonObject release;
        for (const QJsonValue &value : document.array()) {
            const QJsonObject candidate = value.toObject();
            if (candidate.value(QStringLiteral("draft")).toBool()
                || (!currentVersion->isPrerelease()
                    && candidate.value(QStringLiteral("prerelease")).toBool())) {
                continue;
            }
            const auto candidateVersion =
                parseVersion(candidate.value(QStringLiteral("tag_name")).toString());
            if (!candidateVersion || compareVersions(*candidateVersion, *currentVersion) <= 0) {
                continue;
            }
            if (!availableVersion || compareVersions(*candidateVersion, *availableVersion) > 0) {
                availableVersion = candidateVersion;
                release = candidate;
            }
        }

        if (!availableVersion) {
            busy_ = false;
            if (userInitiated) {
                QMessageBox::information(
                    dialogParent_, QStringLiteral("Обновление SquidyGit"),
                    QStringLiteral("Установлена актуальная версия %1.")
                        .arg(QApplication::applicationVersion()));
            }
            return;
        }

        const QString suffix = wantedAssetSuffix();
        if (suffix.isEmpty()) {
            finishWithError(
                QStringLiteral("Для этой архитектуры пока нет готовой сборки.\n%1")
                    .arg(QString::fromLatin1(ReleasesPageUrl)),
                true);
            return;
        }

        std::optional<ReleaseAsset> selectedAsset;
        QString checksumsUrl;
        const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue &value : assets) {
            const QJsonObject object = value.toObject();
            const QString name = object.value(QStringLiteral("name")).toString();
            const QUrl url(object.value(QStringLiteral("browser_download_url")).toString());
            if (name == QStringLiteral("SHA256SUMS.txt") && isSafeDownloadUrl(url)) {
                checksumsUrl = url.toString();
            }
            if (name.endsWith(suffix, Qt::CaseInsensitive) && isSafeDownloadUrl(url)
                && QFileInfo(name).fileName() == name) {
                selectedAsset = ReleaseAsset{
                    availableVersion->display, name, url,
                    normalizedDigest(object.value(QStringLiteral("digest")).toString())};
            }
        }

        if (!selectedAsset) {
            finishWithError(
                QStringLiteral("В выпуске %1 нет пакета для этой платформы.\n%2")
                    .arg(availableVersion->display, QString::fromLatin1(ReleasesPageUrl)),
                true);
            return;
        }

        const QMessageBox::StandardButton answer = QMessageBox::question(
            dialogParent_, QStringLiteral("Доступно обновление"),
            QStringLiteral("Доступна SquidyGit %1. Сейчас установлена версия %2.\n\n"
                           "Скачать проверенный пакет обновления?")
                .arg(selectedAsset->version, QApplication::applicationVersion()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) {
            busy_ = false;
            return;
        }

        if (!selectedAsset->sha256.isEmpty()) {
            downloadAsset(*selectedAsset);
        } else if (!checksumsUrl.isEmpty()) {
            requestChecksums(*selectedAsset, checksumsUrl);
        } else {
            finishWithError(
                QStringLiteral("Пакет не скачан: в выпуске отсутствует контрольная сумма SHA-256."),
                true);
        }
    });
}

void UpdateChecker::requestChecksums(const ReleaseAsset &asset,
                                     const QString &checksumsUrl) {
    const QUrl url(checksumsUrl);
    if (!isSafeDownloadUrl(url)) {
        finishWithError(QStringLiteral("Небезопасный адрес файла контрольных сумм."), true);
        return;
    }

    QNetworkReply *reply = network_->get(githubRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, asset] {
        const QByteArray response = reply->readAll();
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString errorString = reply->errorString();
        reply->deleteLater();
        if (networkError != QNetworkReply::NoError) {
            finishWithError(QStringLiteral("Не удалось получить контрольные суммы.\n%1")
                                .arg(errorString),
                            true);
            return;
        }

        QString expectedDigest;
        const QStringList lines = QString::fromUtf8(response).split(QLatin1Char('\n'));
        static const QRegularExpression checksumLine(
            QStringLiteral("^([0-9a-fA-F]{64})\\s+\\*?(.+)$"));
        for (const QString &line : lines) {
            const QRegularExpressionMatch match = checksumLine.match(line.trimmed());
            if (match.hasMatch() && match.captured(2) == asset.name) {
                expectedDigest = match.captured(1).toLower();
                break;
            }
        }

        if (expectedDigest.isEmpty()) {
            finishWithError(QStringLiteral("Для пакета не найдена контрольная сумма SHA-256."),
                            true);
            return;
        }

        ReleaseAsset verifiedAsset = asset;
        verifiedAsset.sha256 = expectedDigest;
        downloadAsset(verifiedAsset);
    });
}

void UpdateChecker::downloadAsset(const ReleaseAsset &asset) {
    QString downloadDirectory =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloadDirectory.isEmpty()) {
        downloadDirectory = QDir::tempPath();
    }
    if (!QDir().mkpath(downloadDirectory)) {
        finishWithError(QStringLiteral("Не удалось создать папку для загрузки."), true);
        return;
    }

    const QString targetPath = QDir(downloadDirectory).filePath(asset.name);
    QFile existingFile(targetPath);
    if (existingFile.open(QIODevice::ReadOnly)) {
        QCryptographicHash existingHash(QCryptographicHash::Sha256);
        if (existingHash.addData(&existingFile)
            && QString::fromLatin1(existingHash.result().toHex()) == asset.sha256) {
            busy_ = false;
            openDownloadedPackage(targetPath);
            return;
        }
    }

    const auto state = QSharedPointer<DownloadState>::create(targetPath);
    if (!state->file.open(QIODevice::WriteOnly)) {
        finishWithError(QStringLiteral("Не удалось сохранить пакет в:\n%1").arg(targetPath),
                        true);
        return;
    }

    state->progress = new QProgressDialog(
        QStringLiteral("Загрузка %1…").arg(asset.name), QStringLiteral("Отмена"), 0, 100,
        dialogParent_);
    state->progress->setWindowTitle(QStringLiteral("Обновление SquidyGit"));
    state->progress->setWindowModality(Qt::WindowModal);
    state->progress->setMinimumDuration(0);
    state->progress->setValue(0);

    QNetworkReply *reply = network_->get(githubRequest(asset.url));
    connect(state->progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [state](const qint64 received, const qint64 total) {
                if (!state->progress || total <= 0) {
                    return;
                }
                state->progress->setValue(
                    static_cast<int>((received * 100) / total));
            });
    connect(reply, &QIODevice::readyRead, this, [reply, state] {
        const QByteArray chunk = reply->readAll();
        if (state->file.write(chunk) != chunk.size()) {
            state->writeFailed = true;
            reply->abort();
            return;
        }
        state->hash.addData(chunk);
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, state, asset, targetPath] {
                const QByteArray remaining = reply->readAll();
                if (!remaining.isEmpty()) {
                    if (state->file.write(remaining) != remaining.size()) {
                        state->writeFailed = true;
                    } else {
                        state->hash.addData(remaining);
                    }
                }

                const QNetworkReply::NetworkError networkError = reply->error();
                const QString errorString = reply->errorString();
                reply->deleteLater();
                if (state->progress) {
                    state->progress->close();
                    state->progress->deleteLater();
                }

                if (networkError != QNetworkReply::NoError || state->writeFailed) {
                    state->file.cancelWriting();
                    finishWithError(
                        state->writeFailed
                            ? QStringLiteral("Не удалось записать пакет обновления.")
                            : QStringLiteral("Загрузка обновления прервана.\n%1")
                                  .arg(errorString),
                        networkError != QNetworkReply::OperationCanceledError
                            || state->writeFailed);
                    return;
                }

                const QString actualDigest =
                    QString::fromLatin1(state->hash.result().toHex());
                if (actualDigest != asset.sha256) {
                    state->file.cancelWriting();
                    finishWithError(
                        QStringLiteral("Пакет повреждён: контрольная сумма SHA-256 не совпала."),
                        true);
                    return;
                }

                if (!state->file.commit()) {
                    finishWithError(QStringLiteral("Не удалось завершить сохранение пакета."),
                                    true);
                    return;
                }

                busy_ = false;
                openDownloadedPackage(targetPath);
            });
}

void UpdateChecker::openDownloadedPackage(const QString &path) const {
    const QFileInfo fileInfo(path);
    const bool isAppImage = path.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive);
    if (isAppImage) {
        QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ExeOwner
                                        | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    }

    QMessageBox::information(
        dialogParent_, QStringLiteral("Обновление загружено"),
        isAppImage
            ? QStringLiteral("Пакет проверен и сохранён в:\n%1\n\n"
                             "Замените текущий AppImage этим файлом и запустите его.")
                  .arg(path)
            : QStringLiteral("Пакет проверен и сохранён в:\n%1\n\n"
                             "Сейчас откроется штатная установка для вашей системы.")
                  .arg(path));

    const QUrl target = QUrl::fromLocalFile(isAppImage ? fileInfo.absolutePath() : path);
    if (!QDesktopServices::openUrl(target)) {
        QMessageBox::warning(dialogParent_, QStringLiteral("Обновление SquidyGit"),
                             QStringLiteral("Не удалось открыть пакет. Он сохранён в:\n%1")
                                 .arg(path));
    }
}

void UpdateChecker::finishWithError(const QString &message, const bool showMessage) {
    busy_ = false;
    if (showMessage) {
        QMessageBox::warning(dialogParent_, QStringLiteral("Обновление SquidyGit"), message);
    }
}
