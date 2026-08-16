// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "ui/credentialprompter.h"
#include "ui/icons.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"
#include "platform/platformservices.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QStyleFactory>
#include <QTranslator>

namespace {

void registerBundledFonts() {
    const QStringList fontResources{
        QStringLiteral(":/branding/fonts/NotoSans-Regular.ttf"),
        QStringLiteral(":/branding/fonts/NotoSans-SemiBold.ttf"),
        QStringLiteral(":/branding/fonts/NotoSans-Bold.ttf")
    };

    for (const QString &fontResource : fontResources) {
        static_cast<void>(QFontDatabase::addApplicationFont(fontResource));
    }
}

} // namespace

int main(int argc, char *argv[]) {
    // Git and ssh run this same binary to ask for a credential, passing the
    // question as the only argument. That mode answers on standard output and
    // exits, so it must not reach the interface below.
    if (CredentialPrompter::isHelperInvocation()) {
        QCoreApplication helper(argc, argv);
        const QStringList arguments = QCoreApplication::arguments();
        return CredentialPrompter::runHelper(arguments.size() > 1 ? arguments.at(1)
                                                                  : QString());
    }

    QApplication app(argc, argv);
    registerBundledFonts();

    QCoreApplication::setOrganizationName(QStringLiteral("SquidyGit"));
    QCoreApplication::setApplicationName(QStringLiteral("SquidyGit"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SQUIDYGIT_VERSION));

    // The interface follows the system language unless one was chosen in the
    // View menu. Untranslated languages fall back to English, the language the
    // sources are written in. The catalog of Qt itself is loaded as well, so
    // the buttons of the standard dialogs match.
    const QString language =
        QSettings().value(QStringLiteral("interface/language")).toString();
    const QLocale locale = language.isEmpty() ? QLocale::system() : QLocale(language);
    QTranslator qtTranslator;
    if (qtTranslator.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(&qtTranslator);
    }
    QTranslator applicationTranslator;
    if (applicationTranslator.load(locale, QStringLiteral("squidygit"),
                                   QStringLiteral("_"), QStringLiteral(":/i18n"))) {
        QCoreApplication::installTranslator(&applicationTranslator);
    }

    QApplication::setWindowIcon(Icons::applicationIcon());
    PlatformServices::instance().configureApplication();

    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"))) {
        QApplication::setStyle(QStringLiteral("Fusion"));
    }
    Theme::instance()->applyToApplication();

    // Started before the window so that the very first fetch can already ask.
    CredentialPrompter credentials;
    static_cast<void>(credentials.start());

    MainWindow window;
    window.show();

    return QApplication::exec();
}
