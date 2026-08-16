// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

/// Vector glyphs drawn with QPainter so the application needs no external assets
/// and stays crisp on high-DPI screens with flat toolbar icons.
namespace Icons {

inline constexpr int WindowControlButtonWidth = 46;
inline constexpr int WindowControlButtonHeight = 30;
inline constexpr int WindowControlIconSize = 16;

enum class Glyph {
    Commit,
    Checkout,
    Discard,
    Stash,
    StashPop,
    Fetch,
    Pull,
    Push,
    Branch,
    Merge,
    Rebase,
    Tag,
    Terminal,
    Explorer,
    Settings,
    Remote,
    Refresh,
    OpenFolder,
    Clone,
    Create,
    Search,
    FileStatus,
    History,
    Add,
    Remove,
    Trash,
    Reset,
    CherryPick,
    Submodule,
    Repository,
    Folder,
    File,
    FileText,
    FileImage,
    FilePdf,
    FileCode,
    FileArchive,
    Ghost,
    Warning,
    WindowMinimize,
    WindowMaximize,
    WindowRestore,
    WindowClose,
    TabClose
};

[[nodiscard]] QPixmap pixmap(Glyph glyph, int size, const QColor &color);
[[nodiscard]] QIcon icon(Glyph glyph, const QColor &color = QColor());

/// Picks the file glyph that matches the extension of @p fileName.
[[nodiscard]] Glyph fileGlyph(const QString &fileName);

/// The application mark: a squid-ghost on a round red gradient.
[[nodiscard]] QPixmap applicationPixmap(int size);
[[nodiscard]] QIcon applicationIcon();

}
