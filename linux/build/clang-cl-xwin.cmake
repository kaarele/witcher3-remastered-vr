# clang-cl-xwin.cmake - CMake toolchain: clang-cl + lld-link on Linux targeting
# x86_64-pc-windows-msvc, using a CRT + Windows SDK sysroot produced by xwin
# (https://github.com/Jake-Shadle/xwin).
#
# Expected sysroot layout (default `xwin splat` output, NOT --use-winsysroot-style,
# NOT --preserve-ms-arch-notation):
#   ${XWIN_SYSROOT}/crt/include
#   ${XWIN_SYSROOT}/crt/lib/x86_64
#   ${XWIN_SYSROOT}/sdk/include/{ucrt,um,shared,winrt,cppwinrt}
#   ${XWIN_SYSROOT}/sdk/lib/{um,ucrt}/x86_64
#
# Produce it with (inside the build container):
#   xwin --accept-license --arch x86_64 [--manifest-version 17] \
#        [--crt-version <ver>] [--sdk-version <ver>] \
#        splat --output <sysroot>
#
# Casing: by default `xwin splat` keeps original SDK file names and adds
# symlinks (all-lowercase alias for every mixed-case SDK header and lib,
# UPPERCASE .lib aliases, LIBCMT.lib/OLDNAMES.lib aliases for the CRT). Do NOT
# pass --disable-symlinks on a case-sensitive filesystem. Debug CRT libs
# (libcmtd/libucrtd/libvcruntimed/libcpmtd/msvcrtd) are only splatted with
# --include-debug-libs; that is required for /MTd (Debug) builds only, not for
# Release (/MT). This file also runs try_compile checks in Release so the
# default /MDd of CMake's compiler checks never needs the debug libs.
#
# Requirements: clang-cl, lld-link, llvm-lib, llvm-rc, llvm-mt (LLVM >= the
# version demanded by the MSVC STL in the sysroot; STL 14.44 requires Clang 19+,
# checked below), CMake >= 3.24, Ninja. llvm-mt is
# optional (Debug/incremental manifest merging only).
#
# Cache/command-line knobs (all optional):
#   XWIN_SYSROOT              sysroot path (or env XWIN_SYSROOT); required
#   XWIN_LLVM_ROOT            directory containing the LLVM tools, searched before PATH
#                             (e.g. /usr/lib/llvm-22/bin)
#   XWIN_LLVM_SUFFIX          tool name suffix (e.g. -20 for clang-cl-20)
#   XWIN_MSVC_COMPAT_VERSION  value for -fms-compatibility-version (default: parsed
#                             from the sysroot's yvals_core.h, fallback 19.44)
#   XWIN_ASYNC_SEH            ON (default): add -fasync-exceptions so SEH __try/__except
#                             also catches faults raised by instructions *inside* the
#                             __try body (MSVC semantics). See notes at the bottom.
#   XWIN_MSVC_DIAG_PARITY     ON (default): downgrade clang-only hard errors that MSVC
#                             only warns about (narrowing in aggregate init, C4838).
#   XWIN_CASEFIX_LIBS         extra library base names to alias case-insensitively.
#   XWIN_VS_SUBSYSTEM_PARITY  ON (default): link DLLs with /subsystem:console, as the
#                             Visual Studio generator build does (PE header parity only).

cmake_minimum_required(VERSION 3.24)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(XWIN_ARCH x86_64)
set(XWIN_TRIPLE x86_64-pc-windows-msvc)

# ---------------------------------------------------------------- sysroot ----
if(NOT DEFINED XWIN_SYSROOT)
    if(DEFINED ENV{XWIN_SYSROOT})
        set(XWIN_SYSROOT "$ENV{XWIN_SYSROOT}")
    else()
        message(FATAL_ERROR
            "clang-cl-xwin: set XWIN_SYSROOT (cache variable or environment) to the xwin splat output.\n"
            "Create it after accepting Microsoft's licence terms, see linux/BUILD-LINUX.md:\n"
            "  xwin --accept-license --arch x86_64 splat --output <sysroot>")
    endif()
endif()
set(XWIN_SYSROOT "${XWIN_SYSROOT}" CACHE PATH "xwin splat output directory")

set(_xwin_crt_inc   "${XWIN_SYSROOT}/crt/include")
set(_xwin_sdk_inc   "${XWIN_SYSROOT}/sdk/include")
set(_xwin_crt_lib   "${XWIN_SYSROOT}/crt/lib/${XWIN_ARCH}")
set(_xwin_um_lib    "${XWIN_SYSROOT}/sdk/lib/um/${XWIN_ARCH}")
set(_xwin_ucrt_lib  "${XWIN_SYSROOT}/sdk/lib/ucrt/${XWIN_ARCH}")

foreach(_p IN ITEMS
        "${_xwin_crt_inc}/vcruntime.h"
        "${_xwin_crt_inc}/yvals_core.h"
        "${_xwin_sdk_inc}/ucrt/stdio.h"
        # lowercase names: these only exist if xwin created its casing symlinks
        # (MinHook includes <windows.h>/<tlhelp32.h>), so this also catches a
        # splat made with --disable-symlinks.
        "${_xwin_sdk_inc}/um/windows.h"
        "${_xwin_sdk_inc}/um/tlhelp32.h"
        "${_xwin_sdk_inc}/shared/sdkddkver.h"
        "${_xwin_crt_lib}/libcmt.lib"
        "${_xwin_crt_lib}/libcpmt.lib"
        "${_xwin_um_lib}/kernel32.lib"
        "${_xwin_um_lib}/d3d12.lib"
        "${_xwin_ucrt_lib}/libucrt.lib")
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR
            "clang-cl-xwin: missing ${_p}\n"
            "XWIN_SYSROOT='${XWIN_SYSROOT}' is not a complete x86_64 xwin splat. Run:\n"
            "  xwin --accept-license --arch x86_64 splat --output ${XWIN_SYSROOT}")
    endif()
endforeach()

# ------------------------------------------------------------- LLVM tools ----
set(XWIN_LLVM_ROOT "" CACHE PATH "Directory holding clang-cl/lld-link/llvm-* (optional)")
set(XWIN_LLVM_SUFFIX "" CACHE STRING "LLVM tool name suffix, e.g. -20 (optional)")

function(_xwin_find_tool var name)
    if(${var})
        return()
    endif()
    set(_hints)
    if(XWIN_LLVM_ROOT)
        list(APPEND _hints "${XWIN_LLVM_ROOT}")
    endif()
    find_program(${var}
        NAMES "${name}${XWIN_LLVM_SUFFIX}" "${name}"
        HINTS ${_hints}
        NO_CACHE)
    if(NOT ${var})
        if(ARGC GREATER 2 AND "${ARGV2}" STREQUAL "OPTIONAL")
            message(STATUS "clang-cl-xwin: '${name}' not found (only needed for Debug/incremental links)")
            set(${var} "" PARENT_SCOPE)
            return()
        endif()
        message(FATAL_ERROR "clang-cl-xwin: could not find '${name}${XWIN_LLVM_SUFFIX}' "
                            "(set XWIN_LLVM_ROOT, e.g. /usr/lib/llvm-22/bin, or XWIN_LLVM_SUFFIX)")
    endif()
    set(${var} "${${var}}" PARENT_SCOPE)
endfunction()

_xwin_find_tool(_xwin_clang_cl clang-cl)
_xwin_find_tool(_xwin_lld_link lld-link)
_xwin_find_tool(_xwin_llvm_lib llvm-lib)
_xwin_find_tool(_xwin_llvm_rc  llvm-rc)   # CMake enables RC for MSVC-like toolchains
_xwin_find_tool(_xwin_llvm_mt  llvm-mt OPTIONAL)

set(CMAKE_C_COMPILER   "${_xwin_clang_cl}")
set(CMAKE_CXX_COMPILER "${_xwin_clang_cl}")
# Absolute paths in the cache: otherwise CMake may pick /usr/bin/link (coreutils)
# as the MSVC-style linker, or try to locate rc.exe / mt.exe.
set(CMAKE_LINKER       "${_xwin_lld_link}" CACHE FILEPATH "lld-link" FORCE)
set(CMAKE_AR           "${_xwin_llvm_lib}" CACHE FILEPATH "llvm-lib" FORCE)
set(CMAKE_RC_COMPILER  "${_xwin_llvm_rc}"  CACHE FILEPATH "llvm-rc"  FORCE)
if(_xwin_llvm_mt)
    set(CMAKE_MT       "${_xwin_llvm_mt}"  CACHE FILEPATH "llvm-mt"  FORCE)
endif()

# ------------------------------------------- MSVC/STL version consistency ----
# The MSVC STL hard-errors (STL1000/STL1001) on a too-old compiler. Read what the
# splatted STL wants and (a) present a matching _MSC_VER, (b) check clang.
set(_xwin_stl_msvc "")
set(_xwin_stl_clang "")
file(STRINGS "${_xwin_crt_inc}/yvals_core.h" _xwin_yvals
     REGEX "expected (MSVC|Clang) [0-9.]+ or newer")
foreach(_l IN LISTS _xwin_yvals)
    if(_l MATCHES "expected MSVC (19\\.[0-9]+) or newer")
        set(_xwin_stl_msvc "${CMAKE_MATCH_1}")
    elseif(_l MATCHES "expected Clang ([0-9]+)\\.")
        set(_xwin_stl_clang "${CMAKE_MATCH_1}")
    endif()
endforeach()

if(NOT DEFINED XWIN_MSVC_COMPAT_VERSION)
    if(_xwin_stl_msvc)
        set(XWIN_MSVC_COMPAT_VERSION "${_xwin_stl_msvc}")
    else()
        set(XWIN_MSVC_COMPAT_VERSION "19.44") # VS 2022 17.14 (MSVC 14.44)
    endif()
endif()
set(XWIN_MSVC_COMPAT_VERSION "${XWIN_MSVC_COMPAT_VERSION}" CACHE STRING
    "-fms-compatibility-version (=> _MSC_VER / MSVC_VERSION)")

if(_xwin_stl_clang)
    execute_process(COMMAND "${_xwin_clang_cl}" --version
                    OUTPUT_VARIABLE _xwin_cl_ver ERROR_QUIET)
    if(_xwin_cl_ver MATCHES "clang version ([0-9]+)\\.")
        if(CMAKE_MATCH_1 LESS _xwin_stl_clang)
            message(WARNING
                "clang-cl-xwin: clang ${CMAKE_MATCH_1} is older than the Clang "
                "${_xwin_stl_clang} required by the MSVC STL in ${XWIN_SYSROOT}. "
                "Use a newer LLVM, splat an older CRT (xwin --crt-version), or as a "
                "last resort add -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH.")
        endif()
    endif()
endif()

# ------------------------------------------------------------ compile flags ----
option(XWIN_ASYNC_SEH "Make SEH __try catch same-frame hardware faults (-fasync-exceptions)" ON)
option(XWIN_MSVC_DIAG_PARITY "Downgrade clang-only errors that MSVC reports as warnings" ON)

set(_xwin_common
    "--target=${XWIN_TRIPLE}"
    "-fms-compatibility-version=${XWIN_MSVC_COMPAT_VERSION}"
    # /imsvc == "system include dir as if from %INCLUDE%": searched after -I/
    # target_include_directories and after clang's own resource headers
    # (so <intrin.h> etc. come from clang, as they must).
    "/imsvc" "${_xwin_crt_inc}"
    "/imsvc" "${_xwin_sdk_inc}/ucrt"
    "/imsvc" "${_xwin_sdk_inc}/um"
    "/imsvc" "${_xwin_sdk_inc}/shared"
    "/imsvc" "${_xwin_sdk_inc}/winrt")
if(XWIN_ASYNC_SEH)
    list(APPEND _xwin_common "-Xclang" "-fasync-exceptions")
endif()
list(JOIN _xwin_common " " _xwin_common_str)

set(_xwin_cxx_extra "")
if(XWIN_MSVC_DIAG_PARITY)
    # MSVC: C4838 (warning) for narrowing in aggregate init; clang: hard error.
    set(_xwin_cxx_extra " -Wno-error=c++11-narrowing")
endif()

# Toolchain *_INIT flags come first; Platform/Windows-MSVC.cmake then appends
# "/DWIN32 /D_WINDOWS" (+ "/GR /EHsc" for C++) exactly as with real MSVC.
set(CMAKE_C_FLAGS_INIT   "${_xwin_common_str}")
set(CMAKE_CXX_FLAGS_INIT "${_xwin_common_str}${_xwin_cxx_extra}")

# llvm-rc (only used if a .rc is built, e.g. the launcher): CMake preprocesses
# .rc files with the C compiler using these flags, then runs llvm-rc.
set(CMAKE_RC_FLAGS_INIT
    "/I${_xwin_crt_inc} /I${_xwin_sdk_inc}/ucrt /I${_xwin_sdk_inc}/um /I${_xwin_sdk_inc}/shared /I${_xwin_sdk_inc}/winrt")

# Same runtime selection as the top-level CMakeLists.txt (static CRT, /MT /MTd).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# CMake's own compiler/ABI checks link a test exe; run them in Release so they
# use non-debug CRT libs (xwin omits *d.lib unless --include-debug-libs).
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

# --------------------------------------------------------------- link flags ----
set(_xwin_libpaths
    "/libpath:${_xwin_crt_lib}"
    "/libpath:${_xwin_um_lib}"
    "/libpath:${_xwin_ucrt_lib}")

# Case-insensitive alias directory for libraries named with a casing that the
# splat does not provide (lld-link looks up names case-sensitively on Linux).
# xwin keeps the original SDK name plus lowercase + UPPERCASE aliases, so e.g.
# "Synchronization.lib" (top-level CMakeLists.txt) only resolves if the SDK
# file really is spelled that way. Fix-ups go to <build>/xwin-casefix.
set(_xwin_casefix_libs
    Synchronization d3d12 dxgi dxguid user32 kernel32 gdi32 winspool shell32
    ole32 oleaut32 uuid comdlg32 advapi32 comctl32 xinput ${XWIN_CASEFIX_LIBS})
set(_xwin_casefix_dir "${CMAKE_BINARY_DIR}/xwin-casefix")
set(_xwin_casefix_used FALSE)
foreach(_lib IN LISTS _xwin_casefix_libs)
    set(_found FALSE)
    foreach(_d IN ITEMS "${_xwin_um_lib}" "${_xwin_ucrt_lib}" "${_xwin_crt_lib}" "${_xwin_casefix_dir}")
        if(EXISTS "${_d}/${_lib}.lib")
            set(_found TRUE)
            break()
        endif()
    endforeach()
    if(_found)
        continue()
    endif()
    string(TOLOWER "${_lib}.lib" _want)
    foreach(_d IN ITEMS "${_xwin_um_lib}" "${_xwin_ucrt_lib}" "${_xwin_crt_lib}")
        file(GLOB _cands LIST_DIRECTORIES false "${_d}/*")
        foreach(_c IN LISTS _cands)
            get_filename_component(_cn "${_c}" NAME)
            string(TOLOWER "${_cn}" _cnl)
            if(_cnl STREQUAL _want)
                file(MAKE_DIRECTORY "${_xwin_casefix_dir}")
                file(CREATE_LINK "${_c}" "${_xwin_casefix_dir}/${_lib}.lib" SYMBOLIC)
                set(_found TRUE)
                break()
            endif()
        endforeach()
        if(_found)
            break()
        endif()
    endforeach()
endforeach()
if(EXISTS "${_xwin_casefix_dir}")
    list(APPEND _xwin_libpaths "/libpath:${_xwin_casefix_dir}")
endif()

list(JOIN _xwin_libpaths " " _xwin_libpaths_str)
foreach(_t EXE SHARED MODULE)
    set(CMAKE_${_t}_LINKER_FLAGS_INIT "${_xwin_libpaths_str}")
endforeach()

# PE "Subsystem" header field of DLLs. The Windows build uses the Visual Studio
# generator (CMakePresets.json), which writes <SubSystem>Console</SubSystem> for
# every non-WIN32 target, so an MSVC-built dxgi.dll says "Windows CUI". With no
# /subsystem flag lld-link (and link.exe under Ninja) default a DLL to "Windows
# GUI". The Windows loader ignores this field for DLLs (no behaviour change);
# this only keeps the header identical to the MSVC build. Entry point stays
# _DllMainCRTStartup (verified with lld-link 22).
option(XWIN_VS_SUBSYSTEM_PARITY "Link DLLs with /subsystem:console like the Visual Studio generator build" ON)
if(XWIN_VS_SUBSYSTEM_PARITY)
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " /subsystem:console")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT " /subsystem:console")
endif()

# Forward the knobs into try_compile projects (the toolchain is re-read there).
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    XWIN_SYSROOT XWIN_LLVM_ROOT XWIN_LLVM_SUFFIX XWIN_MSVC_COMPAT_VERSION
    XWIN_ASYNC_SEH XWIN_MSVC_DIAG_PARITY XWIN_CASEFIX_LIBS XWIN_VS_SUBSYSTEM_PARITY)

# Only search the host for programs; nothing in this project uses find_package.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# ------------------------------------------------------------------- notes ----
# * Manifests: with Ninja, CMake links through `cmake -E vs_link_dll`. For
#   Release (/INCREMENTAL:NO) it appends /MANIFEST:EMBED,ID=2, which lld-link
#   implements itself (default asInvoker manifest, no mt needed) - matching the
#   RT_MANIFEST #2 of an MSVC-built dxgi.dll. Debug (/INCREMENTAL) goes
#   through llvm-rc + llvm-mt instead (llvm-mt needs libxml2 support). If that
#   path causes trouble, add /manifest:no to CMAKE_SHARED_LINKER_FLAGS.
# * Exceptions/SEH: MSVC always lets __try/__except catch faults raised by
#   loads/stores inside the __try body. Plain clang only catches exceptions
#   raised by *calls* inside the __try ("LLVM does not model asynchronous
#   exceptions"); -fasync-exceptions (what clang-cl's /EHa enables) restores
#   MSVC behaviour. Most of the mod's __try blocks guard inline reads of
#   engine memory, so keep XWIN_ASYNC_SEH=ON unless clang miscompiles/crashes.
# * clang-cl already defaults to -fno-strict-aliasing (no TBAA) for MSVC
#   targets, and to MS bitfield/record layout, 64-bit long double, 16-bit
#   wchar_t - i.e. MSVC ABI.
