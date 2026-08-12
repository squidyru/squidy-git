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
    setWindowTitle(QStringLiteral("Клонировать репозиторий"));
    setModal(true);
    resize(640, 460);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    sourceEdit_ = new QLineEdit;
    sourceEdit_->setPlaceholderText(QStringLiteral("https://github.com/user/project.git"));
    form->addRow(QStringLiteral("Исходный путь / URL:"), sourceEdit_);

    auto *destinationRow = new QHBoxLayout;
    destinationEdit_ = new QLineEdit;
    auto *browseButton = new QPushButton(QStringLiteral("Обзор…"));
    destinationRow->addWidget(destinationEdit_, 1);
    destinationRow->addWidget(browseButton);
    form->addRow(QStringLiteral("Локальная папка:"), destinationRow);

    recursiveCheck_ = new QCheckBox(QStringLiteral("Клонировать подмодули (--recurse-submodules)"));
    form->addRow(QString(), recursiveCheck_);
    layout->addLayout(form);

    layout->addWidget(hint(QStringLiteral(
        "Интерактивный ввод пароля отключён: используйте SSH-ключ или менеджер учётных данных Git.")));

    progress_ = new QProgressBar;
    progress_->setRange(0, 0);
    progress_->hide();
    layout->addWidget(progress_);

    outputView_ = new QPlainTextEdit;
    outputView_->setReadOnly(true);
    outputView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    outputView_->setPlaceholderText(QStringLiteral("Вывод git clone появится здесь"));
    layout->addWidget(outputView_, 1);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Cancel);
    cloneButton_ = buttons_->addButton(QStringLiteral("Клонировать"),
                                       QDialogButtonBox::AcceptRole);
    cloneButton_->setProperty("accent", true);
    cloneButton_->setIcon(Icons::icon(Icons::Glyph::Clone));
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
        this, QStringLiteral("Выберите папку назначения"), destinationEdit_->text());
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
        appendOutput(QStringLiteral("Укажите URL репозитория и папку назначения."));
        return;
    }

    const QDir destinationDir(destination);
    if (destinationDir.exists() && !destinationDir.isEmpty()) {
        appendOutput(QStringLiteral("Папка %1 не пуста.").arg(destination));
        return;
    }

    const QString executable = GitClient::gitExecutable();
    if (executable.isEmpty()) {
        appendOutput(QStringLiteral("Git не найден в PATH."));
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
        appendOutput(QStringLiteral("\nГотово."));
        accept();
    } else {
        clonedPath_.clear();
        appendOutput(QStringLiteral("\nКлонирование не удалось (код %1).").arg(exitCode));
    }
}

BranchDialog::BranchDialog(const QString &startPointDescription, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Новая ветка"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    nameEdit_ = new QLineEdit;
    nameEdit_->setPlaceholderText(QStringLiteral("feature/новая-функция"));
    form->addRow(QStringLiteral("Имя ветки:"), nameEdit_);
    form->addRow(QStringLiteral("Создать от:"), new QLabel(startPointDescription));
    layout->addLayout(form);

    checkoutCheck_ = new QCheckBox(QStringLiteral("Переключиться на новую ветку"));
    checkoutCheck_->setChecked(true);
    layout->addWidget(checkoutCheck_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Создать ветку"));
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
    setWindowTitle(QStringLiteral("Слияние"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Влить «%1» в текущую ветку «%2».")
                                     .arg(source, target)));

    noFastForwardCheck_ = new QCheckBox(
        QStringLiteral("Создать коммит слияния, даже если возможен fast-forward"));
    squashCheck_ = new QCheckBox(QStringLiteral("Squash — свести изменения в одно"));
    commitCheck_ = new QCheckBox(QStringLiteral("Зафиксировать результат сразу"));
    commitCheck_->setChecked(true);
    layout->addWidget(noFastForwardCheck_);
    layout->addWidget(squashCheck_);
    layout->addWidget(commitCheck_);

    connect(squashCheck_, &QCheckBox::toggled, this, [this](const bool checked) {
        noFastForwardCheck_->setEnabled(!checked);
        commitCheck_->setEnabled(!checked);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Слить"));
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
    setWindowTitle(QStringLiteral("Новый тег"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    nameEdit_ = new QLineEdit;
    nameEdit_->setPlaceholderText(QStringLiteral("v1.0.0"));
    messageEdit_ = new QLineEdit;
    messageEdit_->setPlaceholderText(QStringLiteral("Необязательное описание (annotated tag)"));
    form->addRow(QStringLiteral("Имя тега:"), nameEdit_);
    form->addRow(QStringLiteral("Сообщение:"), messageEdit_);
    form->addRow(QStringLiteral("Коммит:"), new QLabel(revisionDescription));
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Создать тег"));
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
    setWindowTitle(QStringLiteral("Спрятать изменения"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    messageEdit_ = new QLineEdit;
    messageEdit_->setPlaceholderText(QStringLiteral("Описание stash'а"));
    form->addRow(QStringLiteral("Сообщение:"), messageEdit_);
    layout->addLayout(form);

    keepStagedCheck_ = new QCheckBox(QStringLiteral("Оставить проиндексированные изменения"));
    untrackedCheck_ = new QCheckBox(QStringLiteral("Включить неотслеживаемые файлы"));
    untrackedCheck_->setChecked(true);
    layout->addWidget(keepStagedCheck_);
    layout->addWidget(untrackedCheck_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Спрятать"));
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
    setWindowTitle(QStringLiteral("Сбросить текущую ветку"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Переместить текущую ветку на %1")
                                     .arg(revisionDescription)));

    softButton_ = new QRadioButton(QStringLiteral("Soft — оставить изменения в индексе"));
    mixedButton_ = new QRadioButton(QStringLiteral("Mixed — оставить изменения в рабочей копии"));
    hardButton_ = new QRadioButton(QStringLiteral("Hard — удалить все локальные изменения"));
    mixedButton_->setChecked(true);
    layout->addWidget(softButton_);
    layout->addWidget(mixedButton_);
    layout->addWidget(hardButton_);
    layout->addWidget(hint(QStringLiteral(
        "Hard-сброс необратимо удаляет несохранённые изменения в рабочей копии.")));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Сбросить"));
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
    form->addRow(QStringLiteral("Репозиторий:"), remoteCombo_);
    layout->addLayout(form);

    layout->addWidget(new QLabel(QStringLiteral("Ветки для отправки:")));
    branchList_ = new QListWidget;
    for (const GitBranchInfo &branch : branches) {
        auto *item = new QListWidgetItem(branch.name, branchList_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(branch.name == currentBranch ? Qt::Checked : Qt::Unchecked);
        if (!branch.upstream.isEmpty()) {
            item->setToolTip(QStringLiteral("Отслеживает %1 (ahead %2, behind %3)")
                                 .arg(branch.upstream)
                                 .arg(branch.ahead)
                                 .arg(branch.behind));
        }
    }
    layout->addWidget(branchList_, 1);

    upstreamCheck_ = new QCheckBox(QStringLiteral("Отслеживать ветку в удалённом репозитории"));
    upstreamCheck_->setChecked(true);
    tagsCheck_ = new QCheckBox(QStringLiteral("Отправить теги"));
    forceCheck_ = new QCheckBox(QStringLiteral("Принудительно (--force-with-lease)"));
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
    setWindowTitle(QStringLiteral("Добавить удалённый репозиторий"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    nameEdit_ = new QLineEdit(QStringLiteral("origin"));
    urlEdit_ = new QLineEdit;
    urlEdit_->setPlaceholderText(QStringLiteral("git@github.com:user/project.git"));
    form->addRow(QStringLiteral("Имя:"), nameEdit_);
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
    setWindowTitle(QStringLiteral("Настройки"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    nameEdit_ = new QLineEdit(userName);
    emailEdit_ = new QLineEdit(userEmail);
    themeCombo_ = new QComboBox;
    themeCombo_->addItem(QStringLiteral("Светлая"), false);
    themeCombo_->addItem(QStringLiteral("Тёмная"), true);
    historyCombo_ = new QComboBox;
    for (const int limit : {200, 500, 1000, 5000}) {
        historyCombo_->addItem(QStringLiteral("%1 коммитов").arg(limit), limit);
    }
    themeCombo_->setCurrentIndex(Theme::instance()->mode() == Theme::Mode::Dark ? 1 : 0);
    historyCombo_->setCurrentIndex(qMax(0, historyCombo_->findData(
        QSettings().value(QStringLiteral("historyLimit"), 500).toInt())));
    form->addRow(QStringLiteral("Имя автора:"), nameEdit_);
    form->addRow(QStringLiteral("E-mail:"), emailEdit_);
    form->addRow(QStringLiteral("Тема оформления:"), themeCombo_);
    form->addRow(QStringLiteral("Глубина истории:"), historyCombo_);
    layout->addLayout(form);
    layout->addWidget(hint(QStringLiteral(
        "Имя и e-mail записываются в локальную конфигурацию репозитория.")));

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
