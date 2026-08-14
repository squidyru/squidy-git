// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "ui/icons.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QStyleFactory>
#include <QTranslator>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

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
    QApplication::setDesktopFileName(QStringLiteral("squidygit"));

    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"))) {
        QApplication::setStyle(QStringLiteral("Fusion"));
    }
    Theme::instance()->applyToApplication();

    MainWindow window;
    window.show();

    return QApplication::exec();
}
