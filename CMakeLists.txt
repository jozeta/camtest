cmake_minimum_required(VERSION 3.20)
project(camtest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statically linked CRT, no VC++ Redistributable needed -- ships as two
# portable files (camtest.exe + camtest_filter.dll) that must sit together.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)

# ---------------- vendored DirectShow BaseClasses (Microsoft, MIT) ----------------
file(GLOB BASECLASSES_SOURCES
    "${CMAKE_SOURCE_DIR}/third_party/baseclasses/*.cpp"
)
list(FILTER BASECLASSES_SOURCES EXCLUDE REGEX ".*dllentry\\.cpp$")
list(FILTER BASECLASSES_SOURCES EXCLUDE REGEX ".*dllsetup\\.cpp$")

add_library(baseclasses STATIC ${BASECLASSES_SOURCES})
target_include_directories(baseclasses PUBLIC "${CMAKE_SOURCE_DIR}/third_party/baseclasses")

# ---------------- camtest_filter.dll: the actual DirectShow in-proc COM server ----------------
# Consumers (Teams, browsers, the OS Frame Server, ...) CoCreateInstance our
# filter CLSIDs, which LoadLibrary's whatever InProcServer32 points at. That
# must be a real DLL: LoadLibrary invokes the module's entry point under
# DLL_PROCESS_ATTACH semantics regardless of what kind of PE it is, and an
# EXE's entry point (WinMainCRTStartup) doesn't expect that -- it re-runs full
# CRT+app startup, which for a GUI app means re-entering wWinMain and its
# modal message loop inside the caller's process, blowing the stack. A real
# DLL with a real (trivial) DllMain sidesteps this entirely.
set(CAMTEST_FILTER_SOURCES
    src/VCamSlots.cpp
    src/MediaSource/DllMain.cpp
    src/MediaSource/ClassFactory.cpp
    src/MediaSource/CamtestSourceFilter.cpp
    src/MediaSource/CamtestOutputPin.cpp
    src/Imaging/ImageLoader.cpp
    src/Imaging/Letterbox.cpp
    src/Imaging/Rgb24Convert.cpp
    src/Ipc/NamedPipeClient.cpp
)

add_library(camtest_filter SHARED ${CAMTEST_FILTER_SOURCES})
target_compile_definitions(camtest_filter PRIVATE NOMINMAX)
# Ninja (unlike the Visual Studio generator) does NOT automatically turn a
# .def source into a linker /DEF: flag, so it must be passed explicitly --
# without this the DLL silently has zero exports and every consumer's
# CoCreateInstance fails with CO_E_ERRORINDLL.
target_link_options(camtest_filter PRIVATE "/DEF:${CMAKE_SOURCE_DIR}/src/MediaSource/camtest_filter.def")
target_include_directories(camtest_filter PRIVATE "${CMAKE_SOURCE_DIR}/third_party/baseclasses")
target_link_libraries(camtest_filter PRIVATE
    baseclasses
    strmiids
    windowscodecs
    ole32
    oleaut32
    uuid
    winmm
)
set_target_properties(camtest_filter PROPERTIES OUTPUT_NAME "camtest_filter")

# ---------------- camtest.exe: GUI configurator only ----------------
# No longer a COM server of any kind -- just drives registration (pointing
# InProcServer32 at camtest_filter.dll) and serves frames to it over the
# per-slot named pipes.
set(CAMTEST_SOURCES
    src/main.cpp
    src/AppConfig.cpp
    src/VCamSlots.cpp
    src/Gui/MainDialog.cpp
    src/Engine/VCamEngine.cpp
    src/Ipc/NamedPipeServer.cpp
    resources/camtest.rc
)

add_executable(camtest WIN32 ${CAMTEST_SOURCES})
# The .rc file embeds its own manifest (resource ID 1); disable the linker's
# default auto-generated manifest to avoid a duplicate RT_MANIFEST resource.
target_link_options(camtest PRIVATE "/MANIFEST:NO")
target_link_libraries(camtest PRIVATE
    strmiids
    ole32
    oleaut32
    uuid
    comctl32
    shell32
    comdlg32
)

set_target_properties(camtest PROPERTIES OUTPUT_NAME "camtest")
add_dependencies(camtest camtest_filter)
