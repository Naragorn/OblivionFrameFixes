# Builds the real OblivionFrameFixes.dll on Linux: clang-cl and lld-link against Microsoft's
# C runtime, C++ standard library and Windows SDK, laid out as a winsysroot by
# xwin (https://github.com/Jake-Shadle/xwin).
#
# The same ABI, runtime library and build definition as the MSVC build on
# Windows. CMake sets MSVC for every compiler that takes cl's command line, so
# the MSVC branch of CMakeLists.txt applies here as well: /W4, the static
# runtime, the linker map. Releases stay with MSVC (.github/workflows/build.yml
# builds the DLL on every push); this is the build for working on Linux.
#
# One-time setup. The second command downloads Microsoft's files and accepts
# Microsoft's licence for them; they are not redistributable and stay out of
# the repository.
#
#   cargo install xwin --locked
#   xwin --accept-license --arch x86 --cache-dir ~/tools/xwin-cache splat --use-winsysroot-style --preserve-ms-arch-notation --output ~/tools/xwin
#
# Build:
#
#   cmake -S . -B build-linux-clangcl -G Ninja -DCMAKE_BUILD_TYPE=Release --toolchain cmake/toolchain-linux-clang-cl.cmake
#   cmake --build build-linux-clangcl
#
# FRAMEFIXES_WINSYSROOT, as a CMake or an environment variable, points somewhere
# other than ~/tools/xwin.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR x86)

if(NOT FRAMEFIXES_WINSYSROOT)
	if(DEFINED ENV{FRAMEFIXES_WINSYSROOT})
		set(FRAMEFIXES_WINSYSROOT "$ENV{FRAMEFIXES_WINSYSROOT}")
	else()
		set(FRAMEFIXES_WINSYSROOT "$ENV{HOME}/tools/xwin")
	endif()
endif()
if(NOT EXISTS "${FRAMEFIXES_WINSYSROOT}/VC/Tools/MSVC" OR NOT EXISTS "${FRAMEFIXES_WINSYSROOT}/Windows Kits/10/Lib")
	message(FATAL_ERROR
		"No winsysroot at ${FRAMEFIXES_WINSYSROOT}. Run xwin as the top of "
		"cmake/toolchain-linux-clang-cl.cmake describes, or set FRAMEFIXES_WINSYSROOT.")
endif()

# The _MSC_VER the compiler announces. Without a cl.exe to ask, clang-cl says
# 19.33; the C runtime xwin fetched is 14.44 (Visual Studio 17.14), the
# toolset the Windows build links with (its OblivionFrameFixes.dll names linker 14.44).
set(FRAMEFIXES_MSVC_COMPATIBILITY "19.44" CACHE STRING "clang-cl -fms-compatibility-version")

# CMake's compiler checks configure small projects of their own; they need the
# same values.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES FRAMEFIXES_WINSYSROOT FRAMEFIXES_MSVC_COMPATIBILITY)

find_program(FRAMEFIXES_CLANG_CL clang-cl REQUIRED)
find_program(FRAMEFIXES_LLD_LINK lld-link REQUIRED)
find_program(FRAMEFIXES_LLVM_LIB llvm-lib REQUIRED)
find_program(FRAMEFIXES_LLVM_RC llvm-rc REQUIRED)
find_program(FRAMEFIXES_LLVM_MT llvm-mt REQUIRED)

set(CMAKE_CXX_COMPILER "${FRAMEFIXES_CLANG_CL}")
set(CMAKE_CXX_COMPILER_TARGET i686-pc-windows-msvc)
set(CMAKE_LINKER "${FRAMEFIXES_LLD_LINK}")
set(CMAKE_AR "${FRAMEFIXES_LLVM_LIB}")
set(CMAKE_RC_COMPILER "${FRAMEFIXES_LLVM_RC}")
set(CMAKE_MT "${FRAMEFIXES_LLVM_MT}")

set(CMAKE_CXX_FLAGS_INIT "/winsysroot ${FRAMEFIXES_WINSYSROOT} -fms-compatibility-version=${FRAMEFIXES_MSVC_COMPATIBILITY}")

# /manifest:no: CMake would otherwise run mt over the linked DLL. The Windows
# build carries link.exe's default manifest - trustInfo at asInvoker, no
# assembly dependencies; this build carries none.
foreach(kind EXE SHARED MODULE)
	set(CMAKE_${kind}_LINKER_FLAGS_INIT "/winsysroot:${FRAMEFIXES_WINSYSROOT} /manifest:no")
endforeach()

# xwin leaves Microsoft's debug runtime out (it is not redistributable), so
# there is no msvcrtd.lib: CMake's compiler checks build in Release, and so
# the Windows build does (CMAKE_BUILD_TYPE=Release).
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
