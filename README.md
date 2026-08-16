<!-- Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru> -->

# SquidyGit

<p align="center">
  <img src="resources/squidygit-128.png" width="96" alt="SquidyGit logo">
</p>

<p align="center">
  A fast native desktop Git client for Linux and Windows, built with C++20 and Qt 6.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-0.0.1-2d5d8c" alt="Version 0.0.1">
</p>

<p align="center">
  <a href="README.md">English</a> · <a href="README.ru.md">Русский</a> · <a href="README.zh-CN.md">简体中文</a>
</p>

## Why SquidyGit exists

I wanted the kind of desktop client that keeps history, staging and branches in one
window, and on Linux I could not find one that fit. Some tools were too limited, some
were too heavy for what they did, and the polished ones were either commercial or simply
never released for Linux. SourceTree, whose layout I had been comfortable with elsewhere,
is available only for macOS and Windows.

So I started writing the client I wanted to use every day: a familiar layout, native
code, no account to create, no telemetry, and an MIT licence.

SquidyGit keeps the common Git operations visible instead of wrapping them in its own
vocabulary. It drives the `git` executable already installed on your system and writes
every command it runs into the built-in log, so nothing happens that you cannot inspect
or repeat in a terminal yourself.

## What makes it different

- Every Git command the application runs is shown in the command log together with its
  output
- SquidyGit calls the system `git` binary and carries no embedded Git implementation, so
  behaviour matches your own configuration, hooks and aliases
- C++20 and Qt 6, without Electron, a bundled runtime or a background service
- MIT licence, no sign-in, no telemetry, no feature gates and no licence reminders
- English, Russian and Simplified Chinese cover the whole interface, not just a handful
  of menus

## Screenshots

The screenshots below show the current English interface using the light theme.

### Repository workspace

Branches, tags, staged and unstaged files, a live diff and commit controls stay visible
in one compact, resizable workspace.

<p align="center">
  <img src="resources/screenshots/en/file-status.png" alt="English file status and staging workspace">
</p>

### Commit history

The history view combines a visual graph, branch and tag labels, filters, changed files,
commit details and a diff preview.

<p align="center">
  <img src="resources/screenshots/en/history.png" alt="English commit history and visual graph">
</p>

### Repository list

Open an existing repository, clone a remote project or initialize a new one.

<p align="center">
  <img src="resources/screenshots/en/main-window.png" alt="English local repository list">
</p>

## What works today

### Working tree and commits

- Separate lists of staged and unstaged files, with flat and directory-tree views,
  filtering and status badges
- Unified diff viewer with old and new line numbers and colour highlighting
- A full side-by-side diff window, opened from the diff context menu
- Staging, unstaging and discarding by hunk or by individual line
- Commit, amend and an optional push right after the commit

### History

- Visual commit graph with branches, tags, merge commits and commit details
- Changed files and a diff preview for the selected commit
- Search by message, author, file contents, path or SHA

### Files and file history

- Browse tracked files in the working copy, HEAD, any branch, tag or commit without
  checking it out
- Follow a file across renames, reach deleted files through older revisions and jump
  from the file timeline to the full commit
- View the changes made by a commit, read historical contents or compare a historical
  version with the revision selected in the tree
- Filter by file name and preview source code with line numbers and syntax highlighting,
  images and SVG drawings, PDFs when Qt PDF is available, and binary data as a hex dump

### Branches and remotes

- Create, check out, rename, delete, merge and rebase branches
- Fetch, pull and push with upstream tracking, tags and `--force-with-lease`
- Cherry-pick, revert and reset, with confirmation for destructive operations
- Stashes, tags, remotes and a submodule list

### Application

- Repository tabs, bookmarks and automatic session restoration
- Background remote checks keep incoming commit counters current without pressing
  Fetch
- Light and dark themes
- English, Russian and Simplified Chinese, with a manual language selector
- Quick access to a terminal and to the repository folder
- Built-in log of every executed Git command

## What is planned

The list below is roughly in the order I intend to work on it. It is a statement of
intent, not a schedule.

### Next

- Credential handling: a built-in prompt for HTTPS passwords and SSH key passphrases,
  so private repositories work without preparing a credential helper first
- Automatic refresh when the repository changes outside the application
- External diff and merge tools, including conflict resolution through Meld, KDiff3 and
  similar utilities
- Blame with per-line authorship

### Planned

- Git Flow: initialization and the feature, release and hotfix operations
- Interactive rebase with reordering, squashing and message editing
- Undo for the last operation, based on `git reflog`
- Repository profiles: per-repository name, e-mail and key, with a warning before
  committing as the wrong author
- Exporting the command log as a runnable shell script, so a sequence performed in the
  interface can be moved into a terminal or a CI job
- Submodule operations, `git clean`, creating and applying patches
- Faster history on large repositories through lazy loading
- Custom actions: user-defined commands available from the menus

### Under consideration

- Git LFS support
- Pull request integration with the common hosting services
- macOS builds

## Contributing

I write SquidyGit in my own time, and I would be glad if someone joined in. Bug reports
and ideas are useful on their own, and anything from the list above is free to pick up.
Say so in an issue first and it will not be written twice. Patches are welcome regardless
of size; so are translations into other languages and a second pair of eyes on the
interface wording.

If you would rather ask something before starting, write to &lt;sergey@squidy.ru&gt;
or open an issue.

## Interface languages

SquidyGit follows the operating-system language by default. English, Russian and
Simplified Chinese can also be selected manually from **View → Language**; the new
language is applied after the application restarts.

## Technology

SquidyGit is a native C++20 application built with:

- Qt 6: Core, Gui, Widgets, Concurrent, Network and SVG; PDF/PdfWidgets is optional
- CMake 3.16 or newer
- The system Git command-line client

No runtime is bundled, and there is no embedded Git implementation.

## Build

### Linux

Install a C++20 compiler, CMake, Git and the Qt 6 development packages, including the
Qt Linguist tools (`qt6-l10n-tools` and `qt6-tools-dev` on Debian and Ubuntu), then run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/SquidyGit
```

The Qt PDF development module is optional (`qt6-pdf-dev` on Debian and Ubuntu). Without
it, SquidyGit still builds and shows PDF revisions as a hexadecimal byte dump.

To install the application, desktop entry and icons for the current user:

```bash
cmake --install build --prefix ~/.local
```

To build a DEB package for Debian or Ubuntu:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target deb_package
```

The `deb_package` target always uses a separate Release build, even when the active CLion
profile is configured for Debug. Reload CMake in CLion, select `deb_package` in the CMake
tool window and build the target. The resulting package is written to `build/dist`.

### Windows

Install Qt 6 with MinGW, then point CMake to your Qt installation:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/mingw_64
cmake --build build --parallel
.\build\SquidyGit.exe
```

The Windows build automatically runs `windeployqt` and copies the required Qt runtime
files next to the executable.

To build the Windows installer, install Inno Setup 6.3 or newer and run:

```powershell
cmake --build build --target windows_installer
```

The installer is written to `build/dist/SquidyGit-0.0.1-windows-x64.exe`. It installs
SquidyGit into `Program Files`, creates Start menu and desktop shortcuts, and includes
an uninstaller. Administrator permission is requested during installation. A newer
installer upgrades the existing installation in place, which is what the built-in
update check uses.

## Safety and current limits

Potentially destructive operations, including discarding changes, hard reset and branch
deletion, require confirmation.

Interactive credential prompts are disabled so that Git can never block the interface
waiting for input. Until the built-in prompt described above is finished, private
repositories therefore need an SSH key or a configured Git credential helper.

Conflicts can currently be resolved by taking one side of the file as a whole; a
three-way view and external merge tools are on the list.

## License

SquidyGit is available under the [MIT License](LICENSE).

Copyright (c) 2026 Sergey Yakunin &lt;sergey@squidy.ru&gt;
