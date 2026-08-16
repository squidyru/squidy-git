// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "updatechecker.h"

#include "platform/platformservices.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
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
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSharedPointer>
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
            QMessageBox::information(dialogParent_, tr("SquidyGit Update"),
                                     tr("An update check is already running."));
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
                finishWithError(tr("There are no published releases yet."),
                                userInitiated);
            } else {
                finishWithError(tr("The update check failed.\n%1")
                                    .arg(errorString),
                                userInitiated);
            }
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            finishWithError(tr("GitHub returned a malformed list of releases."),
                            userInitiated);
            return;
        }

        const auto currentVersion = parseVersion(QApplication::applicationVersion());
        if (!currentVersion) {
            finishWithError(tr("The installed version could not be determined."),
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
                    dialogParent_, tr("SquidyGit Update"),
                    tr("Version %1 is up to date.")
                        .arg(QApplication::applicationVersion()));
            }
            return;
        }

        const QString suffix = PlatformServices::instance().updateAssetSuffix();
        if (suffix.isEmpty()) {
            finishWithError(
                tr("There is no build for this architecture yet.\n%1")
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
                tr("Release %1 has no package for this platform.\n%2")
                    .arg(availableVersion->display, QString::fromLatin1(ReleasesPageUrl)),
                true);
            return;
        }

        const QMessageBox::StandardButton answer = QMessageBox::question(
            dialogParent_, tr("Update available"),
            tr("SquidyGit %1 is available. Version %2 is installed.\n\nDownload the "
               "verified update package?")
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
                tr("The package was not downloaded: the release has no SHA-256 checksum."),
                true);
        }
    });
}

void UpdateChecker::requestChecksums(const ReleaseAsset &asset,
                                     const QString &checksumsUrl) {
    const QUrl url(checksumsUrl);
    if (!isSafeDownloadUrl(url)) {
        finishWithError(tr("The address of the checksum file is not safe."), true);
        return;
    }

    QNetworkReply *reply = network_->get(githubRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, asset] {
        const QByteArray response = reply->readAll();
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString errorString = reply->errorString();
        reply->deleteLater();
        if (networkError != QNetworkReply::NoError) {
            finishWithError(tr("The checksums could not be downloaded.\n%1")
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
            finishWithError(tr("No SHA-256 checksum was found for the package."),
                            true);
            return;
        }

        ReleaseAsset verifiedAsset = asset;
        verifiedAsset.sha256 = expectedDigest;
        downloadAsset(verifiedAsset);
    });
}

void UpdateChecker::downloadAsset(const ReleaseAsset &asset) {
    const QString downloadDirectory = PlatformServices::instance().downloadDirectory();
    if (!QDir().mkpath(downloadDirectory)) {
        finishWithError(tr("The download folder could not be created."), true);
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
        finishWithError(tr("The package could not be saved to:\n%1").arg(targetPath),
                        true);
        return;
    }

    state->progress = new QProgressDialog(
        tr("Downloading %1…").arg(asset.name), tr("Cancel"), 0, 100,
        dialogParent_);
    state->progress->setWindowTitle(tr("SquidyGit Update"));
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
                            ? tr("The update package could not be written.")
                            : tr("The update download was interrupted.\n%1")
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
                        tr("The package is damaged: the SHA-256 checksum does not match."),
                        true);
                    return;
                }

                if (!state->file.commit()) {
                    finishWithError(tr("Saving the package could not be completed."),
                                    true);
                    return;
                }

                busy_ = false;
                openDownloadedPackage(targetPath);
            });
}

void UpdateChecker::openDownloadedPackage(const QString &path) {
    PlatformServices &platform = PlatformServices::instance();
    const UpdatePackageKind kind = platform.updatePackageKind(path);
    if (kind == UpdatePackageKind::SystemInstaller) {
        installSystemPackage(path);
        return;
    }
    if (kind == UpdatePackageKind::NativeInstaller) {
        installNativePackage(path);
        return;
    }

    platform.prepareDownloadedPackage(path);
    const bool manualExecutable = kind == UpdatePackageKind::ManualExecutable;
    const bool manualArchive = kind == UpdatePackageKind::ManualArchive;

    QString hint = tr("The standard installer of your system is about to open.");
    if (manualExecutable) {
        hint = tr("Replace the current AppImage with this file and start it.");
    } else if (manualArchive) {
        hint = tr("Unpack the archive over the current program folder.");
    }

    QMessageBox::information(dialogParent_, tr("Update downloaded"),
                             tr("The package is verified and saved to:\n%1\n\n%2")
                                 .arg(path, hint));

    const bool opened = manualExecutable || manualArchive
                            ? platform.revealInFileManager(path)
                            : platform.openPath(path);
    if (!opened) {
        QMessageBox::warning(dialogParent_, tr("SquidyGit Update"),
                             tr("The package could not be opened. It is saved to:\n%1")
                                 .arg(path));
    }
}

void UpdateChecker::installNativePackage(const QString &path) {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        dialogParent_, tr("Install update"),
        tr("The package is verified and ready to install:\n%1\n\nThe system will ask for "
           "administrator rights. SquidyGit closes for the installation and starts again "
           "afterwards. Install the update now?")
            .arg(path),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        return;
    }

    const InstallerLaunchResult result =
        PlatformServices::instance().launchUpdateInstaller(path);
    if (result == InstallerLaunchResult::Started) {
        return;
    }

    QMessageBox::warning(
        dialogParent_, tr("SquidyGit Update"),
        result == InstallerLaunchResult::Cancelled
            ? tr("The installation was cancelled. The package is saved to:\n%1").arg(path)
            : tr("The update installation could not be started.\nThe package is saved "
                 "to:\n%1")
                  .arg(path));
}

void UpdateChecker::installSystemPackage(const QString &path) {
    if (packageInstaller_ != nullptr) {
        QMessageBox::information(dialogParent_, tr("SquidyGit Update"),
                                 tr("An update installation is already running."));
        return;
    }

    const std::optional<PlatformCommand> command =
        PlatformServices::instance().systemPackageInstaller(path);
    if (!command) {
        QMessageBox::information(
            dialogParent_, tr("Update downloaded"),
            tr("The package is verified and saved to:\n%1\n\nThe system package installer was "
               "not found. The package will be opened manually.")
                .arg(path));
        if (!PlatformServices::instance().openPath(path)) {
            QMessageBox::warning(dialogParent_, tr("SquidyGit Update"),
                                 tr("The package could not be opened. It is saved to:\n%1")
                                     .arg(path));
        }
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        dialogParent_, tr("Install update"),
        tr("The package is verified and ready to install:\n%1\n\nThe system will ask for "
           "administrator rights. Install the update now?")
            .arg(path),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        return;
    }

    auto *progress = new QProgressDialog(
        tr("The system is installing the update…"), QString(), 0, 0, dialogParent_);
    progress->setWindowTitle(tr("SquidyGit Update"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(0);
    progress->show();

    const QString previousVersion = PlatformServices::instance().installedPackageVersion();

    auto *process = new QProcess(this);
    packageInstaller_ = process;
    busy_ = true;
    process->setProcessChannelMode(QProcess::MergedChannels);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (auto iterator = command->environment.cbegin();
         iterator != command->environment.cend(); ++iterator) {
        environment.insert(iterator.key(), iterator.value());
    }
    process->setProcessEnvironment(environment);

    connect(process, &QProcess::errorOccurred, this,
            [this, process, progress, path](const QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart || packageInstaller_ != process) {
                    return;
                }
                packageInstaller_ = nullptr;
                busy_ = false;
                progress->close();
                progress->deleteLater();
                process->deleteLater();
                QMessageBox::warning(
                    dialogParent_, tr("SquidyGit Update"),
                    tr("The system installation could not be started.\nThe package is "
                       "saved to:\n%1")
                        .arg(path));
            });
    connect(process, &QProcess::finished, this,
            [this, process, progress, path, previousVersion](
                const int exitCode, const QProcess::ExitStatus exitStatus) {
                if (packageInstaller_ != process) {
                    return;
                }
                const QString output = QString::fromLocal8Bit(process->readAll()).trimmed();
                packageInstaller_ = nullptr;
                busy_ = false;
                progress->close();
                progress->deleteLater();
                process->deleteLater();

                if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                    QString message =
                        PlatformServices::instance().isSystemInstallerCancellation(exitCode)
                            ? tr("The installation was cancelled: administrator rights "
                                 "were not granted.")
                            : tr("The installation failed.");
                    if (!output.isEmpty()) {
                        message += QStringLiteral("\n\n%1").arg(output.right(1600));
                    }
                    QMessageBox::warning(dialogParent_,
                                         tr("SquidyGit Update"), message);
                    return;
                }

                const QString currentVersion =
                    PlatformServices::instance().installedPackageVersion();
                if (!currentVersion.isEmpty() && currentVersion == previousVersion) {
                    QMessageBox::warning(
                        dialogParent_, tr("SquidyGit Update"),
                        tr("The package version did not change (%1). Install the package "
                           "manually:\n%2")
                            .arg(currentVersion, path));
                    return;
                }

                const QMessageBox::StandardButton restart = QMessageBox::question(
                    dialogParent_, tr("Update installed"),
                    tr("SquidyGit was updated successfully. Restart the application?"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                if (restart != QMessageBox::Yes) {
                    return;
                }

                if (PlatformServices::instance().restartApplication()) {
                    QApplication::quit();
                } else {
                    QMessageBox::warning(
                        dialogParent_, tr("SquidyGit Update"),
                        tr("The application could not be restarted. Start SquidyGit "
                           "manually."));
                }
            });

    process->start(command->program, command->arguments);
}

void UpdateChecker::finishWithError(const QString &message, const bool showMessage) {
    busy_ = false;
    if (showMessage) {
        QMessageBox::warning(dialogParent_, tr("SquidyGit Update"), message);
    }
}
