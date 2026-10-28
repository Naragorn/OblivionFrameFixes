cmake_minimum_required(VERSION 3.20)
project(OblivionFrameFixes CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "Oblivion is 32-bit: configure with a 32-bit toolchain (vcvars32.bat, -A Win32, or cmake/toolchain-linux-clang-cl.cmake).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static runtime: the plugin carries no dependency on an installed VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_options(/W4 /permissive- /utf-8)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

# The pure decisions, process-free, so the tests run them on any machine.
add_library(logic INTERFACE)
target_include_directories(logic INTERFACE src)

add_library(detour STATIC src/CodeWriter.cpp src/EntryDetour.cpp src/VarLookupStub.cpp)
target_include_directories(detour PUBLIC src)

add_library(patches STATIC src/Memory.cpp src/FrameClock.cpp src/StreamCopy.cpp src/VarLookup.cpp src/EngineVarTrace.cpp
            src/CastCache.cpp src/Gains.cpp src/FastProtect.cpp)
target_include_directories(patches PUBLIC src)
target_link_libraries(patches PUBLIC detour)

add_library(plugin_entry STATIC src/PluginEntry.cpp)
target_include_directories(plugin_entry PUBLIC src)

add_library(OblivionFrameFixes SHARED src/Plugin.cpp)
target_link_libraries(OblivionFrameFixes PRIVATE plugin_entry patches)

enable_testing()

add_executable(FrameClockTest tests/FrameClockTest.cpp)
target_link_libraries(FrameClockTest PRIVATE logic)
add_test(NAME FrameClockTest COMMAND FrameClockTest)

add_executable(StreamCopyTest tests/StreamCopyTest.cpp)
target_link_libraries(StreamCopyTest PRIVATE logic)
add_test(NAME StreamCopyTest COMMAND StreamCopyTest)

add_executable(EntryDetourTest tests/EntryDetourTest.cpp)
target_link_libraries(EntryDetourTest PRIVATE detour)
add_test(NAME EntryDetourTest COMMAND EntryDetourTest)

add_executable(VarLookupTest tests/VarLookupTest.cpp)
target_link_libraries(VarLookupTest PRIVATE logic)
add_test(NAME VarLookupTest COMMAND VarLookupTest)

add_executable(VarLookupStubTest tests/VarLookupStubTest.cpp)
target_link_libraries(VarLookupStubTest PRIVATE detour)
add_test(NAME VarLookupStubTest COMMAND VarLookupStubTest)

add_executable(PluginEntryTest tests/PluginEntryTest.cpp)
target_link_libraries(PluginEntryTest PRIVATE plugin_entry)
add_test(NAME PluginEntryTest COMMAND PluginEntryTest)

add_executable(EngineVarTraceTest tests/EngineVarTraceTest.cpp)
target_link_libraries(EngineVarTraceTest PRIVATE logic)
add_test(NAME EngineVarTraceTest COMMAND EngineVarTraceTest)
add_executable(CastCacheTest tests/CastCacheTest.cpp)
target_link_libraries(CastCacheTest PRIVATE logic)
add_test(NAME CastCacheTest COMMAND CastCacheTest)
add_executable(GainsTest tests/GainsTest.cpp)
target_link_libraries(GainsTest PRIVATE logic)
add_test(NAME GainsTest COMMAND GainsTest)
add_executable(ProtectTest tests/ProtectTest.cpp)
target_link_libraries(ProtectTest PRIVATE logic)
add_test(NAME ProtectTest COMMAND ProtectTest)
