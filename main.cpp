// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "icons.h"
#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStyleFactory>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("SquidyGit"));
    QCoreApplication::setApplicationName(QStringLiteral("SquidyGit"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SQUIDYGIT_VERSION));

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
