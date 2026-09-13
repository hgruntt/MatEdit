cmake_minimum_required(VERSION 3.10)
project(MaterialEditor)

set(CMAKE_CXX_STANDARD 17)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED) 
find_package(gli CONFIG REQUIRED)
add_executable(MaterialEditor
    src/main.cpp
    src/cube_data.h
    src/sphere_data.h
    resource.rc 
)

# Подключаем библиотеки
target_link_libraries(MaterialEditor PRIVATE glfw glad::glad imgui::imgui glm::glm gli)

