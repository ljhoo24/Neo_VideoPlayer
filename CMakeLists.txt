cmake_minimum_required(VERSION 3.20)

project(VideoPlayer
    VERSION   1.0.2
    LANGUAGES CXX
)

# ================================================================
# C++20
# ================================================================
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# Qt MOC / RCC / UIC — must be ON before find_package(Qt...)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ================================================================
# Qt6 (falls back to Qt5 if Qt6 is not found)
# ================================================================
find_package(Qt6 QUIET COMPONENTS Widgets Sql Network)

if(Qt6_FOUND)
    message(STATUS "VideoPlayer: using Qt ${Qt6_VERSION}")
    set(QT_LIBS Qt6::Widgets Qt6::Sql Qt6::Network)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets Sql Network)
    message(STATUS "VideoPlayer: using Qt ${Qt5_VERSION} (Qt6 not found)")
    set(QT_LIBS Qt5::Widgets Qt5::Sql Qt5::Network)
endif()

# ================================================================
# libmpv
#
# On Windows: download the dev package from
#   https://sourceforge.net/projects/mpv-player-windows/files/libmpv/
# then unzip to e.g. C:/tools/mpv-dev-x86_64  (the default below).
#
# Override via cmake -DMPV_ROOT=<your-path>
# ================================================================
if(NOT DEFINED MPV_ROOT)
    if(WIN32)
        set(MPV_ROOT "C:/tools/mpv-dev-x86_64"
            CACHE PATH "Root directory of the libmpv dev package")
    else()
        set(MPV_ROOT "/usr/local"
            CACHE PATH "Root directory of the libmpv dev package")
    endif()
endif()

find_path(MPV_INCLUDE_DIR
    NAMES mpv/client.h
    HINTS "${MPV_ROOT}/include"
    PATHS "/usr/include" "/usr/local/include"
    REQUIRED
)

# The shinchiro dev build ships mpv.lib (generated from libmpv-2.dll)
find_library(MPV_LIBRARY
    NAMES mpv libmpv
    HINTS "${MPV_ROOT}" "${MPV_ROOT}/lib" "${MPV_ROOT}/lib/x64"
    PATHS "/usr/lib" "/usr/local/lib"
    REQUIRED
)

message(STATUS "mpv include : ${MPV_INCLUDE_DIR}")
message(STATUS "mpv library : ${MPV_LIBRARY}")

# ================================================================
# Target
# ================================================================
set(SOURCES
    VideoPlayer/main.cpp
    VideoPlayer/MainWindow.cpp
    VideoPlayer/DatabaseManager.cpp
    VideoPlayer/MpvPlayerWidget.cpp
    VideoPlayer/PlaylistModel.cpp
    VideoPlayer/OptionsDialog.cpp
    VideoPlayer/IconFont.cpp
    VideoPlayer/resources/resources.qrc
)

if(WIN32)
    list(APPEND SOURCES VideoPlayer/app.rc)
endif()

set(HEADERS
    VideoPlayer/MainWindow.h
    VideoPlayer/DatabaseManager.h
    VideoPlayer/MpvPlayerWidget.h
    VideoPlayer/PlaylistModel.h
    VideoPlayer/OptionsDialog.h
    VideoPlayer/IconFont.h
)

# WIN32 subsystem = no console window in release builds
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${MPV_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/VideoPlayer
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${QT_LIBS}
        ${MPV_LIBRARY}
)

# ================================================================
# Windows post-build: copy mpv DLL next to the .exe
# ================================================================
if(WIN32)
    # Shinchiro builds name the DLL libmpv-2.dll
    find_file(MPV_DLL
        NAMES libmpv-2.dll mpv-2.dll mpv-1.dll mpv.dll
        HINTS "${MPV_ROOT}" "${MPV_ROOT}/bin"
        NO_DEFAULT_PATH
    )
    if(MPV_DLL)
        # The import library (mpv.lib) resolves to the DLL by its ORIGINAL
        # name — for shinchiro dev builds that is "libmpv-2.dll". Copy it
        # under that exact name so the Windows loader finds it next to the
        # exe; renaming it (e.g. to mpv-2.dll) makes startup fail with
        # 0xC0000135 unless an mpv DLL happens to be on PATH.
        get_filename_component(MPV_DLL_NAME "${MPV_DLL}" NAME)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MPV_DLL}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${MPV_DLL_NAME}"
            COMMENT "Copying ${MPV_DLL_NAME}…"
        )
        message(STATUS "mpv DLL      : ${MPV_DLL} -> ${MPV_DLL_NAME}")
    else()
        message(WARNING "mpv DLL not found in ${MPV_ROOT}/bin – "
                        "copy it manually next to the executable.")
    endif()

    # Deploy Qt DLLs with windeployqt (Qt6)
    if(Qt6_FOUND)
        find_program(WINDEPLOYQT windeployqt
            HINTS "${Qt6_DIR}/../../../bin")
        if(WINDEPLOYQT)
            # --compiler-runtime copies the MSVC runtime DLLs (vcruntime140*,
            # msvcp140*, …) next to the exe so the build runs on machines that
            # don't have the matching VC++ redistributable installed. Requires
            # the VS dev-environment vars (build from a Developer prompt).
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND "${WINDEPLOYQT}"
                        --no-translations
                        --compiler-runtime
                        "$<TARGET_FILE:${PROJECT_NAME}>"
                COMMENT "Running windeployqt…"
            )
        endif()
    endif()
endif()

# ================================================================
# Bundle GLSL shaders next to the exe
#
# The NVIDIA Image Scaling (NIS) shader is loaded at runtime by mpv
# via the `glsl-shaders` property. It needs to sit beside the exe in
# a `shaders/` subdirectory — see MpvPlayerWidget::applyUpscalingProfile
# for the lookup. Copy on every build so editing the shader source
# without re-running CMake still produces a fresh copy in the build
# tree.
# ================================================================
set(VP_SHADER_FILES
    VideoPlayer/shaders/NVScaler.glsl
)

foreach(_shader ${VP_SHADER_FILES})
    get_filename_component(_shader_name "${_shader}" NAME)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/${_shader}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders/${_shader_name}"
        COMMENT "Copying shader: ${_shader_name}"
    )
endforeach()
