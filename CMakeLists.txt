# ─────────────────────────────────────────────────────────────────────────────
# OwOEsp — Linux-Build (CMake). Windows baut weiter über .vcxproj.
# Benötigt: cmake, g++, libglfw3-dev, libgl1-mesa-dev, libx11-dev,
#           libxtst-dev, libxext-dev, libcurl4-openssl-dev
# Bauen:  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
# ─────────────────────────────────────────────────────────────────────────────
cmake_minimum_required(VERSION 3.16)
project(OwOEsp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ── Performance (CachyOS/Arch: native Codegen + LTO; beides abschaltbar) ──────
# KEIN -ffast-math: der Code validiert isfinite()/NaN (Geometry, ViewMatrix) —
# Fast-Math würde diese Checks wegoptimieren.
option(OWO_NATIVE "Mit -march=native/-mtune=native bauen (CPU-spezifisch)" ON)
option(OWO_LTO    "Link-Time-Optimization (Interprocedural) aktivieren" ON)

if(NOT MSVC)
    add_compile_options(-O3 -fno-math-errno)
    if(OWO_NATIVE)
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-march=native" OWO_HAS_MARCH_NATIVE)
        if(OWO_HAS_MARCH_NATIVE)
            add_compile_options(-march=native -mtune=native)
            message(STATUS "OwOEsp: -march=native aktiviert")
        else()
            message(STATUS "OwOEsp: -march=native nicht unterstützt — generisch")
        endif()
    endif()
endif()

if(OWO_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT OWO_IPO_OK OUTPUT OWO_IPO_MSG)
    if(OWO_IPO_OK)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "OwOEsp: LTO aktiviert")
    else()
        message(STATUS "OwOEsp: LTO nicht verfügbar (${OWO_IPO_MSG})")
    endif()
endif()

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(X11 REQUIRED x11)
pkg_check_modules(XTST REQUIRED xtst)
pkg_check_modules(XEXT REQUIRED xext)
pkg_check_modules(CURL REQUIRED libcurl)

# Gemeinsame Include-Pfade (relative Includes wie "../core/Globals.h" + pch.h)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(IMGUI_CORE
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
)
set(IMGUI_BACKEND_GL
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_opengl3.cpp
)
set(CORE_SRC
    memory.cpp
    pch.cpp
    core/Globals.cpp
    core/Config.cpp
    core/Input.cpp
    core/OffsetsUpdater.cpp
    core/SigScan.cpp
)
set(UI_SRC
    ui/Menu.cpp
    ui/MenuWidgets.cpp
    ui/MenuModel.cpp
    ui/Theme.cpp
    ui/MenuAssets.cpp
)
set(FEATURES_SRC
    features/Reader.cpp
    features/MouseSmooth.cpp
    features/Chams.cpp
    features/RadarMap.cpp
    features/Bhop.cpp
    features/Triggerbot.cpp
    features/Aimbot.cpp
    features/CoordinateSaver.cpp
    features/Recoil.cpp
    features/ESP.cpp
    features/GrenadeVisuals.cpp
    features/Weapons.cpp
)

# ── Hauptcheat (externes Overlay) ─────────────────────────────────────────────
add_executable(OwOEsp
    app/App.cpp
    render/Renderer.cpp
    ${CORE_SRC}
    ${FEATURES_SRC}
    ${UI_SRC}
    ${IMGUI_CORE}
    ${IMGUI_BACKEND_GL}
)
target_include_directories(OwOEsp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${OPENGL_INCLUDE_DIR} ${GLFW_INCLUDE_DIRS} ${X11_INCLUDE_DIRS}
    ${XTST_INCLUDE_DIRS} ${XEXT_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS}
)
target_link_libraries(OwOEsp PRIVATE
    ${OPENGL_LIBRARIES} ${GLFW_LIBRARIES} ${X11_LIBRARIES}
    ${XTST_LIBRARIES} ${XEXT_LIBRARIES} ${CURL_LIBRARIES}
    Threads::Threads ${CMAKE_DL_LIBS}
)
target_compile_options(OwOEsp PRIVATE ${GLFW_CFLAGS_OTHER} ${X11_CFLAGS_OTHER})

# ── UI-Preview (ohne CS2, wie OwOEspPreview.vcxproj) ──────────────────────────
add_executable(OwOEspPreview
    preview_main.cpp
    memory.cpp
    pch.cpp
    core/Globals.cpp
    core/Config.cpp
    core/OffsetsUpdater.cpp
    ${UI_SRC}
    ${IMGUI_CORE}
    ${IMGUI_BACKEND_GL}
)
target_include_directories(OwOEspPreview PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${OPENGL_INCLUDE_DIR} ${GLFW_INCLUDE_DIRS} ${X11_INCLUDE_DIRS}
    ${XTST_INCLUDE_DIRS} ${XEXT_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS}
)
target_link_libraries(OwOEspPreview PRIVATE
    ${OPENGL_LIBRARIES} ${GLFW_LIBRARIES} ${X11_LIBRARIES}
    ${XTST_LIBRARIES} ${XEXT_LIBRARIES} ${CURL_LIBRARIES}
    Threads::Threads ${CMAKE_DL_LIBS}
)
target_compile_options(OwOEspPreview PRIVATE ${GLFW_CFLAGS_OTHER} ${X11_CFLAGS_OTHER})

# ── Laufzeitdaten neben die Binaries (wie PostBuild-xcopy unter Windows) ───────
foreach(d Gun_textures assets configs radar)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${d})
        add_custom_command(TARGET OwOEsp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/${d} $<TARGET_FILE_DIR:OwOEsp>/${d})
    endif()
endforeach()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/imgui.ini)
    add_custom_command(TARGET OwOEsp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/imgui.ini $<TARGET_FILE_DIR:OwOEsp>/imgui.ini)
endif()

message(STATUS "OwOEsp Linux-Build: ${CMAKE_BUILD_TYPE} (siehe README-LINUX.md)")
