cmake_minimum_required(VERSION 3.20)

# The tests on their own, built natively but 32-bit, for working on Linux:
# none of them needs Windows, and the DLL build (clang-cl against xwin's
# winsysroot) makes Windows executables that cannot run here.
#
#   cmake -S tests -B build-tests -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32
#   cmake --build build-tests && ctest --test-dir build-tests --output-on-failure

project(OblivionFrameFixesTests LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "The tests must be built 32-bit (clang++ with -m32): the mirrored engine structures hold for that pointer size.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

foreach(name FrameClockTest StreamCopyTest PluginEntryTest)
	add_executable(${name} ${name}.cpp ../src/PluginEntry.cpp)
	target_include_directories(${name} PRIVATE ../src)
	target_compile_options(${name} PRIVATE -Wall -Wextra)
	add_test(NAME ${name} COMMAND ${name})
endforeach()

add_executable(EntryDetourTest EntryDetourTest.cpp ../src/CodeWriter.cpp ../src/EntryDetour.cpp)
target_include_directories(EntryDetourTest PRIVATE ../src)
target_compile_options(EntryDetourTest PRIVATE -Wall -Wextra)
add_test(NAME EntryDetourTest COMMAND EntryDetourTest)

add_executable(VarLookupTest VarLookupTest.cpp)
target_include_directories(VarLookupTest PRIVATE ../src)
target_compile_options(VarLookupTest PRIVATE -Wall -Wextra)
add_test(NAME VarLookupTest COMMAND VarLookupTest)

add_executable(VarLookupStubTest VarLookupStubTest.cpp ../src/CodeWriter.cpp ../src/EntryDetour.cpp ../src/VarLookupStub.cpp)
target_include_directories(VarLookupStubTest PRIVATE ../src)
target_compile_options(VarLookupStubTest PRIVATE -Wall -Wextra)
add_test(NAME VarLookupStubTest COMMAND VarLookupStubTest)

add_executable(EngineVarTraceTest EngineVarTraceTest.cpp)
target_include_directories(EngineVarTraceTest PRIVATE ../src)
target_compile_options(EngineVarTraceTest PRIVATE -Wall -Wextra)
add_test(NAME EngineVarTraceTest COMMAND EngineVarTraceTest)
add_executable(CastCacheTest CastCacheTest.cpp)
target_include_directories(CastCacheTest PRIVATE ../src)
target_compile_options(CastCacheTest PRIVATE -Wall -Wextra)
add_test(NAME CastCacheTest COMMAND CastCacheTest)

add_executable(GainsTest GainsTest.cpp)
target_include_directories(GainsTest PRIVATE ../src)
target_compile_options(GainsTest PRIVATE -Wall -Wextra)
add_test(NAME GainsTest COMMAND GainsTest)

add_executable(ProtectTest ProtectTest.cpp)
target_include_directories(ProtectTest PRIVATE ../src)
target_compile_options(ProtectTest PRIVATE -Wall -Wextra)
add_test(NAME ProtectTest COMMAND ProtectTest)
