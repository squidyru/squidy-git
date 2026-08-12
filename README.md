<!-- Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru> -->

# SquidyGit

<p align="center">
  <img src="resources/squidygit-128.png" width="96" alt="SquidyGit logo">
</p>

<p align="center">
  A fast, native desktop Git client built with C++20 and Qt 6.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-0.0.1-2d5d8c" alt="Version 0.0.1">
</p>

<p align="center">
  <a href="README.md">English</a> · <a href="README.ru.md">Русский</a>
</p>

## Why SquidyGit?

I could not find a Linux Git client that felt comfortable for my everyday workflow. Some
tools were too limited, others felt overly complicated, and many did not provide the
combination of repository history, staging and branch management I wanted in one native
desktop application. So I decided to build my own.

SquidyGit is designed to keep common Git operations visible and approachable without
hiding the underlying tool. It runs the system `git` executable and shows every command
in its command log.

## Screenshots

### Repository workspace

Staged and unstaged files, branches, tags and a live diff are available in one view.

<p align="center">
  <img src="resources/screenshots/file-status.png" alt="File status and staging workspace">
</p>

### Commit history

The history view combines the commit graph, references, changed files and diff preview.

<p align="center">
  <img src="resources/screenshots/history.png" alt="Commit history and visual graph">
</p>

### Repository list

Open an existing repository, clone a remote project or initialize a new one.

<p align="center">
  <img src="resources/screenshots/main-window.png" alt="Local repository list">
</p>

## Features

- Repository tabs, bookmarks and session restoration
- Working-tree view with separate staged and unstaged file lists
- Flat and directory-tree file views with filtering and status badges
- Unified diff viewer with old/new line numbers and syntax-aware highlighting
- Partial staging and discarding by hunk or individual line
- Commit, amend and optional push-after-commit workflows
- Visual commit graph with branches, tags and merge commits
- Branch creation, checkout, rename, deletion, merge and rebase
- Fetch, pull and push with upstream, tags and `--force-with-lease` options
- Stash, tags, remotes and submodule management
- Commit search by message, author, file contents, path or SHA
- Light and dark themes
- Built-in log of all executed Git commands

## Technology

SquidyGit is a native C++20 application built with:

- Qt 6: Core, Gui, Widgets and Concurrent
- CMake 3.16 or newer
- The system Git command-line client

No additional runtime library or embedded Git implementation is used.

## Build

### Linux

Install a C++20 compiler, CMake, Git and the Qt 6 development packages, then run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/SquidyGit
```

To install the application, desktop entry and icons for the current user:

```bash
cmake --install build --prefix ~/.local
```

### Windows

Install Qt 6 with MinGW, then point CMake to your Qt installation:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/mingw_64
cmake --build build --parallel
.\build\SquidyGit.exe
```

The Windows build automatically runs `windeployqt` and copies the required Qt runtime
files next to the executable.

To build the offline Windows installer, install Qt Installer Framework and run:

```powershell
cmake --build build --target windows_installer
```

The installer is written to `build/dist/SquidyGit-0.0.1-windows-x64.exe`. It installs
SquidyGit into `Program Files`, creates Start menu and desktop shortcuts, and includes
an uninstaller. Administrator permission is requested during installation.

## Safety

Potentially destructive operations, including discarding changes, hard reset and branch
deletion, require confirmation. Interactive credential prompts are disabled, so private
repositories require an SSH key or a configured Git credential helper.

## License

SquidyGit is available under the [MIT License](LICENSE).

Copyright (c) 2026 Sergey Yakunin &lt;sergey@squidy.ru&gt;
