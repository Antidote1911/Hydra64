# Cross-compilation Linux → 64-bit Windows with the system MinGW (UCRT, POSIX threads).
# Qt and zlib come from the MSYS2 "ucrt64" packages stored by tools/windows/fetch_sysroot.py;
# the Qt tools (moc, rcc, lrelease) are those of the machine's Qt (QT_HOST_PATH).
# Used by "./build.sh --win", which passes HYDRA64_WIN_SYSROOT.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

if(NOT HYDRA64_WIN_SYSROOT)
    set(HYDRA64_WIN_SYSROOT "$ENV{HYDRA64_WIN_SYSROOT}")
endif()
set(ENV{HYDRA64_WIN_SYSROOT} "${HYDRA64_WIN_SYSROOT}") # reread by the sub-configurations (try_compile)
set(CMAKE_FIND_ROOT_PATH "${HYDRA64_WIN_SYSROOT}/ucrt64" /usr/x86_64-w64-mingw32)
set(CMAKE_PREFIX_PATH "${HYDRA64_WIN_SYSROOT}/ucrt64")
# Programs: those of the machine; libraries, headers and CMake packages: those of Windows.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
