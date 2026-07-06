# Portable CMake

Windows CMD scripts will first look for CMake here:

```text
tools/cmake/bin/cmake.exe
```

If neither this portable copy nor a system `cmake` is available, the `.bat` build scripts automatically call:

```bat
scripts\setup_portable_cmake.bat
```

The downloaded CMake files are intentionally ignored by Git.
