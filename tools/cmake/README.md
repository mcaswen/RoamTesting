# Portable CMake

This directory vendors the Windows x86_64 CMake package used by the CMD build scripts:

```text
tools/cmake/bin/cmake.exe
```

The `.bat` scripts use this executable before checking `PATH`, so a Windows target machine does not need a separate CMake install.

If this copy is missing or needs to be refreshed, run:

```bat
scripts\setup_portable_cmake.bat
```

That helper downloads the pinned CMake zip again and restores the same directory layout.
