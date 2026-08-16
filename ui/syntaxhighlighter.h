// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QSet>
#include <QString>
#include <QSyntaxHighlighter>

class QTextDocument;

/// The rule sets the viewer can apply. Every language is described by the same
/// small set of traits, so a family shares one scanner and differs only in its
/// words and comment markers.
enum class SyntaxLanguage {
    None,
    C,
    Cpp,
    CSharp,
    Java,
    Kotlin,
    Swift,
    JavaScript,
    TypeScript,
    Python,
    Go,
    Rust,
    Ruby,
    Php,
    Lua,
    Perl,
    Shell,
    PowerShell,
    CMake,
    Make,
    Sql,
    Json,
    Yaml,
    Ini,
    Css,
    Markup,
    Markdown
};

/// Highlights the read-only source viewer. The scanner walks each line once, so
/// a comment marker inside a string stays a string.
class SyntaxHighlighter final : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit SyntaxHighlighter(QTextDocument *document);

    /// Loads @p contents into the document and colours it with the rules of
    /// @p fileName; an empty name leaves the text plain. Text and rules are set
    /// in one call because the highlighter has to repaint after either changes.
    /// The first characters of the contents disambiguate shared extensions.
    void showFile(const QString &fileName, const QString &contents);
    [[nodiscard]] SyntaxLanguage language() const;

    /// Chooses a language without looking at any contents.
    [[nodiscard]] static SyntaxLanguage languageForFile(const QString &fileName);

protected:
    void highlightBlock(const QString &text) override;

private:
    enum BlockState {
        NormalState = 0,
        BlockCommentState,
        SingleQuoteBlockState,
        DoubleQuoteBlockState,
        FenceState
    };

    struct Traits {
        QSet<QString> keywords;
        QSet<QString> types;
        QStringList lineComments;
        QString blockCommentStart;
        QString blockCommentEnd;
        /// Python style ''' and """ spans.
        bool tripleQuotes = false;
        bool singleQuoteStrings = true;
        bool backtickStrings = false;
        /// C style "#include" and "#define" lines.
        bool preprocessor = false;
        /// "key: value" and "key = value" lines of configuration formats.
        bool keyValue = false;
    };

    void applyTraits();
    void highlightCode(const QString &text);
    void highlightMarkup(const QString &text);
    void highlightMarkdown(const QString &text);
    /// Colours a word and reports how far the scanner has to jump.
    [[nodiscard]] int highlightWord(const QString &text, int start);
    [[nodiscard]] int highlightNumber(const QString &text, int start);
    /// Returns the position after the closing quote, or the end of the line.
    [[nodiscard]] int highlightString(const QString &text, int start, QChar quote);

    SyntaxLanguage language_ = SyntaxLanguage::None;
    Traits traits_;
};
