// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>

struct ThemePalette {
    QColor window;
    QColor chrome;
    QColor chromeText;
    QColor tabInactive;
    QColor sidebarSelection;
    QColor toolbar;
    QColor sidebar;
    QColor surface;
    QColor surfaceAlternate;
    /// Every second row of the file tree, in the manner of a Finder list.
    QColor rowStripe;
    QColor border;
    QColor text;
    QColor mutedText;
    QColor sectionText;
    QColor accent;
    QColor accentHover;
    QColor accentText;
    QColor selection;
    QColor selectionText;
    QColor hover;
    QColor success;
    QColor warning;
    QColor danger;
    QColor staged;
    QColor untracked;
    QColor addedBackground;
    QColor addedText;
    QColor removedBackground;
    QColor removedText;
    QColor hunkBackground;
    QColor hunkText;
    QColor graphNodeBorder;
    /// Colours of the read-only source viewer.
    QColor syntaxKeyword;
    QColor syntaxType;
    QColor syntaxString;
    QColor syntaxNumber;
    QColor syntaxComment;
    QColor syntaxMeta;
    QList<QColor> laneColors;
};

/// Application-wide look and feel with light and dark themes;
/// both are reproduced here from a single stylesheet template.
class Theme final : public QObject {
    Q_OBJECT

public:
    enum class Mode {
        Light,
        Dark
    };

    static Theme *instance();

    [[nodiscard]] Mode mode() const;
    void setMode(Mode mode);
    [[nodiscard]] const ThemePalette &palette() const;
    [[nodiscard]] QString styleSheet() const;
    void applyToApplication() const;

Q_SIGNALS:
    void changed();

private:
    explicit Theme(QObject *parent = nullptr);

    Mode mode_ = Mode::Light;
    ThemePalette palette_;
};
