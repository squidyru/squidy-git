// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "core/gitclient.h"
#include "ui/filepreview.h"
#include "ui/filespage.h"
#include "ui/pdfpreview.h"
#include "ui/sourceview.h"
#include "ui/syntaxhighlighter.h"
#include "ui/theme.h"

#include <QBuffer>
#include <QPainter>
#include <QPdfWriter>
#include <QComboBox>
#include <QLineEdit>
#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextStream>
#include <QTreeWidget>

// FilesPage integration tests: the page reads a real repository, so the tests
// check the wiring between the tree, the timeline and the viewer.
class TestFilesPage final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void showsTheRootOfTheWorkingCopy();
    void fillsTheTimelineOfTheSelectedFile();
    void showsHistoricalContentsOfAFile();
    void opensFilesThatOnlyExistInOlderRevisions();
    void reportsTheCommitOfTheSelectedRevision();
    void keepsAnExplicitCommitWhenReferencesRefresh();

    void picksTheLanguageFromTheFileName();
    void readsQtCataloguesAsMarkupDespiteTheExtension();
    void highlightsKeywordsCommentsAndStrings();
    void keepsCommentMarkersInsideStringsAsString();
    void carriesBlockCommentsAcrossLines();
    void widensTheLineNumbersWithTheFileLength();
    void decodesPicturesAndDrawings();
    void refusesToDecodeThingsThatAreNotPictures();
    void dumpsBytesOfAFileNothingCanRender();
    void showsAPictureOfTheRepositoryInTheViewer();
    void showsPagesOfAPdfRevision();
    void keepsOnlyMatchingFilesWhileSearching();
    void restoresTheWholeTreeWhenTheSearchIsCleared();

private:
    void writeFile(const QString &name, const QString &contents) const;
    [[nodiscard]] GitClient repository() const;
    [[nodiscard]] static QTreeWidget *tree(const FilesPage &page);
    [[nodiscard]] static QTreeWidget *timeline(const FilesPage &page);
    /// Returns the item of the level currently displayed, or nullptr.
    [[nodiscard]] static QTreeWidgetItem *itemNamed(const QTreeWidget *view,
                                                    const QString &name);

    QTemporaryDir *directory_ = nullptr;
};

void TestFilesPage::initTestCase() {
    if (GitClient::gitExecutable().isEmpty()) {
        QSKIP("git is not installed, the files suite cannot run");
    }
}

void TestFilesPage::init() {
    directory_ = new QTemporaryDir;
    QVERIFY(directory_->isValid());
    QVERIFY(GitClient::initRepository(directory_->path(), false).succeeded());

    GitClient git = repository();
    QVERIFY(git.runCustom({QStringLiteral("config"), QStringLiteral("user.name"),
                           QStringLiteral("Test")}).succeeded());
    QVERIFY(git.runCustom({QStringLiteral("config"), QStringLiteral("user.email"),
                           QStringLiteral("test@example.com")}).succeeded());

    QVERIFY(QDir(directory_->path()).mkpath(QStringLiteral("src")));
    QVERIFY(QDir(directory_->path()).mkpath(QStringLiteral("docs")));

    writeFile(QStringLiteral("src/old.txt"), QStringLiteral("one\n"));
    writeFile(QStringLiteral("docs/readme.md"), QStringLiteral("documentation\n"));
    QVERIFY(git.stageAll().succeeded());
    QVERIFY(git.commit(QStringLiteral("First commit"), false).succeeded());

    QVERIFY(git.runCustom({QStringLiteral("mv"), QStringLiteral("src/old.txt"),
                           QStringLiteral("src/new.txt")}).succeeded());
    writeFile(QStringLiteral("src/new.txt"), QStringLiteral("one\ntwo\n"));
    QVERIFY(git.stageAll().succeeded());
    QVERIFY(git.commit(QStringLiteral("Rename and extend"), false).succeeded());

    QVERIFY(git.runCustom({QStringLiteral("rm"),
                           QStringLiteral("docs/readme.md")}).succeeded());
    QVERIFY(git.commit(QStringLiteral("Drop the documentation"), false).succeeded());
}

void TestFilesPage::cleanup() {
    delete directory_;
    directory_ = nullptr;
}

GitClient TestFilesPage::repository() const {
    GitClient git;
    [[maybe_unused]] const GitCommandResult opened = git.openRepository(directory_->path());
    return git;
}

void TestFilesPage::writeFile(const QString &name, const QString &contents) const {
    QFile file(QDir(directory_->path()).filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream stream(&file);
    stream << contents;
}

QTreeWidget *TestFilesPage::tree(const FilesPage &page) {
    return page.findChild<QTreeWidget *>(QStringLiteral("fileTree"));
}

QTreeWidget *TestFilesPage::timeline(const FilesPage &page) {
    return page.findChild<QTreeWidget *>(QStringLiteral("fileTimeline"));
}

QTreeWidgetItem *TestFilesPage::itemNamed(const QTreeWidget *view, const QString &name) {
    for (int index = 0; index < view->topLevelItemCount(); ++index) {
        if (view->topLevelItem(index)->text(0) == name) {
            return view->topLevelItem(index);
        }
    }
    return nullptr;
}

void TestFilesPage::showsTheRootOfTheWorkingCopy() {
    FilesPage page(directory_->path());
    QTreeWidget *view = tree(page);
    QVERIFY(view != nullptr);

    // Only tracked content is listed, so the deleted documentation is gone.
    QTRY_VERIFY_WITH_TIMEOUT(view->topLevelItemCount() == 1, 10'000);
    QCOMPARE(view->topLevelItem(0)->text(0), QStringLiteral("src"));
}

void TestFilesPage::fillsTheTimelineOfTheSelectedFile() {
    FilesPage page(directory_->path());
    page.showFile(QStringLiteral("src/new.txt"), QStringLiteral("HEAD"));

    QTreeWidget *revisions = timeline(page);
    QVERIFY(revisions != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(revisions->topLevelItemCount() == 2, 10'000);

    // The rename is reported as such, and the file is selected in the tree.
    QCOMPARE(revisions->topLevelItem(0)->text(2), QStringLiteral("Renamed"));
    QCOMPARE(revisions->topLevelItem(1)->text(2), QStringLiteral("Added"));
    QVERIFY(tree(page)->currentItem() != nullptr);
    QCOMPARE(tree(page)->currentItem()->text(0), QStringLiteral("new.txt"));
}

void TestFilesPage::showsHistoricalContentsOfAFile() {
    FilesPage page(directory_->path());
    page.showFile(QStringLiteral("src/new.txt"), QStringLiteral("HEAD"));

    QTreeWidget *revisions = timeline(page);
    QTRY_VERIFY_WITH_TIMEOUT(revisions->topLevelItemCount() == 2, 10'000);

    auto *mode = page.findChild<QComboBox *>(QStringLiteral("fileViewerMode"));
    QVERIFY(mode != nullptr);
    mode->setCurrentIndex(1);

    // The oldest revision still carries the former path of the file.
    revisions->setCurrentItem(revisions->topLevelItem(1));

    auto *contents = page.findChild<QPlainTextEdit *>(QStringLiteral("fileContents"));
    QVERIFY(contents != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(contents->toPlainText(), QStringLiteral("one\n"), 10'000);

    // Reading history never touches the working tree.
    QCOMPARE(repository().status().size(), 0);
}

void TestFilesPage::opensFilesThatOnlyExistInOlderRevisions() {
    FilesPage page(directory_->path());
    page.showFile(QStringLiteral("docs/readme.md"), QStringLiteral("HEAD~1"));

    QTreeWidget *view = tree(page);
    QTRY_VERIFY_WITH_TIMEOUT(itemNamed(view, QStringLiteral("docs")) != nullptr, 10'000);

    QTreeWidget *revisions = timeline(page);
    QTRY_VERIFY_WITH_TIMEOUT(revisions->topLevelItemCount() == 1, 10'000);
    QCOMPARE(revisions->topLevelItem(0)->text(2), QStringLiteral("Added"));
}

void TestFilesPage::reportsTheCommitOfTheSelectedRevision() {
    FilesPage page(directory_->path());
    page.showFile(QStringLiteral("src/new.txt"), QStringLiteral("HEAD"));

    QTreeWidget *revisions = timeline(page);
    QTRY_VERIFY_WITH_TIMEOUT(revisions->topLevelItemCount() == 2, 10'000);

    QSignalSpy activated(&page, &FilesPage::commitActivated);
    Q_EMIT revisions->itemDoubleClicked(revisions->topLevelItem(0), 0);

    // The newest revision of the file is the rename, not the tip of the branch.
    const GitCommandResult rename = repository().runCustom({
        QStringLiteral("rev-parse"), QStringLiteral("HEAD~1")
    });
    QVERIFY(rename.succeeded());

    QCOMPARE(activated.size(), 1);
    QCOMPARE(activated.constFirst().constFirst().toString(), rename.outputText().trimmed());
}

void TestFilesPage::keepsAnExplicitCommitWhenReferencesRefresh() {
    FilesPage page(directory_->path());
    const QString hash = repository().headHash();
    QVERIFY(!hash.isEmpty());

    page.showFile(QStringLiteral("src/new.txt"), hash);
    page.setReferences({}, {});

    auto *selector = page.findChild<QComboBox *>(QStringLiteral("fileRevision"));
    QVERIFY(selector != nullptr);
    QCOMPARE(selector->currentData().toString(), hash);
}

namespace {

/// Runs the highlighter over a snippet and reports the colour of one character.
QColor colourAt(const QString &fileName, const QString &contents, const int line,
                const int column) {
    QTextDocument document;
    SyntaxHighlighter highlighter(&document);
    highlighter.showFile(fileName, contents);

    const QTextBlock block = document.findBlockByNumber(line);
    for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
        if (column >= range.start && column < range.start + range.length) {
            return range.format.foreground().color();
        }
    }
    return {};
}

}

void TestFilesPage::picksTheLanguageFromTheFileName() {
    QCOMPARE(SyntaxHighlighter::languageForFile(QStringLiteral("ui/theme.cpp")),
             SyntaxLanguage::Cpp);
    QCOMPARE(SyntaxHighlighter::languageForFile(QStringLiteral("setup.py")),
             SyntaxLanguage::Python);
    QCOMPARE(SyntaxHighlighter::languageForFile(QStringLiteral("CMakeLists.txt")),
             SyntaxLanguage::CMake);
    QCOMPARE(SyntaxHighlighter::languageForFile(QStringLiteral("README.md")),
             SyntaxLanguage::Markdown);
    QCOMPARE(SyntaxHighlighter::languageForFile(QStringLiteral("LICENSE")),
             SyntaxLanguage::None);
}

void TestFilesPage::readsQtCataloguesAsMarkupDespiteTheExtension() {
    QTextDocument document;
    SyntaxHighlighter highlighter(&document);

    highlighter.showFile(QStringLiteral("app.ts"), QStringLiteral("const value = 1;\n"));
    QCOMPARE(highlighter.language(), SyntaxLanguage::TypeScript);

    highlighter.showFile(QStringLiteral("translations/squidygit_ru.ts"),
                         QStringLiteral("<?xml version=\"1.0\"?>\n<TS version=\"2.1\">\n"));
    QCOMPARE(highlighter.language(), SyntaxLanguage::Markup);
}

void TestFilesPage::highlightsKeywordsCommentsAndStrings() {
    const ThemePalette &palette = Theme::instance()->palette();
    const QString source = QStringLiteral("// a comment\n"
                                          "int value = 42;\n"
                                          "const char *text = \"hello\";\n");

    QCOMPARE(colourAt(QStringLiteral("main.cpp"), source, 0, 3), palette.syntaxComment);
    QCOMPARE(colourAt(QStringLiteral("main.cpp"), source, 1, 0), palette.syntaxType);
    QCOMPARE(colourAt(QStringLiteral("main.cpp"), source, 1, 12), palette.syntaxNumber);
    QCOMPARE(colourAt(QStringLiteral("main.cpp"), source, 2, 0), palette.syntaxKeyword);
    QCOMPARE(colourAt(QStringLiteral("main.cpp"), source, 2, 20), palette.syntaxString);
}

void TestFilesPage::keepsCommentMarkersInsideStringsAsString() {
    // The scanner reads a line once, so a marker inside a string stays a string.
    const QString source = QStringLiteral("QString url = \"https://squidy.ru\";\n");
    const QColor colour = colourAt(QStringLiteral("main.cpp"), source, 0, 23);

    QCOMPARE(colour, Theme::instance()->palette().syntaxString);
    QVERIFY(colour != Theme::instance()->palette().syntaxComment);
}

void TestFilesPage::carriesBlockCommentsAcrossLines() {
    const QString source = QStringLiteral("/* opening\n"
                                          "   still inside\n"
                                          "*/ int after = 1;\n");

    QCOMPARE(colourAt(QStringLiteral("main.cpp"), source, 1, 5),
             Theme::instance()->palette().syntaxComment);
    QCOMPARE(colourAt(QStringLiteral("main.cpp"), source, 2, 3),
             Theme::instance()->palette().syntaxType);
}

void TestFilesPage::widensTheLineNumbersWithTheFileLength() {
    SourceView view;
    view.setPlainText(QStringLiteral("one\ntwo\n"));
    const int narrow = view.gutterWidth();

    QStringList lines;
    for (int index = 0; index < 500; ++index) {
        lines.append(QStringLiteral("line"));
    }
    view.setPlainText(lines.join(u'\n'));

    QVERIFY(narrow > 0);
    QVERIFY2(view.gutterWidth() > narrow, "three digits need more room than two");
}

void TestFilesPage::decodesPicturesAndDrawings() {
    QImage source(24, 12, QImage::Format_ARGB32);
    source.fill(Qt::red);

    QByteArray encoded;
    QBuffer buffer(&encoded);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(source.save(&buffer, "PNG"));

    QString format;
    const QImage decoded = FilePreview::decodeImage(encoded, &format);
    QCOMPARE(format, QStringLiteral("PNG"));
    QCOMPARE(decoded.size(), QSize(24, 12));

    // A drawing carries no pixels, so it is rasterised large enough to read.
    const QByteArray drawing =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"4\">"
        "<rect width=\"8\" height=\"4\" fill=\"red\"/></svg>";
    QString drawingFormat;
    const QImage rendered = FilePreview::decodeImage(drawing, &drawingFormat);
    if (rendered.isNull()) {
        QSKIP("the SVG image plugin is not installed");
    }
    QCOMPARE(drawingFormat, QStringLiteral("SVG"));
    QVERIFY2(rendered.width() > 8, "a drawing is rendered above its nominal size");
    QCOMPARE(rendered.width() / rendered.height(), 2);
}

void TestFilesPage::refusesToDecodeThingsThatAreNotPictures() {
    QVERIFY(FilePreview::decodeImage(QByteArray()).isNull());
    QVERIFY(FilePreview::decodeImage(QByteArray("int main() { return 0; }")).isNull());
}

void TestFilesPage::dumpsBytesOfAFileNothingCanRender() {
    QByteArray content("\x7f", 1);
    content.append("ELF text", 8);

    const QString dump = FilePreview::hexDump(content);
    const QStringList lines = dump.split(u'\n', Qt::SkipEmptyParts);

    QCOMPARE(lines.size(), 1);
    QVERIFY(lines.constFirst().startsWith(QStringLiteral("00000000  7f 45 4c 46")));
    // Unprintable bytes become dots, the rest stays readable.
    QVERIFY(lines.constFirst().endsWith(QStringLiteral(".ELF text")));

    QVERIFY(FilePreview::hexDump(content, 2).split(u'\n', Qt::SkipEmptyParts)
                .constFirst().contains(QStringLiteral("7f 45")));
}

void TestFilesPage::showsAPictureOfTheRepositoryInTheViewer() {
    QImage picture(6, 6, QImage::Format_ARGB32);
    picture.fill(Qt::blue);
    QVERIFY(picture.save(QDir(directory_->path()).filePath(QStringLiteral("logo.png"))));

    GitClient git = repository();
    QVERIFY(git.stageAll().succeeded());
    QVERIFY(git.commit(QStringLiteral("Add a logo"), false).succeeded());

    FilesPage page(directory_->path());
    page.showFile(QStringLiteral("logo.png"), QStringLiteral("HEAD"));

    QTreeWidget *revisions = timeline(page);
    QTRY_VERIFY_WITH_TIMEOUT(revisions->topLevelItemCount() == 1, 10'000);

    auto *mode = page.findChild<QComboBox *>(QStringLiteral("fileViewerMode"));
    QVERIFY(mode != nullptr);
    mode->setCurrentIndex(1);

    // The picture widget takes over the viewer instead of a "binary" notice.
    auto *stack = page.findChild<QStackedWidget *>();
    QVERIFY(stack != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(stack->currentIndex() == 2, 10'000);
}

void TestFilesPage::showsPagesOfAPdfRevision() {
    if (!PdfPreview::isSupported()) {
        QSKIP("this build has no Qt PDF module");
    }

    // A real two-page document, written by Qt itself.
    const QString path = QDir(directory_->path()).filePath(QStringLiteral("manual.pdf"));
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A5));
        QPainter painter(&writer);
        painter.drawText(100, 100, QStringLiteral("first page"));
        QVERIFY(writer.newPage());
        painter.drawText(100, 100, QStringLiteral("second page"));
    }

    GitClient git = repository();
    QVERIFY(git.stageAll().succeeded());
    QVERIFY(git.commit(QStringLiteral("Add the manual"), false).succeeded());

    FilesPage page(directory_->path());
    page.showFile(QStringLiteral("manual.pdf"), QStringLiteral("HEAD"));

    QTreeWidget *revisions = timeline(page);
    QTRY_VERIFY_WITH_TIMEOUT(revisions->topLevelItemCount() == 1, 10'000);

    auto *mode = page.findChild<QComboBox *>(QStringLiteral("fileViewerMode"));
    QVERIFY(mode != nullptr);
    mode->setCurrentIndex(1);

    auto *preview = page.findChild<PdfPreview *>();
    QVERIFY(preview != nullptr);
    auto *stack = page.findChild<QStackedWidget *>();
    QTRY_VERIFY_WITH_TIMEOUT(stack->currentWidget() == preview, 10'000);
}

void TestFilesPage::keepsOnlyMatchingFilesWhileSearching() {
    FilesPage page(directory_->path());
    QTreeWidget *view = tree(page);
    QTRY_VERIFY_WITH_TIMEOUT(view->topLevelItemCount() == 1, 10'000);

    auto *filter = page.findChild<QLineEdit *>(QStringLiteral("fileNameFilter"));
    QVERIFY(filter != nullptr);
    filter->setText(QStringLiteral("new"));

    // "src/new.txt" is the only match, and the folder holding it is opened.
    // An unfiltered folder carries a nameless placeholder child instead, which
    // is why the name is what the wait looks at.
    QTRY_VERIFY_WITH_TIMEOUT(view->topLevelItemCount() == 1
                                 && view->topLevelItem(0)->childCount() == 1
                                 && view->topLevelItem(0)->child(0)->text(0)
                                        == QStringLiteral("new.txt"),
                             10'000);
    QTreeWidgetItem *folder = view->topLevelItem(0);
    QCOMPARE(folder->text(0), QStringLiteral("src"));
    QVERIFY2(folder->isExpanded(), "the folder of a match opens by itself");

    // A name nothing carries empties the tree rather than leaving it stale.
    filter->setText(QStringLiteral("nothing-is-named-this"));
    QTRY_VERIFY_WITH_TIMEOUT(view->topLevelItemCount() == 0, 10'000);
}

void TestFilesPage::restoresTheWholeTreeWhenTheSearchIsCleared() {
    FilesPage page(directory_->path());
    QTreeWidget *view = tree(page);
    QTRY_VERIFY_WITH_TIMEOUT(view->topLevelItemCount() == 1, 10'000);

    auto *filter = page.findChild<QLineEdit *>(QStringLiteral("fileNameFilter"));
    filter->setText(QStringLiteral("new"));
    QTRY_VERIFY_WITH_TIMEOUT(view->topLevelItemCount() == 1
                                 && view->topLevelItem(0)->childCount() == 1
                                 && view->topLevelItem(0)->child(0)->text(0)
                                        == QStringLiteral("new.txt"),
                             10'000);

    filter->clear();
    // The plain tree is lazy again: the level below the root stays unread.
    QTRY_VERIFY_WITH_TIMEOUT(view->topLevelItemCount() == 1
                                 && !view->topLevelItem(0)->isExpanded(), 10'000);
    QCOMPARE(view->topLevelItem(0)->text(0), QStringLiteral("src"));
}

QTEST_MAIN(TestFilesPage)

#include "tst_filespage.moc"
