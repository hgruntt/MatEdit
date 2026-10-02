cmake_minimum_required(VERSION 3.20)

project(MaterialEditor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")

set(STB_INCLUDE_DIR "${EXTERNAL_DIR}/stb")
if(NOT EXISTS "${STB_INCLUDE_DIR}/stb_image.h")
    include(FetchContent)
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    )
    FetchContent_MakeAvailable(stb)
    set(STB_INCLUDE_DIR "${stb_SOURCE_DIR}")
endif()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(
    "${EXTERNAL_DIR}/glfw"
    "${CMAKE_BINARY_DIR}/glfw"
)

add_library(glad STATIC
    "${EXTERNAL_DIR}/glad/src/glad.c"
)

target_include_directories(glad PUBLIC
    "${EXTERNAL_DIR}/glad/include"
)

add_library(imgui STATIC
    "${EXTERNAL_DIR}/imgui/imgui.cpp"
    "${EXTERNAL_DIR}/imgui/imgui_draw.cpp"
    "${EXTERNAL_DIR}/imgui/imgui_tables.cpp"
    "${EXTERNAL_DIR}/imgui/imgui_widgets.cpp"
    "${EXTERNAL_DIR}/imgui/backends/imgui_impl_glfw.cpp"
    "${EXTERNAL_DIR}/imgui/backends/imgui_impl_opengl3.cpp"
)

target_include_directories(imgui PUBLIC
    "${EXTERNAL_DIR}/imgui"
    "${EXTERNAL_DIR}/imgui/backends"
)

target_link_libraries(imgui PUBLIC
    glfw
)

add_library(glm INTERFACE)

target_include_directories(glm INTERFACE
    "${EXTERNAL_DIR}/glm"
)

add_library(gli INTERFACE)

target_include_directories(gli INTERFACE
    "${EXTERNAL_DIR}/gli"
)

add_executable(imgui_font_compressor
    "${EXTERNAL_DIR}/imgui/misc/fonts/binary_to_compressed_c.cpp"
)

set(EMBEDDED_FONT_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedUiFont.cpp")
add_custom_command(
    OUTPUT "${EMBEDDED_FONT_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
        "-DCOMPRESSOR=$<TARGET_FILE:imgui_font_compressor>"
        "-DFONT_FILE=${EXTERNAL_DIR}/imgui/misc/fonts/Roboto-Medium.ttf"
        "-DOUTPUT_FILE=${EMBEDDED_FONT_SOURCE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedFont.cmake"
    DEPENDS
        imgui_font_compressor
        "${EXTERNAL_DIR}/imgui/misc/fonts/Roboto-Medium.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedFont.cmake"
    VERBATIM
)

add_executable(MaterialEditor
    src/EditorUI.cpp
    src/MaterialSystem.cpp
    src/Config.cpp
    src/Shader.cpp
    src/ImageLoader.cpp
    src/Skybox.cpp
    src/PostProcess.cpp
    src/Contributors.cpp
    src/I18n.cpp
    src/main.cpp
    "${EMBEDDED_FONT_SOURCE}"
)

if(WIN32)
    target_sources(MaterialEditor PRIVATE
        resource.rc
    )
endif()

target_include_directories(MaterialEditor PRIVATE
    "${EXTERNAL_DIR}/glad/include"
    "${EXTERNAL_DIR}/imgui"
    "${EXTERNAL_DIR}/imgui/backends"
    "${EXTERNAL_DIR}/glm"
    "${EXTERNAL_DIR}/gli"
    "${STB_INCLUDE_DIR}"
)

    target_link_libraries(MaterialEditor PRIVATE
    glad
    imgui
    glm
    gli
    )

if(WIN32)
    target_compile_definitions(MaterialEditor PRIVATE
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
    target_link_libraries(MaterialEditor PRIVATE
        opengl32
        gdi32
        user32
        shell32
        imm32
        winhttp
    )
elseif(UNIX)
    target_link_libraries(MaterialEditor PRIVATE
        dl
        pthread
        m
    )
    target_link_options(MaterialEditor PRIVATE
        -static-libstdc++
        -static-libgcc
    )
endif()

if(MSVC)
    target_compile_options(MaterialEditor PRIVATE
        /W3
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(MaterialEditor PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(MaterialEditor PRIVATE
        $<$<CONFIG:Release>:-O2>
    )
endif()

set_target_properties(MaterialEditor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_custom_command(TARGET MaterialEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/CONTRIBUTORS.txt"
        "$<TARGET_FILE_DIR:MaterialEditor>/CONTRIBUTORS.txt"
)
