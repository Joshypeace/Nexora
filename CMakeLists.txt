cmake_minimum_required(VERSION 3.21)

project(Omnipane VERSION 1.0.0 DESCRIPTION "A minimal floating application launcher for Windows" LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Omnipane is a Windows-only application.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# GuiPrivate provides QWindowsWindow::setCustomMargins, used to extend the client
# area over the native frame while keeping DWM shadows and acrylic (see WindowEffects.cpp).
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets GuiPrivate)
qt_standard_project_setup()

option(OMNIPANE_BUILD_TESTS "Build the Omnipane test suite" ON)

# Compiler settings shared by the app, its core library and the tests.
function(omnipane_configure_target target)
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN NOMINMAX
        _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A00000C
        OMNIPANE_VERSION="${PROJECT_VERSION}"
    )
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
    endif()
endfunction()

# Everything except main(), so the test suite can link the real components.
qt_add_library(OmnipaneCore STATIC
    src/AppEntry.h
    src/ComApartment.h
    src/AppCache.h            src/AppCache.cpp
    src/AppDiscovery.h        src/AppDiscovery.cpp
    src/DiscoveryService.h    src/DiscoveryService.cpp
    src/AppModel.h            src/AppModel.cpp
    src/AppDelegate.h         src/AppDelegate.cpp
    src/AppLauncher.h         src/AppLauncher.cpp
    src/GlobalHotkey.h        src/GlobalHotkey.cpp
    src/Settings.h            src/Settings.cpp
    src/WindowEffects.h       src/WindowEffects.cpp
    src/DimOverlay.h          src/DimOverlay.cpp
    src/Spinner.h             src/Spinner.cpp
    src/LauncherWindow.h      src/LauncherWindow.cpp
    src/OmnipaneApp.h           src/OmnipaneApp.cpp
    src/ShortcutDialog.h      src/ShortcutDialog.cpp
    src/Theme.h               src/Theme.cpp
    src/FolderWatcher.h       src/FolderWatcher.cpp
)
# INTERFACE only: on the library itself, src/ on the include path would make the
# Windows SDK's <appmodel.h> resolve to our AppModel.h (case-insensitive filesystem).
target_include_directories(OmnipaneCore INTERFACE src)

qt_add_resources(OmnipaneCore "omnipane_assets"
    PREFIX "/"
    BASE resources
    FILES
        resources/logo.png
        resources/background.jpg
)

omnipane_configure_target(OmnipaneCore)
target_link_libraries(OmnipaneCore PUBLIC
    Qt6::Widgets
    Qt6::GuiPrivate
    dwmapi shell32 ole32 shlwapi version user32 uuid advapi32
)

qt_add_executable(Omnipane WIN32
    src/main.cpp
    resources/omnipane.rc
    resources/omnipane.manifest
)
omnipane_configure_target(Omnipane)
target_link_libraries(Omnipane PRIVATE OmnipaneCore)

if(OMNIPANE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Gathers the Qt runtime next to the executable so it runs outside a Qt-enabled shell.
get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

set(OMNIPANE_DIST_DIR "${CMAKE_SOURCE_DIR}/dist/Omnipane" CACHE PATH "Folder receiving the runnable Omnipane build")

if(WINDEPLOYQT_EXECUTABLE)
    add_custom_target(deploy
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${OMNIPANE_DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OMNIPANE_DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:Omnipane>" "${OMNIPANE_DIST_DIR}"
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw
                --no-compiler-runtime --skip-plugin-types tls,networkinformation,generic,qmltooling
                "${OMNIPANE_DIST_DIR}/Omnipane.exe"
        DEPENDS Omnipane
        COMMENT "Deploying Omnipane and the Qt runtime to ${OMNIPANE_DIST_DIR}"
        VERBATIM)
endif()

install(TARGETS Omnipane RUNTIME DESTINATION .)

# Distribution packages, written to dist/: a portable zip and a per-user MSI (WiX v3).
set(OMNIPANE_PACKAGE_DIR "${CMAKE_SOURCE_DIR}/dist")

if(TARGET deploy)
    add_custom_target(portable
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${OMNIPANE_PACKAGE_DIR}/Omnipane-${PROJECT_VERSION}-portable-x64.zip"
                --format=zip Omnipane
        WORKING_DIRECTORY "${OMNIPANE_PACKAGE_DIR}"
        DEPENDS deploy
        COMMENT "Creating the portable zip"
        VERBATIM)
endif()

find_program(WIX_CANDLE_EXECUTABLE candle
    HINTS "$ENV{WIX}/bin" "C:/Program Files (x86)/WiX Toolset v3.14/bin" "C:/Program Files (x86)/WiX Toolset v3.11/bin")

if(TARGET deploy AND WIX_CANDLE_EXECUTABLE)
    get_filename_component(_wix_bin "${WIX_CANDLE_EXECUTABLE}" DIRECTORY)
    add_custom_target(installer
        COMMAND "${CMAKE_COMMAND}"
                -DDIST_DIR=${OMNIPANE_DIST_DIR}
                -DOUT_DIR=${CMAKE_BINARY_DIR}/installer
                -DVERSION=${PROJECT_VERSION}
                -DWIX_BIN=${_wix_bin}
                -DICON=${CMAKE_SOURCE_DIR}/resources/omnipane.ico
                -P "${CMAKE_SOURCE_DIR}/packaging/make_installer.cmake"
        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_BINARY_DIR}/installer/Omnipane-${PROJECT_VERSION}-x64.msi"
                "${OMNIPANE_PACKAGE_DIR}"
        DEPENDS deploy
        COMMENT "Building the per-user MSI installer"
        VERBATIM)
else()
    message(STATUS "WiX Toolset v3 not found; the 'installer' target is unavailable")
endif()
