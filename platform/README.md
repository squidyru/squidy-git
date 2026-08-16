# Platform boundary

Runtime integration with the host operating system belongs in this directory. The rest
of SquidyGit depends only on the `PlatformServices` interface and must not select behavior
with `Q_OS_*`, `_WIN32`, or similar preprocessor checks.

The shared implementation in `platformservices.cpp` contains behavior provided uniformly
by Qt. CMake compiles exactly one host implementation:

- `platformservices_linux.cpp`
- `platformservices_windows.cpp`
- `platformservices_macos.cpp`
- `platformservices_generic.cpp` for unsupported systems

The boundary currently owns Git executable discovery, terminals and file managers,
application restart and desktop registration, null-device paths, download locations, and
the selection and launch of platform update packages. Build-time packaging and dependency
selection remain in CMake because they configure the build rather than application
runtime behavior.

When adding a platform-dependent feature:

1. Add the smallest platform-neutral operation to `PlatformServices`.
2. Give the shared class a safe default where possible.
3. Implement host-specific behavior only in the corresponding source file.
4. Keep user interaction and workflow decisions in core or UI code.
5. Extend `tst_platformservices` with the observable contract.
