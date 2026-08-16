// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "syntaxhighlighter.h"

#include "theme.h"

#include <QHash>
#include <QStringList>
#include <QStringView>
#include <QTextDocument>

namespace {

QSet<QString> words(const char *list) {
    const QStringList parts = QString::fromLatin1(list).split(u' ', Qt::SkipEmptyParts);
    return {parts.begin(), parts.end()};
}

QSet<QString> merged(QSet<QString> first, const QSet<QString> &second) {
    first.unite(second);
    return first;
}

bool isWordCharacter(const QChar character) {
    return character.isLetterOrNumber() || character == u'_' || character == u'$';
}

QSet<QString> cKeywords() {
    return words("auto break case const continue default do else enum extern for goto if "
                 "inline register restrict return sizeof static struct switch typedef union "
                 "volatile while");
}

QSet<QString> cTypes() {
    return words("bool char double float int long short signed unsigned void size_t "
                 "ssize_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t "
                 "NULL true false");
}

QSet<QString> cppKeywords() {
    return merged(cKeywords(),
                  words("alignas alignof and catch class co_await co_return co_yield concept "
                        "consteval constexpr constinit const_cast decltype delete "
                        "dynamic_cast explicit export friend mutable namespace new noexcept "
                        "not operator or private protected public reinterpret_cast requires "
                        "static_assert static_cast template this thread_local throw try "
                        "typeid typename using virtual xor"));
}

QSet<QString> cppTypes() {
    return merged(cTypes(),
                  words("auto char8_t char16_t char32_t nullptr wchar_t qint32 qint64 "
                        "quint32 quint64 qreal qsizetype"));
}

QSet<QString> javaKeywords() {
    return words("abstract assert break case catch class continue default do else enum "
                 "extends final finally for if implements import instanceof interface native "
                 "new package private protected public return static strictfp super switch "
                 "synchronized this throw throws transient try volatile while var record "
                 "sealed yield");
}

QSet<QString> csharpKeywords() {
    return merged(javaKeywords(),
                  words("as async await base checked delegate event explicit fixed foreach "
                        "get in internal is lock namespace operator out override params "
                        "readonly ref sealed set sizeof stackalloc struct typeof unchecked "
                        "unsafe using virtual where"));
}

QSet<QString> javaTypes() {
    return words("boolean byte char double float int long short void String Object List Map "
                 "Set true false null");
}

QSet<QString> javaScriptKeywords() {
    return words("async await break case catch class const continue debugger default delete "
                 "do else export extends finally for from function get if import in "
                 "instanceof let new of return set static super switch this throw try typeof "
                 "var void while with yield");
}

QSet<QString> javaScriptTypes() {
    return words("Array Boolean Date Error JSON Map Math Number Object Promise RegExp Set "
                 "String Symbol console document undefined null true false NaN Infinity");
}

QSet<QString> typeScriptKeywords() {
    return merged(javaScriptKeywords(),
                  words("abstract as declare enum implements interface is keyof namespace "
                        "private protected public readonly satisfies type"));
}

QSet<QString> typeScriptTypes() {
    return merged(javaScriptTypes(),
                  words("any bigint boolean never number string unknown void"));
}

QSet<QString> pythonKeywords() {
    return words("and as assert async await break class continue def del elif else except "
                 "finally for from global if import in is lambda match nonlocal not or pass "
                 "raise return try while with yield");
}

QSet<QString> pythonTypes() {
    return words("bool bytes dict float frozenset int list object set str tuple type self "
                 "cls None True False");
}

QSet<QString> goKeywords() {
    return words("break case chan const continue default defer else fallthrough for func go "
                 "goto if import interface map package range return select struct switch type "
                 "var");
}

QSet<QString> goTypes() {
    return words("bool byte complex64 complex128 error float32 float64 int int8 int16 int32 "
                 "int64 rune string uint uint8 uint16 uint32 uint64 uintptr nil true false "
                 "make new len cap append");
}

QSet<QString> rustKeywords() {
    return words("as async await break const continue crate dyn else enum extern fn for "
                 "if impl in let loop match mod move mut pub ref return self Self static "
                 "struct super trait type unsafe use where while");
}

QSet<QString> rustTypes() {
    return words("bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize "
                 "String Vec Option Result Box None Some Ok Err true false");
}

QSet<QString> rubyKeywords() {
    return words("alias and begin break case class def defined? do else elsif end ensure for "
                 "if in module next not or redo rescue retry return self super then unless "
                 "until when while yield require require_relative attr_accessor");
}

QSet<QString> phpKeywords() {
    return words("abstract and array as break callable case catch class clone const continue "
                 "declare default do echo else elseif empty enddeclare endfor endforeach "
                 "endif endswitch endwhile enum extends final finally fn for foreach function "
                 "global goto if implements include include_once instanceof insteadof "
                 "interface isset list match namespace new or print private protected public "
                 "readonly require require_once return static switch throw trait try unset "
                 "use var while xor yield");
}

QSet<QString> shellKeywords() {
    return words("if then else elif fi for while until do done case esac function in select "
                 "time return break continue local export readonly declare source alias set "
                 "unset trap exit");
}

QSet<QString> shellTypes() {
    return words("echo cd printf read test cat grep sed awk cut sort uniq head tail find "
                 "xargs mkdir rm cp mv chmod chown git make cmake sudo apt npm python pip");
}

QSet<QString> powerShellKeywords() {
    return words("begin break catch class continue data define do dynamicparam else elseif "
                 "end enum exit filter finally for foreach from function if in inlinescript "
                 "param process return switch throw trap try until using var while");
}

QSet<QString> cmakeKeywords() {
    return words("if elseif else endif foreach endforeach while endwhile function endfunction "
                 "macro endmacro break return include set unset list string file find_package "
                 "add_executable add_library add_subdirectory target_link_libraries "
                 "target_include_directories target_compile_options target_compile_definitions "
                 "install option project cmake_minimum_required enable_testing add_test "
                 "set_target_properties set_tests_properties message configure_file "
                 "qt_add_executable qt_add_resources qt_add_translations");
}

QSet<QString> sqlKeywords() {
    return words("ADD ALL ALTER AND AS ASC BEGIN BETWEEN BY CASE COMMIT CONSTRAINT CREATE "
                 "DELETE DESC DISTINCT DROP ELSE END EXISTS FOREIGN FROM FULL GROUP HAVING IN "
                 "INDEX INNER INSERT INTO IS JOIN KEY LEFT LIKE LIMIT NOT NULL OFFSET ON OR "
                 "ORDER OUTER PRIMARY REFERENCES RIGHT ROLLBACK SELECT SET TABLE THEN "
                 "TRANSACTION UNION UNIQUE UPDATE VALUES VIEW WHEN WHERE WITH "
                 "add all alter and as asc begin between by case commit constraint create "
                 "delete desc distinct drop else end exists foreign from full group having in "
                 "index inner insert into is join key left like limit not null offset on or "
                 "order outer primary references right rollback select set table then "
                 "transaction union unique update values view when where with");
}

QSet<QString> luaKeywords() {
    return words("and break do else elseif end false for function goto if in local nil not or "
                 "repeat return then true until while");
}

QSet<QString> perlKeywords() {
    return words("my our local sub package use require if elsif else unless while until for "
                 "foreach do last next redo return bless ref defined undef eval die warn print "
                 "printf push pop shift unshift keys values exists delete");
}

QSet<QString> swiftKeywords() {
    return words("associatedtype async await break case catch class continue default defer "
                 "deinit do else enum extension fallthrough fileprivate final for func guard "
                 "if import in init inout internal is lazy let mutating nonmutating open "
                 "operator private protocol public repeat rethrows return self static struct "
                 "subscript super switch throw throws try typealias var weak where while");
}

QSet<QString> kotlinKeywords() {
    return words("abstract actual annotation as break by catch class companion const "
                 "constructor continue crossinline data delegate do dynamic else enum expect "
                 "external field file final finally for fun get if import in infix init inline "
                 "inner interface internal is lateinit noinline object open operator out "
                 "override package param private property protected public receiver reified "
                 "return sealed set setparam super suspend tailrec this throw try typealias "
                 "typeof val var vararg when where while");
}

}

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {
}

SyntaxLanguage SyntaxHighlighter::language() const {
    return language_;
}

SyntaxLanguage SyntaxHighlighter::languageForFile(const QString &fileName) {
    static const QHash<QString, SyntaxLanguage> byExtension = {
        {QStringLiteral("c"), SyntaxLanguage::C},
        {QStringLiteral("h"), SyntaxLanguage::Cpp},
        {QStringLiteral("cc"), SyntaxLanguage::Cpp},
        {QStringLiteral("cpp"), SyntaxLanguage::Cpp},
        {QStringLiteral("cxx"), SyntaxLanguage::Cpp},
        {QStringLiteral("hpp"), SyntaxLanguage::Cpp},
        {QStringLiteral("hh"), SyntaxLanguage::Cpp},
        {QStringLiteral("inl"), SyntaxLanguage::Cpp},
        {QStringLiteral("m"), SyntaxLanguage::C},
        {QStringLiteral("mm"), SyntaxLanguage::Cpp},
        {QStringLiteral("cs"), SyntaxLanguage::CSharp},
        {QStringLiteral("java"), SyntaxLanguage::Java},
        {QStringLiteral("kt"), SyntaxLanguage::Kotlin},
        {QStringLiteral("kts"), SyntaxLanguage::Kotlin},
        {QStringLiteral("swift"), SyntaxLanguage::Swift},
        {QStringLiteral("js"), SyntaxLanguage::JavaScript},
        {QStringLiteral("mjs"), SyntaxLanguage::JavaScript},
        {QStringLiteral("cjs"), SyntaxLanguage::JavaScript},
        {QStringLiteral("jsx"), SyntaxLanguage::JavaScript},
        {QStringLiteral("tsx"), SyntaxLanguage::TypeScript},
        {QStringLiteral("py"), SyntaxLanguage::Python},
        {QStringLiteral("pyw"), SyntaxLanguage::Python},
        {QStringLiteral("go"), SyntaxLanguage::Go},
        {QStringLiteral("rs"), SyntaxLanguage::Rust},
        {QStringLiteral("rb"), SyntaxLanguage::Ruby},
        {QStringLiteral("php"), SyntaxLanguage::Php},
        {QStringLiteral("lua"), SyntaxLanguage::Lua},
        {QStringLiteral("pl"), SyntaxLanguage::Perl},
        {QStringLiteral("pm"), SyntaxLanguage::Perl},
        {QStringLiteral("sh"), SyntaxLanguage::Shell},
        {QStringLiteral("bash"), SyntaxLanguage::Shell},
        {QStringLiteral("zsh"), SyntaxLanguage::Shell},
        {QStringLiteral("ps1"), SyntaxLanguage::PowerShell},
        {QStringLiteral("psm1"), SyntaxLanguage::PowerShell},
        {QStringLiteral("cmake"), SyntaxLanguage::CMake},
        {QStringLiteral("mk"), SyntaxLanguage::Make},
        {QStringLiteral("sql"), SyntaxLanguage::Sql},
        {QStringLiteral("json"), SyntaxLanguage::Json},
        {QStringLiteral("yml"), SyntaxLanguage::Yaml},
        {QStringLiteral("yaml"), SyntaxLanguage::Yaml},
        {QStringLiteral("ini"), SyntaxLanguage::Ini},
        {QStringLiteral("cfg"), SyntaxLanguage::Ini},
        {QStringLiteral("conf"), SyntaxLanguage::Ini},
        {QStringLiteral("toml"), SyntaxLanguage::Ini},
        {QStringLiteral("desktop"), SyntaxLanguage::Ini},
        {QStringLiteral("css"), SyntaxLanguage::Css},
        {QStringLiteral("scss"), SyntaxLanguage::Css},
        {QStringLiteral("less"), SyntaxLanguage::Css},
        {QStringLiteral("qss"), SyntaxLanguage::Css},
        {QStringLiteral("xml"), SyntaxLanguage::Markup},
        {QStringLiteral("html"), SyntaxLanguage::Markup},
        {QStringLiteral("htm"), SyntaxLanguage::Markup},
        {QStringLiteral("svg"), SyntaxLanguage::Markup},
        {QStringLiteral("ui"), SyntaxLanguage::Markup},
        {QStringLiteral("qrc"), SyntaxLanguage::Markup},
        {QStringLiteral("plist"), SyntaxLanguage::Markup},
        {QStringLiteral("md"), SyntaxLanguage::Markdown},
        {QStringLiteral("markdown"), SyntaxLanguage::Markdown},
        // Qt translation catalogues share the extension with TypeScript; the
        // contents decide, see setSource().
        {QStringLiteral("ts"), SyntaxLanguage::TypeScript}
    };

    static const QHash<QString, SyntaxLanguage> byName = {
        {QStringLiteral("cmakelists.txt"), SyntaxLanguage::CMake},
        {QStringLiteral("makefile"), SyntaxLanguage::Make},
        {QStringLiteral("gnumakefile"), SyntaxLanguage::Make},
        {QStringLiteral("dockerfile"), SyntaxLanguage::Shell},
        {QStringLiteral(".gitignore"), SyntaxLanguage::Ini},
        {QStringLiteral(".gitattributes"), SyntaxLanguage::Ini},
        {QStringLiteral(".gitmodules"), SyntaxLanguage::Ini}
    };

    const QString name = fileName.section(u'/', -1);
    if (const auto named = byName.constFind(name.toLower()); named != byName.constEnd()) {
        return named.value();
    }

    const qsizetype dot = name.lastIndexOf(u'.');
    if (dot <= 0) {
        return SyntaxLanguage::None;
    }

    return byExtension.value(name.sliced(dot + 1).toLower(), SyntaxLanguage::None);
}

void SyntaxHighlighter::showFile(const QString &fileName, const QString &contents) {
    language_ = languageForFile(fileName);

    // A ".ts" holding a Qt catalogue is XML, not TypeScript.
    if (language_ == SyntaxLanguage::TypeScript
        && QStringView(contents).left(64).trimmed().startsWith(u'<')) {
        language_ = SyntaxLanguage::Markup;
    }

    applyTraits();
    document()->setPlainText(contents);
    // The rules and the text are set together on purpose: a document change
    // alone does not repaint blocks that were highlighted once already.
    rehighlight();
}

void SyntaxHighlighter::applyTraits() {
    traits_ = Traits();
    traits_.lineComments = {QStringLiteral("//")};
    traits_.blockCommentStart = QStringLiteral("/*");
    traits_.blockCommentEnd = QStringLiteral("*/");

    switch (language_) {
        case SyntaxLanguage::None:
        case SyntaxLanguage::Markup:
        case SyntaxLanguage::Markdown:
            break;
        case SyntaxLanguage::C:
            traits_.keywords = cKeywords();
            traits_.types = cTypes();
            traits_.preprocessor = true;
            break;
        case SyntaxLanguage::Cpp:
            traits_.keywords = cppKeywords();
            traits_.types = cppTypes();
            traits_.preprocessor = true;
            break;
        case SyntaxLanguage::CSharp:
            traits_.keywords = csharpKeywords();
            traits_.types = merged(javaTypes(), words("decimal object sbyte string uint "
                                                      "ulong ushort var dynamic"));
            traits_.preprocessor = true;
            break;
        case SyntaxLanguage::Java:
            traits_.keywords = javaKeywords();
            traits_.types = javaTypes();
            break;
        case SyntaxLanguage::Kotlin:
            traits_.keywords = kotlinKeywords();
            traits_.types = merged(javaTypes(), words("Any Unit Nothing Int Long Double "
                                                      "Float Boolean null true false"));
            break;
        case SyntaxLanguage::Swift:
            traits_.keywords = swiftKeywords();
            traits_.types = words("Any Bool Character Double Float Int Int8 Int16 Int32 "
                                  "Int64 String UInt Void nil true false");
            break;
        case SyntaxLanguage::JavaScript:
            traits_.keywords = javaScriptKeywords();
            traits_.types = javaScriptTypes();
            traits_.backtickStrings = true;
            break;
        case SyntaxLanguage::TypeScript:
            traits_.keywords = typeScriptKeywords();
            traits_.types = typeScriptTypes();
            traits_.backtickStrings = true;
            break;
        case SyntaxLanguage::Python:
            traits_.keywords = pythonKeywords();
            traits_.types = pythonTypes();
            traits_.lineComments = {QStringLiteral("#")};
            traits_.blockCommentStart.clear();
            traits_.blockCommentEnd.clear();
            traits_.tripleQuotes = true;
            break;
        case SyntaxLanguage::Go:
            traits_.keywords = goKeywords();
            traits_.types = goTypes();
            traits_.backtickStrings = true;
            break;
        case SyntaxLanguage::Rust:
            traits_.keywords = rustKeywords();
            traits_.types = rustTypes();
            break;
        case SyntaxLanguage::Ruby:
            traits_.keywords = rubyKeywords();
            traits_.types = words("nil true false self String Array Hash Symbol Integer "
                                  "Float");
            traits_.lineComments = {QStringLiteral("#")};
            traits_.blockCommentStart.clear();
            traits_.blockCommentEnd.clear();
            break;
        case SyntaxLanguage::Php:
            traits_.keywords = phpKeywords();
            traits_.types = words("array bool callable float int iterable mixed object string "
                                  "void null true false $this");
            traits_.lineComments = {QStringLiteral("//"), QStringLiteral("#")};
            break;
        case SyntaxLanguage::Lua:
            traits_.keywords = luaKeywords();
            traits_.lineComments = {QStringLiteral("--")};
            traits_.blockCommentStart = QStringLiteral("--[[");
            traits_.blockCommentEnd = QStringLiteral("]]");
            break;
        case SyntaxLanguage::Perl:
            traits_.keywords = perlKeywords();
            traits_.lineComments = {QStringLiteral("#")};
            traits_.blockCommentStart.clear();
            traits_.blockCommentEnd.clear();
            break;
        case SyntaxLanguage::Shell:
            traits_.keywords = shellKeywords();
            traits_.types = shellTypes();
            traits_.lineComments = {QStringLiteral("#")};
            traits_.blockCommentStart.clear();
            traits_.blockCommentEnd.clear();
            traits_.backtickStrings = true;
            break;
        case SyntaxLanguage::PowerShell:
            traits_.keywords = powerShellKeywords();
            traits_.lineComments = {QStringLiteral("#")};
            traits_.blockCommentStart = QStringLiteral("<#");
            traits_.blockCommentEnd = QStringLiteral("#>");
            break;
        case SyntaxLanguage::CMake:
            traits_.keywords = cmakeKeywords();
            traits_.types = words("ON OFF TRUE FALSE PUBLIC PRIVATE INTERFACE REQUIRED "
                                  "COMPONENTS STATIC SHARED NAMES PATHS QUIET");
            traits_.lineComments = {QStringLiteral("#")};
            traits_.blockCommentStart.clear();
            traits_.blockCommentEnd.clear();
            traits_.singleQuoteStrings = false;
            break;
        case SyntaxLanguage::Make:
            traits_.keywords = words("ifeq ifneq ifdef ifndef else endif include export "
                                     "unexport define endef override .PHONY");
            traits_.lineComments = {QStringLiteral("#")};
            traits_.blockCommentStart.clear();
            traits_.blockCommentEnd.clear();
            break;
        case SyntaxLanguage::Sql:
            traits_.keywords = sqlKeywords();
            traits_.lineComments = {QStringLiteral("--")};
            break;
        case SyntaxLanguage::Json:
            traits_.types = words("true false null");
            traits_.lineComments.clear();
            traits_.blockCommentStart.clear();
            traits_.blockCommentEnd.clear();
            traits_.singleQuoteStrings = false;
            break;
        case SyntaxLanguage::Yaml:
            traits_.types = words("true false null yes no on off");
            traits_.lineComments = {QStringLiteral("#")};
            traits_.blockCommentStart.clear();
            traits_.blockCommentEnd.clear();
            traits_.keyValue = true;
            break;
        case SyntaxLanguage::Ini:
            traits_.lineComments = {QStringLiteral("#"), QStringLiteral(";")};
            traits_.blockCommentStart.clear();
            traits_.blockCommentEnd.clear();
            traits_.keyValue = true;
            break;
        case SyntaxLanguage::Css:
            traits_.lineComments.clear();
            traits_.keyValue = true;
            break;
    }
}

void SyntaxHighlighter::highlightBlock(const QString &text) {
    switch (language_) {
        case SyntaxLanguage::None:
            return;
        case SyntaxLanguage::Markup:
            highlightMarkup(text);
            return;
        case SyntaxLanguage::Markdown:
            highlightMarkdown(text);
            return;
        default:
            highlightCode(text);
            return;
    }
}

void SyntaxHighlighter::highlightCode(const QString &text) {
    const ThemePalette &palette = Theme::instance()->palette();
    const QStringView line(text);
    int index = 0;

    switch (previousBlockState()) {
        case BlockCommentState: {
            const int end = text.indexOf(traits_.blockCommentEnd);
            if (end < 0) {
                setFormat(0, text.size(), palette.syntaxComment);
                setCurrentBlockState(BlockCommentState);
                return;
            }
            index = end + static_cast<int>(traits_.blockCommentEnd.size());
            setFormat(0, index, palette.syntaxComment);
            break;
        }
        case SingleQuoteBlockState:
        case DoubleQuoteBlockState: {
            const QString marker = previousBlockState() == SingleQuoteBlockState
                                       ? QStringLiteral("'''")
                                       : QStringLiteral("\"\"\"");
            const int end = text.indexOf(marker);
            if (end < 0) {
                setFormat(0, text.size(), palette.syntaxString);
                setCurrentBlockState(previousBlockState());
                return;
            }
            index = end + 3;
            setFormat(0, index, palette.syntaxString);
            break;
        }
        default:
            break;
    }

    setCurrentBlockState(NormalState);

    if (traits_.keyValue && index == 0) {
        const qsizetype equals = text.indexOf(u'=');
        const qsizetype colon = text.indexOf(u':');
        const qsizetype separator = equals >= 0 && colon >= 0 ? qMin(equals, colon)
                                                              : qMax(equals, colon);
        if (separator > 0) {
            int start = 0;
            while (start < separator && text.at(start).isSpace()) {
                ++start;
            }
            const QStringView key = line.sliced(start, separator - start).trimmed();
            if (!key.isEmpty() && !key.contains(u' ') && !key.startsWith(u'#')
                && !key.startsWith(u';')) {
                setFormat(start, static_cast<int>(key.size()), palette.syntaxType);
            }
        }
    }

    while (index < text.size()) {
        const QChar character = text.at(index);

        bool lineComment = false;
        for (const QString &marker : traits_.lineComments) {
            if (line.sliced(index).startsWith(marker)) {
                setFormat(index, text.size() - index, palette.syntaxComment);
                lineComment = true;
                break;
            }
        }
        if (lineComment) {
            return;
        }

        if (!traits_.blockCommentStart.isEmpty()
            && line.sliced(index).startsWith(traits_.blockCommentStart)) {
            const int end = text.indexOf(traits_.blockCommentEnd,
                                         index + traits_.blockCommentStart.size());
            if (end < 0) {
                setFormat(index, text.size() - index, palette.syntaxComment);
                setCurrentBlockState(BlockCommentState);
                return;
            }
            const int stop = end + static_cast<int>(traits_.blockCommentEnd.size());
            setFormat(index, stop - index, palette.syntaxComment);
            index = stop;
            continue;
        }

        if (character == u'"' || (character == u'\'' && traits_.singleQuoteStrings)
            || (character == u'`' && traits_.backtickStrings)) {
            index = highlightString(text, index, character);
            continue;
        }

        // A directive is coloured on its own; the rest of the line is ordinary
        // code, so "#include "file.h"" keeps its string.
        if (traits_.preprocessor && character == u'#'
            && line.first(index).trimmed().isEmpty()) {
            int stop = index + 1;
            while (stop < text.size() && text.at(stop).isLetter()) {
                ++stop;
            }
            setFormat(index, stop - index, palette.syntaxMeta);
            index = stop;
            continue;
        }

        if (character.isDigit() && (index == 0 || !isWordCharacter(text.at(index - 1)))) {
            index = highlightNumber(text, index);
            continue;
        }

        if (character.isLetter() || character == u'_') {
            index = highlightWord(text, index);
            continue;
        }

        ++index;
    }
}

int SyntaxHighlighter::highlightWord(const QString &text, const int start) {
    int stop = start;
    while (stop < text.size() && isWordCharacter(text.at(stop))) {
        ++stop;
    }

    const QString word = text.sliced(start, stop - start);
    const ThemePalette &palette = Theme::instance()->palette();
    if (traits_.keywords.contains(word)) {
        setFormat(start, stop - start, palette.syntaxKeyword);
    } else if (traits_.types.contains(word)) {
        setFormat(start, stop - start, palette.syntaxType);
    }

    return stop;
}

int SyntaxHighlighter::highlightNumber(const QString &text, const int start) {
    int stop = start;
    while (stop < text.size()
           && (text.at(stop).isLetterOrNumber() || text.at(stop) == u'.'
               || text.at(stop) == u'\'')) {
        ++stop;
    }

    setFormat(start, stop - start, Theme::instance()->palette().syntaxNumber);
    return stop;
}

int SyntaxHighlighter::highlightString(const QString &text, const int start,
                                       const QChar quote) {
    const ThemePalette &palette = Theme::instance()->palette();

    if (traits_.tripleQuotes && text.sliced(start).startsWith(QString(3, quote))) {
        const int end = text.indexOf(QString(3, quote), start + 3);
        if (end < 0) {
            setFormat(start, text.size() - start, palette.syntaxString);
            setCurrentBlockState(quote == u'\'' ? SingleQuoteBlockState
                                                : DoubleQuoteBlockState);
            return text.size();
        }
        setFormat(start, end + 3 - start, palette.syntaxString);
        return end + 3;
    }

    int stop = start + 1;
    while (stop < text.size()) {
        if (text.at(stop) == u'\\' && stop + 1 < text.size()) {
            stop += 2;
            continue;
        }
        if (text.at(stop) == quote) {
            ++stop;
            break;
        }
        ++stop;
    }

    setFormat(start, stop - start, palette.syntaxString);
    return stop;
}

void SyntaxHighlighter::highlightMarkup(const QString &text) {
    const ThemePalette &palette = Theme::instance()->palette();
    const QStringView line(text);
    int index = 0;

    if (previousBlockState() == BlockCommentState) {
        const int end = text.indexOf(QStringLiteral("-->"));
        if (end < 0) {
            setFormat(0, text.size(), palette.syntaxComment);
            setCurrentBlockState(BlockCommentState);
            return;
        }
        index = end + 3;
        setFormat(0, index, palette.syntaxComment);
    }

    setCurrentBlockState(NormalState);

    while (index < text.size()) {
        if (line.sliced(index).startsWith(QStringLiteral("<!--"))) {
            const int end = text.indexOf(QStringLiteral("-->"), index);
            if (end < 0) {
                setFormat(index, text.size() - index, palette.syntaxComment);
                setCurrentBlockState(BlockCommentState);
                return;
            }
            setFormat(index, end + 3 - index, palette.syntaxComment);
            index = end + 3;
            continue;
        }

        if (text.at(index) != u'<') {
            ++index;
            continue;
        }

        int cursor = index + 1;
        while (cursor < text.size() && (text.at(cursor) == u'/' || text.at(cursor) == u'?'
                                        || text.at(cursor) == u'!')) {
            ++cursor;
        }
        while (cursor < text.size() && (text.at(cursor).isLetterOrNumber()
                                        || text.at(cursor) == u'_' || text.at(cursor) == u'-'
                                        || text.at(cursor) == u':')) {
            ++cursor;
        }
        setFormat(index, cursor - index, palette.syntaxKeyword);
        index = cursor;

        while (index < text.size() && text.at(index) != u'>') {
            const QChar character = text.at(index);
            if (character == u'"' || character == u'\'') {
                index = highlightString(text, index, character);
                continue;
            }
            if (character.isLetter() || character == u'_') {
                int stop = index;
                while (stop < text.size() && (text.at(stop).isLetterOrNumber()
                                              || text.at(stop) == u'_'
                                              || text.at(stop) == u'-'
                                              || text.at(stop) == u':')) {
                    ++stop;
                }
                setFormat(index, stop - index, palette.syntaxType);
                index = stop;
                continue;
            }
            ++index;
        }

        if (index < text.size()) {
            setFormat(index, 1, palette.syntaxKeyword);
            ++index;
        }
    }
}

void SyntaxHighlighter::highlightMarkdown(const QString &text) {
    const ThemePalette &palette = Theme::instance()->palette();
    const QStringView line(text);

    const bool fence = line.trimmed().startsWith(QStringLiteral("```"));
    if (previousBlockState() == FenceState) {
        setCurrentBlockState(fence ? NormalState : FenceState);
        if (fence) {
            setFormat(0, text.size(), palette.syntaxMeta);
        }
        return;
    }
    if (fence) {
        setCurrentBlockState(FenceState);
        setFormat(0, text.size(), palette.syntaxMeta);
        return;
    }

    setCurrentBlockState(NormalState);

    if (line.trimmed().startsWith(u'#')) {
        setFormat(0, text.size(), palette.syntaxKeyword);
        return;
    }
    if (line.trimmed().startsWith(u'>')) {
        setFormat(0, text.size(), palette.syntaxComment);
        return;
    }

    for (int index = 0; index < text.size(); ++index) {
        if (text.at(index) == u'`') {
            const int end = text.indexOf(u'`', index + 1);
            if (end < 0) {
                break;
            }
            setFormat(index, end + 1 - index, palette.syntaxString);
            index = end;
            continue;
        }
        if (text.at(index) == u'(' && index > 0 && text.at(index - 1) == u']') {
            const int end = text.indexOf(u')', index + 1);
            if (end < 0) {
                break;
            }
            setFormat(index + 1, end - index - 1, palette.syntaxType);
            index = end;
        }
    }
}
