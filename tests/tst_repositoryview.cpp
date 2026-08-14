// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "core/gitclient.h"
#include "ui/commitgraph.h"
#include "ui/commitmodel.h"
#include "ui/repositoryview.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTreeView>

// RepositoryView integration tests.
class TestRepositoryView final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void appliesFirstSnapshot();
    void refreshesAfterExternalIndexChange();
    void feedsTheHistoryViewFromTheModel();

private:
    void writeFile(const QString &name, const QString &contents) const;
    [[nodiscard]] GitClient repository() const;
    [[nodiscard]] static QAbstractItemModel *historyModel(const RepositoryView &view);

    QTemporaryDir *directory_ = nullptr;
};

void TestRepositoryView::initTestCase() {
    if (GitClient::gitExecutable().isEmpty()) {
        QSKIP("git is not installed, the view suite cannot run");
    }
}

void TestRepositoryView::init() {
    directory_ = new QTemporaryDir;
    QVERIFY(directory_->isValid());
    QVERIFY(GitClient::initRepository(directory_->path(), false).succeeded());

    GitClient git = repository();
    QVERIFY(git.runCustom({QStringLiteral("config"), QStringLiteral("user.name"),
                           QStringLiteral("Test")}).succeeded());
    QVERIFY(git.runCustom({QStringLiteral("config"), QStringLiteral("user.email"),
                           QStringLiteral("test@example.com")}).succeeded());

    writeFile(QStringLiteral("first.txt"), QStringLiteral("one\n"));
    QVERIFY(git.stage({QStringLiteral("first.txt")}).succeeded());
    QVERIFY(git.commit(QStringLiteral("First commit"), false).succeeded());
}

void TestRepositoryView::cleanup() {
    delete directory_;
    directory_ = nullptr;
}

GitClient TestRepositoryView::repository() const {
    GitClient git;
    [[maybe_unused]] const GitCommandResult opened = git.openRepository(directory_->path());
    return git;
}

void TestRepositoryView::writeFile(const QString &name, const QString &contents) const {
    QFile file(QDir(directory_->path()).filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream stream(&file);
    stream << contents;
}

void TestRepositoryView::appliesFirstSnapshot() {
    RepositoryView view(directory_->path());
    QVERIFY(view.isValid());

    QSignalSpy refreshed(&view, &RepositoryView::repositoryChanged);
    QVERIFY(refreshed.wait(10'000));

    QCOMPARE(view.currentBranchName().isEmpty(), false);
    QCOMPARE(view.changeCount(), 0);
    QVERIFY(!view.isBusy());
}

void TestRepositoryView::refreshesAfterExternalIndexChange() {
    RepositoryView view(directory_->path());
    QVERIFY(view.isValid());

    QSignalSpy refreshed(&view, &RepositoryView::repositoryChanged);
    QVERIFY(refreshed.wait(10'000));
    QCOMPARE(view.changeCount(), 0);

    refreshed.clear();
    writeFile(QStringLiteral("second.txt"), QStringLiteral("two\n"));
    GitClient git = repository();
    QVERIFY(git.stage({QStringLiteral("second.txt")}).succeeded());

    QVERIFY2(refreshed.wait(10'000), "the view never noticed the external change");
    QCOMPARE(view.changeCount(), 1);
    QVERIFY(view.hasStagedChanges());
}

QAbstractItemModel *TestRepositoryView::historyModel(const RepositoryView &view) {
    auto *historyView = view.findChild<QTreeView *>(QStringLiteral("historyTree"));
    return historyView != nullptr ? historyView->model() : nullptr;
}

void TestRepositoryView::feedsTheHistoryViewFromTheModel() {
    RepositoryView view(directory_->path());
    QVERIFY(view.isValid());
    QSignalSpy refreshed(&view, &RepositoryView::repositoryChanged);
    QVERIFY(refreshed.wait(10'000));

    QAbstractItemModel *model = historyModel(view);
    QVERIFY(model != nullptr);
    QCOMPARE(model->columnCount(), static_cast<int>(CommitModel::ColumnCount));

    QCOMPARE(model->rowCount(), 1);
    const QModelIndex message = model->index(0, CommitModel::Message);
    QCOMPARE(message.data().toString(), QStringLiteral("First commit"));
    QCOMPARE(model->index(0, CommitModel::Author).data().toString(),
             QStringLiteral("Test <test@example.com>"));

    const QModelIndex graph = model->index(0, CommitModel::Graph);
    QVERIFY(graph.data(CommitRoles::LaneCount).toInt() >= 1);
    QVERIFY(graph.data(CommitRoles::IsHead).toBool());
    QVERIFY(!graph.data(CommitRoles::IsUncommitted).toBool());
    QVERIFY(!graph.data(CommitRoles::Hash).toString().isEmpty());

    refreshed.clear();
    writeFile(QStringLiteral("second.txt"), QStringLiteral("two\n"));
    QVERIFY(repository().stage({QStringLiteral("second.txt")}).succeeded());
    QVERIFY(refreshed.wait(10'000));

    QCOMPARE(model->rowCount(), 2);
    QVERIFY(model->index(0, CommitModel::Graph).data(CommitRoles::IsUncommitted).toBool());
    QCOMPARE(model->index(1, CommitModel::Message).data().toString(),
             QStringLiteral("First commit"));
}

QTEST_MAIN(TestRepositoryView)

#include "tst_repositoryview.moc"
