# MaterialEditor

## ENG:

**MaterialEditor** is a modern desktop utility designed for visualizing, creating, and editing game materials and physical definitions (`.mat`, `.def`) for projects based on Xash3D / Xash3D FWGS (such as PrimeXT). Written in C++17 utilizing OpenGL 3.3, GLFW, GLM, GLI, and Dear ImGui.

---

## Features

- **PBR Rendering Pipeline**: Real-time visualization of materials with support for albedo, metallic, roughness, smooth/gloss, and reflections.
- **Texture Maps Support**:
  - Diffuse / Albedo
  - Normal Map (TBN space calculation)
  - Gloss / Specular / AO
  - Luma Map (Self-illumination)
  - Bump / Height Map (Parallax / Bump mapping with adjustable relief scale)
- **Skybox Environment**: Loading and rendering DDS cubemaps for realistic reflections and background.
- **Material & Physics Management**:
  - Full parsing and saving of visual material scripts (`.mat`).
  - Editing physical material definitions (`scripts/materials.def`), including step sounds, impact sounds, and impact decals.
- **Interactive 3D Viewport**:
  - Multiple test primitives (Cube, Sphere, Plane, Cylinder, Cone, Torus, Newell Teapot).
  - Orbit camera controls, zoom, and flexible lighting modes (Camera-following, Fixed, Dynamic).
- **Embedded Shaders**: Core rendering shaders compiled directly from memory, requiring no external shader files next to the executable.

---

## Project Structure

- `src/main.cpp` — Application entry point, main render loop, camera logic, and frame-rate limiter.
- `src/MaterialSystem.cpp / .h` — Material parsing, WAD texture loading, and physical properties handling.
- `src/EditorUI.cpp / .h` — User interface panels, file browser, texture preview, and document editors built with Dear ImGui.
- `src/Shader.cpp / .h` & `src/ShadersSource.h` — In-memory shader compilation and OpenGL program management.
- `src/Config.cpp / .h` — Editor configuration management (`editor_config.txt`).
- `src/shaders/` — Reference shader sources.
- `external/` — Third-party libraries (GLFW, Glad, Dear ImGui, GLM, GLI).

---

## Build Instructions

Requirements:
- CMake 3.20 or higher
- C++17 compatible compiler (MSVC, GCC, Clang)

1. Clone the repository with submodules:
   ```bash
   git clone --recursive <repository-url>
   cd MaterialEditor
   ```
2. Create a build directory and configure the project:
   ```bash
   mkdir build
   cd build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   ```
3. Build the project:
   ```bash
   cmake --build . --config Release
   ```
4. The executable will be generated in the `build/` directory.

---

## License

Distributed under the GPL-3.0 License. See `LICENSE` for details.

---
# RU:

**MaterialEditor** — это современный инструмент для визуализации, создания и редактирования игровых материалов и физических определений (`.mat`, `.def`) для проектов на базе Xash3D / Xash3D FWGS (например, PrimeXT). Программа написана на C++17 с использованием OpenGL 3.3, GLFW, GLM, GLI и Dear ImGui.

---

## Основные возможности

- **PBR-пайплайн рендеринга**: Визуализация материалов в реальном времени с поддержкой альбедо, металличности, шероховатости, глянца и отражений.
- **Поддержка текстурных карт**:
  - Диффузная текстура / Альбедо
  - Карта нормалей (с расчетом TBN-базиса)
  - Глянец / Блик / AO
  - Карта свечения (Luma)
  - Карта высот/рельефа (Bump / Parallax mapping с настраиваемой глубиной)
- **Окружение (Skybox)**: Загрузка и рендеринг DDS кубических карт для реалистичных отражений и фона.
- **Управление материалами и физикой**:
  - Полный парсинг и сохранение файлов визуальных материалов (`.mat`).
  - Редактирование физических свойств материалов (`scripts/materials.def`), включая звуки шагов, звуки ударов и декали.
- **Интерактивная 3D-сцена**:
  - Набор тестовых примитивов (Куб, Сфера, Плоскость, Цилиндр, Конус, Тор, Чайник).
  - Орбитальная камера с зумом и гибкие режимы освещения (следует за камерой, зафиксирован, динамический).
- **Встроенные шейдеры**: Основные шейдеры компилируются прямо из памяти, поэтому программа не требует наличия внешних файлов шейдеров рядом с исполняемым файлом.

---

## Структура проекта

- `src/main.cpp` — Точка входа, главный цикл отрисовки, логика камеры и ограничение кадровой частоты.
- `src/MaterialSystem.cpp / .h` — Парсинг материалов, загрузка текстур из WAD-архивов и обработка физических свойств.
- `src/EditorUI.cpp / .h` — Пользовательский интерфейс: панели, файловый обозреватель, предпросмотр текстур и текстовые редакторы.
- `src/Shader.cpp / .h` & `src/ShadersSource.h` — Компиляция шейдеров из памяти и управление программами OpenGL.
- `src/Config.cpp / .h` — Управление конфигурацией редактора (`editor_config.txt`).
- `src/shaders/` — Исходные тексты шейдеров.
- `external/` — Сторонние библиотеки (GLFW, Glad, Dear ImGui, GLM, GLI).

---

## Сборка проекта

Требования:
- CMake 3.20 или выше
- Компилятор с поддержкой C++17 (MSVC, GCC, Clang)

1. Клонируйте репозиторий вместе с подмодулями:
   ```bash
   git clone --recursive <repository-url>
   cd MaterialEditor
   ```
2. Создайте директорию сборки и сгенерируйте проект:
   ```bash
   mkdir build
   cd build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   ```
3. Выполните сборку:
   ```bash
   cmake --build . --config Release
   ```
4. Исполняемый файл будет создан в папке `build/`.

---

## Лицензия

Проект распространяется под лицензией GPL-3.0. Подробности см. в файле `LICENSE`.
