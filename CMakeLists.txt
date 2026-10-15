cmake_minimum_required(VERSION 3.21)

project(Nexora VERSION 1.0.0 DESCRIPTION "A minimal floating application launcher for Windows" LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Nexora is a Windows-only application.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# GuiPrivate provides QWindowsWindow::setCustomMargins, used to extend the client
# area over the native frame while keeping DWM shadows and acrylic (see WindowEffects.cpp).
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets GuiPrivate)
qt_standard_project_setup()

qt_add_executable(Nexora WIN32
    src/main.cpp
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
    src/NexoraApp.h           src/NexoraApp.cpp
    src/ShortcutDialog.h      src/ShortcutDialog.cpp
    resources/nexora.rc
    resources/nexora.manifest
)

qt_add_resources(Nexora "nexora_assets"
    PREFIX "/"
    BASE resources
    FILES
        resources/logo.png
        resources/background.jpg
)

target_compile_definitions(Nexora PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A00000C
    NEXORA_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(Nexora PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
endif()

target_link_libraries(Nexora PRIVATE
    Qt6::Widgets
    Qt6::GuiPrivate
    dwmapi shell32 ole32 shlwapi version user32 uuid advapi32
)

# Gathers the Qt runtime next to the executable so it runs outside a Qt-enabled shell.
get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

set(NEXORA_DIST_DIR "${CMAKE_SOURCE_DIR}/dist/Nexora" CACHE PATH "Folder receiving the runnable Nexora build")

if(WINDEPLOYQT_EXECUTABLE)
    add_custom_target(deploy
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${NEXORA_DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${NEXORA_DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:Nexora>" "${NEXORA_DIST_DIR}"
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw
                --no-compiler-runtime --skip-plugin-types tls,networkinformation,generic,qmltooling
                "${NEXORA_DIST_DIR}/Nexora.exe"
        DEPENDS Nexora
        COMMENT "Deploying Nexora and the Qt runtime to ${NEXORA_DIST_DIR}"
        VERBATIM)
endif()

install(TARGETS Nexora RUNTIME DESTINATION .)
