// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "dialogs.h"

#include "icons.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

QLabel *hint(const QString &text) {
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("mutedText"));
    label->setWordWrap(true);
    return label;
}

QString repositoryNameFromUrl(const QString &url) {
    QString trimmed = url.trimmed();
    while (trimmed.endsWith(u'/')) {
        trimmed.chop(1);
    }
    if (trimmed.endsWith(QStringLiteral(".git"), Qt::CaseInsensitive)) {
        trimmed.chop(4);
    }
    const qsizetype slash = trimmed.lastIndexOf(u'/');
    const qsizetype colon = trimmed.lastIndexOf(u':');
    const qsizetype separator = qMax(slash, colon);
    return separator >= 0 ? trimmed.mid(separator + 1) : trimmed;
}

}

CloneDialog::CloneDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Clone Repository"));
    setModal(true);
    resize(640, 460);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    sourceEdit_ = new QLineEdit;
    sourceEdit_->setPlaceholderText(QStringLiteral("https://github.com/user/project.git"));
    form->addRow(tr("Source path or URL:"), sourceEdit_);

    auto *destinationRow = new QHBoxLayout;
    destinationEdit_ = new QLineEdit;
    auto *browseButton = new QPushButton(tr("Browse…"));
    destinationRow->addWidget(destinationEdit_, 1);
    destinationRow->addWidget(browseButton);
    form->addRow(tr("Local folder:"), destinationRow);

    recursiveCheck_ = new QCheckBox(tr("Clone submodules (--recurse-submodules)"));
    form->addRow(QString(), recursiveCheck_);
    layout->addLayout(form);

    layout->addWidget(hint(tr("Interactive password entry is disabled: use an SSH key or a "
                              "Git credential helper.")));

    progress_ = new QProgressBar;
    progress_->setRange(0, 0);
    progress_->hide();
    layout->addWidget(progress_);

    outputView_ = new QPlainTextEdit;
    outputView_->setReadOnly(true);
    outputView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    outputView_->setPlaceholderText(tr("The output of git clone appears here"));
    layout->addWidget(outputView_, 1);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Cancel);
    cloneButton_ = buttons_->addButton(tr("Clone"),
                                       QDialogButtonBox::AcceptRole);
    cloneButton_->setProperty("accent", true);
    cloneButton_->setIcon(Icons::icon(Icons::Glyph::Clone,
                                      Theme::instance()->palette().accentText));
    layout->addWidget(buttons_);

    destinationEdit_->setText(QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation)));

    connect(browseButton, &QPushButton::clicked, this, [this] { browseDestination(); });
    connect(sourceEdit_, &QLineEdit::textChanged, this, [this] { updateSuggestedDirectory(); });
    connect(cloneButton_, &QPushButton::clicked, this, [this] { startClone(); });
    connect(buttons_, &QDialogButtonBox::rejected, this, [this] {
        if (process_ != nullptr && process_->state() != QProcess::NotRunning) {
            process_->kill();
        }
        reject();
    });
}

QString CloneDialog::clonedPath() const {
    return clonedPath_;
}

void CloneDialog::browseDestination() {
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("Select the destination folder"), destinationEdit_->text());
    if (!directory.isEmpty()) {
        destinationEdit_->setText(QDir::toNativeSeparators(directory));
    }
}

void CloneDialog::updateSuggestedDirectory() {
    const QString name = repositoryNameFromUrl(sourceEdit_->text());
    if (name.isEmpty()) {
        return;
    }
    QString base = destinationEdit_->text();
    const QFileInfo info(base);
    if (info.fileName() != name) {
        const QString parent = info.exists() && info.isDir() && QDir(base).exists()
                                   ? base
                                   : info.absolutePath();
        destinationEdit_->setText(QDir::toNativeSeparators(QDir(parent).filePath(name)));
    }
}

void CloneDialog::startClone() {
    const QString source = sourceEdit_->text().trimmed();
    const QString destination = destinationEdit_->text().trimmed();
    if (source.isEmpty() || destination.isEmpty()) {
        appendOutput(tr("Enter the repository URL and the destination folder."));
        return;
    }

    const QDir destinationDir(destination);
    if (destinationDir.exists() && !destinationDir.isEmpty()) {
        appendOutput(tr("The folder %1 is not empty.").arg(destination));
        return;
    }

    const QString executable = GitClient::gitExecutable();
    if (executable.isEmpty()) {
        appendOutput(tr("Git was not found in PATH."));
        return;
    }

    QStringList arguments{QStringLiteral("clone"), QStringLiteral("--progress")};
    if (recursiveCheck_->isChecked()) {
        arguments.append(QStringLiteral("--recurse-submodules"));
    }
    arguments.append(source);
    arguments.append(destination);

    process_ = new QProcess(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    process_->setProcessEnvironment(environment);
    process_->setProgram(executable);
    process_->setArguments(arguments);

    connect(process_, &QProcess::readyReadStandardOutput, this, [this] {
        appendOutput(QString::fromUtf8(process_->readAllStandardOutput()));
    });
    connect(process_, &QProcess::readyReadStandardError, this, [this] {
        appendOutput(QString::fromUtf8(process_->readAllStandardError()));
    });
    connect(process_, &QProcess::finished, this,
            [this](const int exitCode) { finishClone(exitCode); });
    connect(process_, &QProcess::errorOccurred, this, [this] {
        appendOutput(process_->errorString());
        finishClone(-1);
    });

    clonedPath_ = QDir::cleanPath(destination);
    cloneButton_->setEnabled(false);
    sourceEdit_->setEnabled(false);
    destinationEdit_->setEnabled(false);
    progress_->show();
    appendOutput(QStringLiteral("git %1\n").arg(arguments.join(u' ')));
    process_->start();
}

void CloneDialog::appendOutput(const QString &text) {
    if (text.isEmpty()) {
        return;
    }
    QString normalized = text;
    normalized.replace(u'\r', u'\n');
    outputView_->appendPlainText(normalized.trimmed());
}

void CloneDialog::finishClone(const int exitCode) {
    progress_->hide();
    cloneButton_->setEnabled(true);
    sourceEdit_->setEnabled(true);
    destinationEdit_->setEnabled(true);

    if (exitCode == 0) {
        appendOutput(tr("\nDone."));
        accept();
    } else {
        clonedPath_.clear();
        appendOutput(tr("\nCloning failed (code %1).").arg(exitCode));
    }
}

BranchDialog::BranchDialog(const QString &startPointDescription, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("New Branch"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    nameEdit_ = new QLineEdit;
    nameEdit_->setPlaceholderText(tr("feature/new-feature"));
    form->addRow(tr("Branch name:"), nameEdit_);
    form->addRow(tr("Create from:"), new QLabel(startPointDescription));
    layout->addLayout(form);

    checkoutCheck_ = new QCheckBox(tr("Switch to the new branch"));
    checkoutCheck_->setChecked(true);
    layout->addWidget(checkoutCheck_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create Branch"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("accent", true);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(nameEdit_, &QLineEdit::textChanged, this, [buttons](const QString &text) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
    });
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
}

QString BranchDialog::branchName() const {
    return nameEdit_->text().trimmed();
}

bool BranchDialog::checkoutAfterCreate() const {
    return checkoutCheck_->isChecked();
}

MergeDialog::MergeDialog(const QString &source, const QString &target, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Merge"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Merge “%1” into the current branch “%2”.")
                                     .arg(source, target)));

    noFastForwardCheck_ = new QCheckBox(
        tr("Create a merge commit even when fast-forward is possible"));
    squashCheck_ = new QCheckBox(tr("Squash — combine the changes into one"));
    commitCheck_ = new QCheckBox(tr("Commit the result right away"));
    commitCheck_->setChecked(true);
    layout->addWidget(noFastForwardCheck_);
    layout->addWidget(squashCheck_);
    layout->addWidget(commitCheck_);

    connect(squashCheck_, &QCheckBox::toggled, this, [this](const bool checked) {
        noFastForwardCheck_->setEnabled(!checked);
        commitCheck_->setEnabled(!checked);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Merge", "button"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("accent", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool MergeDialog::noFastForward() const {
    return noFastForwardCheck_->isChecked();
}

bool MergeDialog::squash() const {
    return squashCheck_->isChecked();
}

bool MergeDialog::commitResult() const {
    return commitCheck_->isChecked();
}

TagDialog::TagDialog(const QString &revisionDescription, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("New Tag"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    nameEdit_ = new QLineEdit;
    nameEdit_->setPlaceholderText(QStringLiteral("v1.0.0"));
    messageEdit_ = new QLineEdit;
    messageEdit_->setPlaceholderText(tr("Optional description (annotated tag)"));
    form->addRow(tr("Tag name:"), nameEdit_);
    form->addRow(tr("Message:"), messageEdit_);
    form->addRow(tr("Commit:"), new QLabel(revisionDescription));
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create Tag"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("accent", true);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    layout->addWidget(buttons);

    connect(nameEdit_, &QLineEdit::textChanged, this, [buttons](const QString &text) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString TagDialog::tagName() const {
    return nameEdit_->text().trimmed();
}

QString TagDialog::tagMessage() const {
    return messageEdit_->text().trimmed();
}

StashDialog::StashDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Stash Changes"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    messageEdit_ = new QLineEdit;
    messageEdit_->setPlaceholderText(tr("Stash description"));
    form->addRow(tr("Message:"), messageEdit_);
    layout->addLayout(form);

    keepStagedCheck_ = new QCheckBox(tr("Keep the staged changes"));
    untrackedCheck_ = new QCheckBox(tr("Include untracked files"));
    untrackedCheck_->setChecked(true);
    layout->addWidget(keepStagedCheck_);
    layout->addWidget(untrackedCheck_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Stash"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("accent", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString StashDialog::message() const {
    return messageEdit_->text().trimmed();
}

bool StashDialog::keepStaged() const {
    return keepStagedCheck_->isChecked();
}

bool StashDialog::includeUntracked() const {
    return untrackedCheck_->isChecked();
}

ResetDialog::ResetDialog(const QString &revisionDescription, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Reset Current Branch"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Move the current branch to %1")
                                     .arg(revisionDescription)));

    softButton_ = new QRadioButton(tr("Soft — keep the changes staged"));
    mixedButton_ = new QRadioButton(tr("Mixed — keep the changes in the working tree"));
    hardButton_ = new QRadioButton(tr("Hard — discard all local changes"));
    mixedButton_->setChecked(true);
    layout->addWidget(softButton_);
    layout->addWidget(mixedButton_);
    layout->addWidget(hardButton_);
    layout->addWidget(hint(tr("A hard reset irreversibly deletes uncommitted changes in "
                              "the working tree.")));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Reset"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

GitResetMode ResetDialog::resetMode() const {
    if (softButton_->isChecked()) {
        return GitResetMode::Soft;
    }
    if (hardButton_->isChecked()) {
        return GitResetMode::Hard;
    }
    return GitResetMode::Mixed;
}

PushDialog::PushDialog(const QList<GitRemoteInfo> &remotes, const QList<GitBranchInfo> &branches,
                       const QString &currentBranch, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Push"));
    setModal(true);
    resize(460, 380);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    remoteCombo_ = new QComboBox;
    for (const GitRemoteInfo &remote : remotes) {
        remoteCombo_->addItem(QStringLiteral("%1  (%2)").arg(remote.name, remote.url),
                              remote.name);
    }
    if (remoteCombo_->count() == 0) {
        remoteCombo_->addItem(QStringLiteral("origin"), QStringLiteral("origin"));
    }
    form->addRow(tr("Repository:"), remoteCombo_);
    layout->addLayout(form);

    layout->addWidget(new QLabel(tr("Branches to push:")));
    branchList_ = new QListWidget;
    for (const GitBranchInfo &branch : branches) {
        auto *item = new QListWidgetItem(branch.name, branchList_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(branch.name == currentBranch ? Qt::Checked : Qt::Unchecked);
        if (!branch.upstream.isEmpty()) {
            item->setToolTip(tr("Tracks %1 (ahead %2, behind %3)")
                                 .arg(branch.upstream)
                                 .arg(branch.ahead)
                                 .arg(branch.behind));
        }
    }
    layout->addWidget(branchList_, 1);

    upstreamCheck_ = new QCheckBox(tr("Track the branch in the remote repository"));
    upstreamCheck_->setChecked(true);
    tagsCheck_ = new QCheckBox(tr("Push tags"));
    forceCheck_ = new QCheckBox(tr("Force (--force-with-lease)"));
    layout->addWidget(upstreamCheck_);
    layout->addWidget(tagsCheck_);
    layout->addWidget(forceCheck_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Push"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("accent", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString PushDialog::remote() const {
    return remoteCombo_->currentData().toString();
}

QStringList PushDialog::selectedBranches() const {
    QStringList branches;
    for (int index = 0; index < branchList_->count(); ++index) {
        const QListWidgetItem *item = branchList_->item(index);
        if (item->checkState() == Qt::Checked) {
            branches.append(item->text());
        }
    }
    return branches;
}

bool PushDialog::pushTags() const {
    return tagsCheck_->isChecked();
}

bool PushDialog::forcePush() const {
    return forceCheck_->isChecked();
}

bool PushDialog::setUpstream() const {
    return upstreamCheck_->isChecked();
}

RemoteDialog::RemoteDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Add Remote"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    nameEdit_ = new QLineEdit(QStringLiteral("origin"));
    urlEdit_ = new QLineEdit;
    urlEdit_->setPlaceholderText(QStringLiteral("git@github.com:user/project.git"));
    form->addRow(tr("Name:"), nameEdit_);
    form->addRow(QStringLiteral("URL:"), urlEdit_);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setProperty("accent", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString RemoteDialog::remoteName() const {
    return nameEdit_->text().trimmed();
}

QString RemoteDialog::remoteUrl() const {
    return urlEdit_->text().trimmed();
}

PreferencesDialog::PreferencesDialog(const QString &userName, const QString &userEmail,
                                     QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    nameEdit_ = new QLineEdit(userName);
    emailEdit_ = new QLineEdit(userEmail);
    themeCombo_ = new QComboBox;
    themeCombo_->addItem(tr("Light"), false);
    themeCombo_->addItem(tr("Dark"), true);
    historyCombo_ = new QComboBox;
    for (const int limit : {200, 500, 1000, 5000}) {
        historyCombo_->addItem(tr("%1 commits").arg(limit), limit);
    }
    themeCombo_->setCurrentIndex(Theme::instance()->mode() == Theme::Mode::Dark ? 1 : 0);
    historyCombo_->setCurrentIndex(qMax(0, historyCombo_->findData(
        QSettings().value(QStringLiteral("historyLimit"), 500).toInt())));
    form->addRow(tr("Author name:"), nameEdit_);
    form->addRow(QStringLiteral("E-mail:"), emailEdit_);
    form->addRow(tr("Theme:"), themeCombo_);
    form->addRow(tr("History depth:"), historyCombo_);
    layout->addLayout(form);
    layout->addWidget(hint(tr("The name and e-mail are written to the local repository "
                              "configuration.")));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setProperty("accent", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString PreferencesDialog::userName() const {
    return nameEdit_->text().trimmed();
}

QString PreferencesDialog::userEmail() const {
    return emailEdit_->text().trimmed();
}

bool PreferencesDialog::darkTheme() const {
    return themeCombo_->currentData().toBool();
}

int PreferencesDialog::historyLimit() const {
    return historyCombo_->currentData().toInt();
}
