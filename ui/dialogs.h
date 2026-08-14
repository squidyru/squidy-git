// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include "core/gitclient.h"

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QPushButton;
class QRadioButton;

/// Clone a remote repository while streaming git's progress output, like
/// Repository cloning dialog.
class CloneDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CloneDialog(QWidget *parent = nullptr);

    [[nodiscard]] QString clonedPath() const;

private:
    void browseDestination();
    void updateSuggestedDirectory();
    void startClone();
    void appendOutput(const QString &text);
    void finishClone(int exitCode);

    QLineEdit *sourceEdit_ = nullptr;
    QLineEdit *destinationEdit_ = nullptr;
    QCheckBox *recursiveCheck_ = nullptr;
    QPlainTextEdit *outputView_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QPushButton *cloneButton_ = nullptr;
    QDialogButtonBox *buttons_ = nullptr;
    QProcess *process_ = nullptr;
    QString clonedPath_;
};

class BranchDialog final : public QDialog {
    Q_OBJECT

public:
    BranchDialog(const QString &startPointDescription, QWidget *parent = nullptr);

    [[nodiscard]] QString branchName() const;
    [[nodiscard]] bool checkoutAfterCreate() const;

private:
    QLineEdit *nameEdit_ = nullptr;
    QCheckBox *checkoutCheck_ = nullptr;
};

class MergeDialog final : public QDialog {
    Q_OBJECT

public:
    MergeDialog(const QString &source, const QString &target, QWidget *parent = nullptr);

    [[nodiscard]] bool noFastForward() const;
    [[nodiscard]] bool squash() const;
    [[nodiscard]] bool commitResult() const;

private:
    QCheckBox *noFastForwardCheck_ = nullptr;
    QCheckBox *squashCheck_ = nullptr;
    QCheckBox *commitCheck_ = nullptr;
};

class TagDialog final : public QDialog {
    Q_OBJECT

public:
    TagDialog(const QString &revisionDescription, QWidget *parent = nullptr);

    [[nodiscard]] QString tagName() const;
    [[nodiscard]] QString tagMessage() const;

private:
    QLineEdit *nameEdit_ = nullptr;
    QLineEdit *messageEdit_ = nullptr;
};

class StashDialog final : public QDialog {
    Q_OBJECT

public:
    explicit StashDialog(QWidget *parent = nullptr);

    [[nodiscard]] QString message() const;
    [[nodiscard]] bool keepStaged() const;
    [[nodiscard]] bool includeUntracked() const;

private:
    QLineEdit *messageEdit_ = nullptr;
    QCheckBox *keepStagedCheck_ = nullptr;
    QCheckBox *untrackedCheck_ = nullptr;
};

class ResetDialog final : public QDialog {
    Q_OBJECT

public:
    ResetDialog(const QString &revisionDescription, QWidget *parent = nullptr);

    [[nodiscard]] GitResetMode resetMode() const;

private:
    QRadioButton *softButton_ = nullptr;
    QRadioButton *mixedButton_ = nullptr;
    QRadioButton *hardButton_ = nullptr;
};

class PushDialog final : public QDialog {
    Q_OBJECT

public:
    PushDialog(const QList<GitRemoteInfo> &remotes, const QList<GitBranchInfo> &branches,
               const QString &currentBranch, QWidget *parent = nullptr);

    [[nodiscard]] QString remote() const;
    [[nodiscard]] QStringList selectedBranches() const;
    [[nodiscard]] bool pushTags() const;
    [[nodiscard]] bool forcePush() const;
    [[nodiscard]] bool setUpstream() const;

private:
    QComboBox *remoteCombo_ = nullptr;
    QListWidget *branchList_ = nullptr;
    QCheckBox *tagsCheck_ = nullptr;
    QCheckBox *forceCheck_ = nullptr;
    QCheckBox *upstreamCheck_ = nullptr;
};

class RemoteDialog final : public QDialog {
    Q_OBJECT

public:
    explicit RemoteDialog(QWidget *parent = nullptr);

    [[nodiscard]] QString remoteName() const;
    [[nodiscard]] QString remoteUrl() const;

private:
    QLineEdit *nameEdit_ = nullptr;
    QLineEdit *urlEdit_ = nullptr;
};

class PreferencesDialog final : public QDialog {
    Q_OBJECT

public:
    PreferencesDialog(const QString &userName, const QString &userEmail,
                      QWidget *parent = nullptr);

    [[nodiscard]] QString userName() const;
    [[nodiscard]] QString userEmail() const;
    [[nodiscard]] bool darkTheme() const;
    [[nodiscard]] int historyLimit() const;

private:
    QLineEdit *nameEdit_ = nullptr;
    QLineEdit *emailEdit_ = nullptr;
    QComboBox *themeCombo_ = nullptr;
    QComboBox *historyCombo_ = nullptr;
};
