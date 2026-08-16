// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "core/gitclient.h"
#include "ui/commitgraph.h"
#include "ui/commitmodel.h"
#include "ui/diffview.h"
#include "ui/repositoryview.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTreeView>
#include <QTreeWidget>

#include <cstdio>

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
    void countsIncomingCommitsWithoutUserAction();
    void checksEveryOpenTabAndNotJustTheActiveOne();
    void keepsTheInterfaceLiveWhileAnOperationRuns();
    void refusesToStartASecondOperation();
    void keepsEveryPaneReachable();
    void readsTheWorkingTreeDiffOffTheUiThread();
    void dropsADiffTheSelectionHasMovedPast();
    void listsCommitFilesWithoutBlocking();

private:
    void writeFile(const QString &name, const QString &contents) const;
    [[nodiscard]] GitClient repository() const;
    [[nodiscard]] static QAbstractItemModel *historyModel(const RepositoryView &view);
    static void createRepository(const QString &directory);
    /// Leaves the remote one commit ahead without updating its tracking ref.
    static void putRemoteAhead(const QString &directory, const QString &remoteDirectory);

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

namespace {

void writeFileAt(const QString &directory, const QString &name, const QString &contents) {
    QFile file(QDir(directory).filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream stream(&file);
    stream << contents;
}

}

void TestRepositoryView::writeFile(const QString &name, const QString &contents) const {
    writeFileAt(directory_->path(), name, contents);
}

void TestRepositoryView::createRepository(const QString &directory) {
    QVERIFY(GitClient::initRepository(directory, false).succeeded());

    GitClient git;
    QVERIFY(git.openRepository(directory).succeeded());
    QVERIFY(git.runCustom({QStringLiteral("config"), QStringLiteral("user.name"),
                           QStringLiteral("Test")}).succeeded());
    QVERIFY(git.runCustom({QStringLiteral("config"), QStringLiteral("user.email"),
                           QStringLiteral("test@example.com")}).succeeded());

    writeFileAt(directory, QStringLiteral("first.txt"), QStringLiteral("one\n"));
    QVERIFY(git.stage({QStringLiteral("first.txt")}).succeeded());
    QVERIFY(git.commit(QStringLiteral("First commit"), false).succeeded());
}

void TestRepositoryView::putRemoteAhead(const QString &directory,
                                        const QString &remoteDirectory) {
    QVERIFY(GitClient::initRepository(remoteDirectory, true).succeeded());

    GitClient git;
    QVERIFY(git.openRepository(directory).succeeded());
    const QString branch = git.currentBranch();
    QVERIFY(!branch.isEmpty());
    QVERIFY(git.addRemote(QStringLiteral("origin"), remoteDirectory).succeeded());
    QVERIFY(git.push(QStringLiteral("origin"), {branch}, true, false, false).succeeded());

    const QString shared = git.headHash();
    QVERIFY(!shared.isEmpty());

    writeFileAt(directory, QStringLiteral("second.txt"), QStringLiteral("two\n"));
    QVERIFY(git.stage({QStringLiteral("second.txt")}).succeeded());
    QVERIFY(git.commit(QStringLiteral("Second commit"), false).succeeded());
    QVERIFY(git.push(QStringLiteral("origin"), {branch}, false, false, false).succeeded());
    QVERIFY(git.reset(shared, GitResetMode::Hard).succeeded());
    QVERIFY(git.runCustom({QStringLiteral("update-ref"),
                           QStringLiteral("refs/remotes/origin/%1").arg(branch),
                           shared}).succeeded());
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

void TestRepositoryView::countsIncomingCommitsWithoutUserAction() {
    QTemporaryDir remote;
    QVERIFY(remote.isValid());
    putRemoteAhead(directory_->path(), remote.path());
    QVERIFY(!QTest::currentTestFailed());

    RepositoryView view(directory_->path());
    QVERIFY(view.isValid());

    QSignalSpy refreshed(&view, &RepositoryView::repositoryChanged);
    QVERIFY(refreshed.wait(10'000));
    QCOMPARE(view.behindCount(), 0);

    QTRY_VERIFY_WITH_TIMEOUT(view.behindCount() == 1, 30'000);
}

void TestRepositoryView::checksEveryOpenTabAndNotJustTheActiveOne() {
    QTemporaryDir secondWork;
    QTemporaryDir firstRemote;
    QTemporaryDir secondRemote;
    QVERIFY(secondWork.isValid() && firstRemote.isValid() && secondRemote.isValid());

    createRepository(secondWork.path());
    QVERIFY(!QTest::currentTestFailed());
    putRemoteAhead(directory_->path(), firstRemote.path());
    QVERIFY(!QTest::currentTestFailed());
    putRemoteAhead(secondWork.path(), secondRemote.path());
    QVERIFY(!QTest::currentTestFailed());

    RepositoryView first(directory_->path());
    RepositoryView second(secondWork.path());
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());

    QTRY_VERIFY_WITH_TIMEOUT(first.behindCount() == 1 && second.behindCount() == 1, 60'000);
}

// Operations run on a worker. One running inline again would stop the event
// loop and with it the busy state this waits for.
void TestRepositoryView::keepsTheInterfaceLiveWhileAnOperationRuns() {
    QTemporaryDir remote;
    QVERIFY(remote.isValid());
    putRemoteAhead(directory_->path(), remote.path());
    QVERIFY(!QTest::currentTestFailed());

    RepositoryView view(directory_->path());
    QVERIFY(view.isValid());
    QSignalSpy refreshed(&view, &RepositoryView::repositoryChanged);
    QVERIFY(refreshed.wait(10'000));

    QSignalSpy busy(&view, &RepositoryView::busyChanged);
    view.startFetch();

    // Announced before the command finishes, which is only observable while
    // the event loop still turns.
    QCOMPARE(busy.count(), 1);
    QVERIFY(busy.constFirst().constFirst().toBool());
    QVERIFY(view.isBusy());

    QTRY_VERIFY_WITH_TIMEOUT(!view.isBusy(), 30'000);
    QCOMPARE(busy.count(), 2);
    QVERIFY(!busy.constLast().constFirst().toBool());
}

// Two commands over one repository would collide over the index lock.
void TestRepositoryView::refusesToStartASecondOperation() {
    QTemporaryDir remote;
    QVERIFY(remote.isValid());
    putRemoteAhead(directory_->path(), remote.path());
    QVERIFY(!QTest::currentTestFailed());

    RepositoryView view(directory_->path());
    QVERIFY(view.isValid());
    QSignalSpy refreshed(&view, &RepositoryView::repositoryChanged);
    QVERIFY(refreshed.wait(10'000));

    QSignalSpy busy(&view, &RepositoryView::busyChanged);
    view.startFetch();
    view.startFetch();

    // The second request is turned away rather than queued behind the first.
    QCOMPARE(busy.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!view.isBusy(), 30'000);
    QCOMPARE(busy.count(), 2);
}

namespace {

// DiffView names itself and the theme keys its padding off that name, so the
// panel around it carries the name looked up here.
DiffView *workingTreeDiff(const RepositoryView &view) {
    auto *panel = view.findChild<QWidget *>(QStringLiteral("workingTreeDiffPanel"));
    return panel != nullptr ? panel->findChild<DiffView *>() : nullptr;
}

// Selects the row named @p path in the unstaged file list.
bool selectUnstagedFile(const RepositoryView &view, const QString &path) {
    auto *tree = view.findChild<QTreeWidget *>(QStringLiteral("unstagedTree"));
    if (tree == nullptr) {
        return false;
    }
    for (int row = 0; row < tree->topLevelItemCount(); ++row) {
        QTreeWidgetItem *item = tree->topLevelItem(row);
        if (item->text(0).contains(path)) {
            tree->setCurrentItem(item);
            item->setSelected(true);
            return true;
        }
    }
    return false;
}

}

// A pane shut against a zero width handle can never be pulled back. Either
// half alone is fine; only the pair is a fault.
void TestRepositoryView::keepsEveryPaneReachable() {
    RepositoryView view(directory_->path());
    QVERIFY(view.isValid());
    QSignalSpy refreshed(&view, &RepositoryView::repositoryChanged);
    QVERIFY(refreshed.wait(10'000));

    const QList<QSplitter *> splitters = view.findChildren<QSplitter *>();
    QVERIFY2(splitters.size() >= 5, "the pages should be built by now");

    for (const QSplitter *splitter : splitters) {
        const bool reachable = !splitter->childrenCollapsible()
                               || splitter->handleWidth() > 0;
        const QString name = splitter->objectName().isEmpty()
                                 ? QStringLiteral("<unnamed>")
                                 : splitter->objectName();
        QVERIFY2(reachable,
                 qPrintable(QStringLiteral("%1 can hide a pane with no way back")
                                .arg(name)));
    }
}

// Read on a worker, so it cannot be on screen when the handler returns.
void TestRepositoryView::readsTheWorkingTreeDiffOffTheUiThread() {
    writeFile(QStringLiteral("first.txt"), QStringLiteral("one\nsecond line\n"));

    RepositoryView view(directory_->path());
    QVERIFY(view.isValid());
    QSignalSpy refreshed(&view, &RepositoryView::repositoryChanged);
    QVERIFY(refreshed.wait(10'000));

    DiffView *diff = workingTreeDiff(view);
    QVERIFY(diff != nullptr);
    QVERIFY(selectUnstagedFile(view, QStringLiteral("first.txt")));

    QVERIFY2(!diff->hasPatch(), "the diff was read inline instead of on a worker");
    QTRY_VERIFY_WITH_TIMEOUT(diff->hasPatch(), 15'000);
    QVERIFY(diff->toPlainText().contains(QStringLiteral("second line")));
}

// Selections that follow one another leave the pane on the last one, with
// nothing of the previous file left behind. The reads are let to land in turn:
// overlapping them is what the application now avoids by cancelling, and
// racing them here measures the machine more than the behaviour.
void TestRepositoryView::dropsADiffTheSelectionHasMovedPast() {
    writeFile(QStringLiteral("alpha.txt"), QStringLiteral("alpha contents\n"));
    writeFile(QStringLiteral("beta.txt"), QStringLiteral("beta contents\n"));

    RepositoryView view(directory_->path());
    QVERIFY(view.isValid());
    QSignalSpy refreshed(&view, &RepositoryView::repositoryChanged);
    QVERIFY(refreshed.wait(10'000));

    DiffView *diff = workingTreeDiff(view);
    QVERIFY(diff != nullptr);

    QVERIFY(selectUnstagedFile(view, QStringLiteral("alpha.txt")));
    QTRY_VERIFY_WITH_TIMEOUT(diff->toPlainText().contains(QStringLiteral("alpha contents")),
                             15'000);

    QVERIFY(selectUnstagedFile(view, QStringLiteral("beta.txt")));
    QTRY_VERIFY_WITH_TIMEOUT(diff->toPlainText().contains(QStringLiteral("beta contents")),
                             15'000);
    QVERIFY2(!diff->toPlainText().contains(QStringLiteral("alpha contents")),
             "the pane still holds the file the selection left");
}

void TestRepositoryView::listsCommitFilesWithoutBlocking() {
    RepositoryView view(directory_->path());
    QVERIFY(view.isValid());
    QSignalSpy refreshed(&view, &RepositoryView::repositoryChanged);
    QVERIFY(refreshed.wait(10'000));

    auto *files = view.findChild<QTreeWidget *>(QStringLiteral("commitFilesTree"));
    QVERIFY(files != nullptr);

    // The first commit is selected as the history arrives; its file list is
    // filled by the worker rather than during the selection handler.
    QTRY_VERIFY_WITH_TIMEOUT(files->topLevelItemCount() == 1, 15'000);
    QCOMPARE(files->topLevelItem(0)->text(0), QStringLiteral("first.txt"));
}

// Not QTEST_MAIN: the plain text logger buffers its output, and a run that
// ends abnormally takes the whole buffer with it, leaving nothing to read.
// Unbuffered, whatever was reached is on the record.
int main(int argc, char *argv[]) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication application(argc, argv);
    TestRepositoryView test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_repositoryview.moc"
