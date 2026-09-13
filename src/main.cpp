#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <gli/gli.hpp>
#include <cstring>
// Подключаем ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "cube_data.h"
#include "sphere_data.h"


namespace fs = std::filesystem;

// Глобальная или локальная переменная корневой директории игры
static fs::path gameRootPath = "";

// Структура для хранения настроек редактора
struct EditorConfig {
    std::string gamePath = "";
    std::string lastMatFile = "";
    int shapeType = 0;
    int lightMode = 0;
    bool useNormal = true;
    bool useGloss = true;
    bool useLuma = true;
};

// Загрузка расширенного конфига
void LoadConfig(EditorConfig& cfg) {
    std::ifstream file("editor_config.txt");
    if (!file.is_open()) return;

    std::getline(file, cfg.gamePath);
    std::getline(file, cfg.lastMatFile);
    
    std::string line;
    if (std::getline(file, line)) try { cfg.shapeType = std::stoi(line); } catch(...) {}
    if (std::getline(file, line)) try { cfg.lightMode = std::stoi(line); } catch(...) {}
    if (std::getline(file, line)) cfg.useNormal = (line == "1");
    if (std::getline(file, line)) cfg.useGloss = (line == "1");
    if (std::getline(file, line)) cfg.useLuma = (line == "1");
}

// Сохранение расширенного конфига
void SaveConfig(const EditorConfig& cfg) {
    std::ofstream file("editor_config.txt");
    if (!file.is_open()) return;

    file << cfg.gamePath << "\n";
    file << cfg.lastMatFile << "\n";
    file << cfg.shapeType << "\n";
    file << cfg.lightMode << "\n";
    file << (cfg.useNormal ? "1" : "0") << "\n";
    file << (cfg.useGloss ? "1" : "0") << "\n";
    file << (cfg.useLuma ? "1" : "0") << "\n";
}

// Простой класс для компиляции шейдеров
GLuint LoadShader(const char* vertexPath, const char* fragmentPath) {
    auto readShader = [](const char* path) {
        std::ifstream file(path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    };
    std::string vCode = readShader(vertexPath);
    std::string fCode = readShader(fragmentPath);
    const char* vSource = vCode.c_str();
    const char* fSource = fCode.c_str();

    GLuint v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, &vSource, NULL);
    glCompileShader(v);
    GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, &fSource, NULL);
    glCompileShader(f);

    GLuint program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    return program;
}

GLuint LoadDDSTexture(const std::string& path) {
    fs::path fullPath;
    if (fs::path(path).is_absolute()) {
        fullPath = fs::path(path);
    } else {
        fullPath = gameRootPath / path;
    }

    std::string ddsFilePath = fullPath.string() + ".dds";
    gli::texture texture = gli::load(ddsFilePath);
    if (texture.empty()) {
        std::cerr << "Failed to load DDS: " << ddsFilePath << std::endl;
        return 0;
    }
    gli::gl GL(gli::gl::PROFILE_GL33);
    gli::gl::format const format = GL.translate(texture.format(), texture.swizzles());

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(texture.levels() - 1));

    for (std::size_t level = 0; level < texture.levels(); ++level) {
        glCompressedTexImage2D(GL_TEXTURE_2D,
                               static_cast<GLint>(level),
                               GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM, // Используем sRGB формат
                               static_cast<GLsizei>(texture.extent(level).x),
                               static_cast<GLsizei>(texture.extent(level).y),
                               0,
                               static_cast<GLsizei>(texture.size(level)),
                               texture.data(0, 0, level));
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return textureID;
}

// Добавляем функцию загрузки кубмапы
GLuint LoadDDS_Cubemap(const std::string& path) {
    // Используем обычный load, он сам определит, что это cubemap
    gli::texture texture = gli::load(path + ".dds");
    if (texture.empty()) return 0;

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID); // Загружаем как 2D

    gli::gl GL(gli::gl::PROFILE_GL33);

    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t level = 0; level < texture.levels(); ++level) {
            glCompressedTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                   static_cast<GLint>(level),
                                   GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM,
                                   static_cast<GLsizei>(texture.extent(level).x),
                                   static_cast<GLsizei>(texture.extent(level).y),
                                   0,
                                   static_cast<GLsizei>(texture.size(level)),
                                   texture.data(face, 0, level));
        }
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Добавляем параметры обертки
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    
    // Проверка загрузки
    GLint width = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &width);
    std::cout << "Skybox positive X width: " << width << std::endl;

    return textureID;
}

// Добавляем функцию загрузки скайбокса как 2D-атласа
GLuint LoadSkyboxAs2D(const std::string& path) {
   fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::string ddsFilePath = fullPath.string() + ".dds";

    gli::texture texture = gli::load(ddsFilePath);
    if (texture.empty()) {
        std::cerr << "Failed to load Skybox DDS: " << ddsFilePath << std::endl;
        return 0;
    }

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    gli::gl GL(gli::gl::PROFILE_GL33);

    for (std::size_t level = 0; level < texture.levels(); ++level) {
        glCompressedTexImage2D(GL_TEXTURE_2D,
                               static_cast<GLint>(level),
                               GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM,
                               static_cast<GLsizei>(texture.extent(level).x),
                               static_cast<GLsizei>(texture.extent(level).y),
                               0,
                               static_cast<GLsizei>(texture.size(level)),
                               texture.data(0, 0, level));
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return textureID;
}

// Функция для динамической загрузки типов физических материалов из materials.def
std::vector<std::string> LoadPhysicalMaterialTypes() {
    std::vector<std::string> types;
    fs::path defPath = gameRootPath / "scripts" / "materials.def";
    if (!fs::exists(defPath)) {
        defPath = gameRootPath / "materials.def"; // fallback на корень игры
    }
    
    std::ifstream file(defPath);
    if (!file.is_open()) {
        return {"default"}; // Запасной вариант, если файла нет
    }

    std::string line;
    std::string lastLine;
    while (std::getline(file, line)) {
        if (line.find('{') != std::string::npos) {
            size_t n1 = lastLine.find('\"');
            size_t n2 = lastLine.find('\"', n1 + 1);
            if (n1 != std::string::npos && n2 != std::string::npos) {
                types.push_back(lastLine.substr(n1 + 1, n2 - n1 - 1));
            }
        } else {
            if (!line.empty()) lastLine = line;
        }
    }

    if (types.empty()) types.push_back("default");
    return types;
}

static std::vector<std::string> physicalMaterialTypes = {"default"};

struct Material {
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;
    std::map<std::string, GLuint> textures;

    char diffusePath[256] = "";
    char normalPath[256] = "";
    char glossPath[256] = "";
    char lumaPath[256] = "";
    char detailPath[256] = "";
    char detailScale[64] = "1 1";
    float smoothness = 0.0f;
    float reflectScale = 0.0f;
    float refractScale = 0.0f;
    float aberrationScale = 0.00f;
    float reliefScale = 0.00f;
    int swayHeight = 0;
    int matTypeIndex = 0; // Индекс в массиве типов
    void updateBuffers() {
        for(auto& p : params) {
            if(p.first == "diffuseMap") strncpy(diffusePath, p.second.c_str(), 255);
            if(p.first == "normalMap") strncpy(normalPath, p.second.c_str(), 255);
            if(p.first == "glossMap") strncpy(glossPath, p.second.c_str(), 255);
            if(p.first == "LumaMap") strncpy(lumaPath, p.second.c_str(), 255);
            if(p.first == "detailmap") strncpy(detailPath, p.second.c_str(), 255);
            if(p.first == "detailScale") strncpy(detailScale, p.second.c_str(), 63);
            if(p.first == "smoothness") try { smoothness = std::stof(p.second); } catch(...) {}
            if(p.first == "reflectScale") try { reflectScale = std::stof(p.second); } catch(...) {}
            if(p.first == "refractScale") try { refractScale = std::stof(p.second); } catch(...) {}
            if(p.first == "aberrationScale") try { aberrationScale = std::stof(p.second); } catch(...) {}
            if(p.first == "reliefScale") try { reliefScale = std::stof(p.second); } catch(...) {}
            if(p.first == "swayHeight") try { swayHeight = std::stoi(p.second); } catch(...) {}
            if(p.first == "material") {
                matTypeIndex = 0;
                for(size_t i = 0; i < physicalMaterialTypes.size(); i++) {
                    if(p.second == physicalMaterialTypes[i]) {
                        matTypeIndex = static_cast<int>(i);
                            break;
                    }
                }
            }   
        }
    }
    void syncParams() {
        auto setParam = [&](const std::string& key, const std::string& val) {
            bool found = false;
            for(auto& p : params) {
                if(p.first == key) {
                    p.second = val;
                    found = true;
                    break;
                }
            }
            if(!found) {
                params.push_back({key, val});
            }
        };

        setParam("diffuseMap", diffusePath);
        if(strlen(normalPath) > 0) setParam("normalMap", normalPath);
        if(strlen(glossPath) > 0) setParam("glossMap", glossPath);
        if(strlen(lumaPath) > 0) setParam("LumaMap", lumaPath);
        if(strlen(detailPath) > 0) setParam("detailmap", detailPath);
        setParam("detailScale", detailScale);
        setParam("smoothness", std::to_string(smoothness));
        setParam("reflectScale", std::to_string(reflectScale));
        setParam("refractScale", std::to_string(refractScale));
        setParam("aberrationScale", std::to_string(aberrationScale));
        setParam("reliefScale", std::to_string(reliefScale));
        setParam("swayHeight", std::to_string(swayHeight));
        
        if (matTypeIndex >= 0 && matTypeIndex < physicalMaterialTypes.size()) {
            setParam("material", physicalMaterialTypes[matTypeIndex]);
        }
    }

    void loadTextures() {
        for(auto const& [key, id] : textures) glDeleteTextures(1, &id);
        textures.clear();
        for(auto& p : params) {
            if(p.first == "diffuseMap") textures["diffuse"] = LoadDDSTexture(p.second);
            if(p.first == "normalMap") textures["normal"] = LoadDDSTexture(p.second);
            if(p.first == "glossMap") textures["gloss"] = LoadDDSTexture(p.second);
            if(p.first == "LumaMap") textures["luma"] = LoadDDSTexture(p.second);
        }
    }
};

void SaveAllMaterials(const std::string& path, const std::vector<Material>& materials) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ofstream file(fullPath);
    for (const auto& mat : materials) {
        file << "\"" << mat.name << "\"\n{\n";
        for (const auto& p : mat.params) {
            file << "\t\"" << p.first << "\"\t\"" << p.second << "\"\n";
        }
        file << "}\n";
    }
}
/*
LoadALLMaterials - Загрузчик материалов. Читает .mat построчно и заполняет массив материалов
*/

void LoadAllMaterials(const std::string& path, std::vector<Material>& materials) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ifstream file(fullPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open material file: " << fullPath << std::endl;
        return;
    }
    std::string line;
    std::string lastLine;
    Material* currentMat = nullptr;

    while (std::getline(file, line)) {
        if (line.find('{') != std::string::npos) {
            materials.emplace_back();
            currentMat = &materials.back();
            size_t n1 = lastLine.find('\"');
            size_t n2 = lastLine.find('\"', n1 + 1);
            if (n1 != std::string::npos && n2 != std::string::npos) {
                currentMat->name = lastLine.substr(n1 + 1, n2 - n1 - 1);
            } else {
                currentMat->name = "Unnamed";
            }
        } else if (line.find('}') != std::string::npos) {
            if (currentMat) currentMat->updateBuffers();
            currentMat = nullptr;
        } else if (currentMat) {
            size_t q1 = line.find('\"');
            if (q1 == std::string::npos) continue;
            size_t q2 = line.find('\"', q1 + 1);
            size_t q3 = line.find('\"', q2 + 1);
            if (q3 == std::string::npos) continue;
            size_t q4 = line.find('\"', q3 + 1);

            if (q1 != std::string::npos && q2 != std::string::npos && q3 != std::string::npos && q4 != std::string::npos) {
                currentMat->params.push_back({line.substr(q1 + 1, q2 - q1 - 1), line.substr(q3 + 1, q4 - q3 - 1)});
            }
        } else {
            if (!line.empty()) lastLine = line;
        }
    }
}
/*
MAIN - главгая функция программы
*/
int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
   
    EditorConfig editorCfg;
    LoadConfig(editorCfg);
    if (!editorCfg.gamePath.empty() && fs::exists(editorCfg.gamePath)) {
        gameRootPath = editorCfg.gamePath;
    } else {
        gameRootPath = fs::current_path();
    }

    // Настройка OpenGL
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1200, 900, "MaterialEditor", NULL, NULL);



    std::vector<Material> materials;
    std::vector<std::string> matFiles;
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    // Инициализация GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    auto refreshData = [&](std::string& currFile, int& currIndex) {
        materials.clear();
        matFiles.clear();
        
        physicalMaterialTypes = LoadPhysicalMaterialTypes();

        fs::path scriptsDir = gameRootPath / "scripts";
        if (fs::exists(scriptsDir) && fs::is_directory(scriptsDir)) {
            for (const auto& entry : fs::directory_iterator(scriptsDir)) {
                if (entry.path().extension() == ".mat") {
                    matFiles.push_back(fs::relative(entry.path(), gameRootPath).string());
                }
            }
        }
        
        if (!matFiles.empty()) {
            // Пытаемся найти последний открытый файл из конфига
            auto it = std::find(matFiles.begin(), matFiles.end(), editorCfg.lastMatFile);
            if (it != matFiles.end()) {
                currFile = *it;
            } else {
                currFile = matFiles[0];
            }
            
            LoadAllMaterials(currFile, materials);
            currIndex = 0;
            if (!materials.empty()) materials[currIndex].loadTextures();
        } else {
            currFile = "None";
        }
    };

    std::string currentFileName = "None";
    int currentMatIndex = 0;
    refreshData(currentFileName, currentMatIndex);

    GLuint shader = LoadShader("basic.vert", "basic.frag");

    Sphere sphere(32, 32);
    GLuint VAO_cube, VBO_cube, EBO_cube;
    glGenVertexArrays(1, &VAO_cube);
    glGenBuffers(1, &VBO_cube);
    glGenBuffers(1, &EBO_cube);
    glBindVertexArray(VAO_cube);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_cube);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_cube);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);

    GLuint VAO_sphere, VBO_sphere, EBO_sphere;
    glGenVertexArrays(1, &VAO_sphere);
    glGenBuffers(1, &VBO_sphere);
    glGenBuffers(1, &EBO_sphere);
    glBindVertexArray(VAO_sphere);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_sphere);
    glBufferData(GL_ARRAY_BUFFER, sphere.vertices.size() * sizeof(float), sphere.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_sphere);
    size_t indexBufferSize = sphere.indices.size() * sizeof(unsigned int);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexBufferSize, sphere.indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
    
    
    // --- Инициализация ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    auto SetupModernDarkStyle = []() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        // Закругления
        style.WindowRounding = 6.0f;
        style.ChildRounding = 4.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 9.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;

        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.ItemSpacing = ImVec2(8, 6);
        style.ItemInnerSpacing = ImVec2(6, 6);
        style.IndentSpacing = 20.0f;
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 10.0f;

        // Палитра цветов (Modern Dark / Accent Blue-Gray)
        colors[ImGuiCol_Text]                   = ImVec4(0.90f, 0.90f, 0.93f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.13f, 0.14f, 0.16f, 0.98f);
        colors[ImGuiCol_ChildBg]                = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.15f, 0.16f, 0.19f, 0.94f);
        colors[ImGuiCol_Border]                 = ImVec4(0.30f, 0.32f, 0.38f, 0.50f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.21f, 0.23f, 0.28f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.28f, 0.31f, 0.39f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.34f, 0.38f, 0.48f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.17f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.22f, 0.24f, 0.30f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.13f, 0.14f, 0.16f, 0.75f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.18f, 0.19f, 0.23f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.16f, 0.17f, 0.20f, 0.50f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.35f, 0.38f, 0.46f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.45f, 0.49f, 0.58f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.55f, 0.59f, 0.69f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.45f, 0.72f, 1.00f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.24f, 0.36f, 0.54f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.30f, 0.46f, 0.69f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.36f, 0.55f, 0.83f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.24f, 0.36f, 0.54f, 0.70f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.30f, 0.46f, 0.69f, 0.80f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.36f, 0.55f, 0.83f, 1.00f);
        colors[ImGuiCol_Separator]              = colors[ImGuiCol_Border];
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.35f, 0.65f, 1.00f, 0.78f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.35f, 0.65f, 1.00f, 0.20f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.35f, 0.65f, 1.00f, 0.67f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.35f, 0.65f, 1.00f, 0.95f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.18f, 0.20f, 0.25f, 0.86f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.30f, 0.46f, 0.69f, 0.80f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.24f, 0.36f, 0.54f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.15f, 0.16f, 0.19f, 0.97f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.19f, 0.22f, 0.28f, 1.00f);
        colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.35f, 0.65f, 1.00f, 0.35f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.20f, 0.20f, 0.22f, 0.35f);
    };

    SetupModernDarkStyle();
    
    // Загрузка скайбокса (как 2D атлас)
    GLuint skyboxID = LoadSkyboxAs2D("textures/sky");

    glm::vec3 albedo(1.0f, 0.0f, 0.0f);
    float metallic = 0.5f;
    float roughness = 0.5f;
    static float zoom = 2.0f;
    glUseProgram(shader);
    glUniform1i(glGetUniformLocation(shader, "diffuseMap"), 0);
    glUniform1i(glGetUniformLocation(shader, "normalMap"), 1);
    glUniform1i(glGetUniformLocation(shader, "glossMap"), 2);

    static int shapeType = 0;

   while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        // 1. Старт кадра ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Панель снизу
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)display_w, 100));
        ImGui::Begin("Game Root Directory:", nullptr,
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse);
                  // --- Добавляем выбор рабочей директории игры в ImGui ---
        static char gamePathBuffer[512];
        static bool initPathBuf = true;
        if (initPathBuf) {
            strncpy(gamePathBuffer, gameRootPath.string().c_str(), sizeof(gamePathBuffer) - 1);
            initPathBuf = false;
        }
        ImGui::InputText("##GameDir", gamePathBuffer, sizeof(gamePathBuffer));
        if (ImGui::Button("Set & Refresh")) {
            if (fs::exists(gamePathBuffer) && fs::is_directory(gamePathBuffer)) {
                gameRootPath = gamePathBuffer;
                editorCfg.gamePath = gameRootPath.string();
                SaveConfig(editorCfg);
                // Перезагружаем материалы из новой директории
                refreshData(currentFileName, currentMatIndex);
                // Перезагружаем скайбокс
                glDeleteTextures(1, &skyboxID);
                skyboxID = LoadSkyboxAs2D("textures/sky");
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(0, display_h - 200));
        ImGui::SetNextWindowSize(ImVec2((float)display_w/3, 200));
        ImGui::Begin("Material Files Data", nullptr,
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse);

        if (!matFiles.empty()) {
        if (ImGui::BeginCombo("Select .mat File", currentFileName.c_str())) {
            for (int n = 0; n < matFiles.size(); n++) {
                if (ImGui::Selectable(matFiles[n].c_str(), currentFileName == matFiles[n])) {
                    currentFileName = matFiles[n];
                        editorCfg.lastMatFile = currentFileName;
                                            materials.clear();
                                            LoadAllMaterials(currentFileName, materials);
                                            currentMatIndex = 0;
                                            if(!materials.empty()) materials[currentMatIndex].loadTextures();
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextColored(ImVec4(1,0,0,1), "No .mat files found!\nCheck Game Root Directory.");
        }

        if (!materials.empty() && currentMatIndex < materials.size()) {
            static char nameBuffer[128];
            // Инициализируем буфер при первом запуске
            static int lastMatIndex = -1;
            if (lastMatIndex != currentMatIndex) {
                strncpy(nameBuffer, materials[currentMatIndex].name.c_str(), 127);
                lastMatIndex = currentMatIndex;
            }
            if (ImGui::BeginCombo("Select Material", materials[currentMatIndex].name.c_str())) {
                for (int n = 0; n < materials.size(); n++) {
                    if (ImGui::Selectable(materials[n].name.c_str(), currentMatIndex == n)) {
                        currentMatIndex = n;
                        materials[currentMatIndex].loadTextures();
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::InputText("Material Name", nameBuffer, 128)) {
                materials[currentMatIndex].name = nameBuffer;
            }
            Material& mat = materials[currentMatIndex];
            if (ImGui::InputText("Diffuse Path", mat.diffusePath, 256)) {}
            ImGui::InputText("Normal", mat.normalPath, 256);
            ImGui::InputText("Gloss", mat.glossPath, 256);
            ImGui::InputText("Luma", mat.lumaPath, 256);
            ImGui::InputText("Detail", mat.detailPath, 256);
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(0+(float)display_w/3, display_h - 200));
        ImGui::SetNextWindowSize(ImVec2((float)display_w/3, 200));
        ImGui::Begin("Material Parameters", nullptr,
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse);

        if (!materials.empty() && currentMatIndex < materials.size()) {
            Material& mat = materials[currentMatIndex];
            ImGui::Text("Parameters");
            ImGui::SliderFloat("Smoothness", &mat.smoothness, 0.0f, 1.0f);
            ImGui::SliderFloat("Reflect", &mat.reflectScale, 0.0f, 1.0f);
            ImGui::SliderFloat("Refract", &mat.refractScale, 0.0f, 1.0f);
            ImGui::SliderFloat("Aberration", &mat.aberrationScale, 0.0f, 0.1f);
            ImGui::SliderFloat("Relief", &mat.reliefScale, 0.0f, 1.0f);
            ImGui::InputInt("SwayHeight", &mat.swayHeight);
            ImGui::InputText("Detail Scale", mat.detailScale, 64);
            std::vector<const char*> physMatPtrs;
            for (const auto& s : physicalMaterialTypes) {
                physMatPtrs.push_back(s.c_str());
            }

            if (ImGui::Combo("Phys Material", &mat.matTypeIndex, physMatPtrs.data(), static_cast<int>(physMatPtrs.size()))) {
                if (mat.matTypeIndex >= 0 && mat.matTypeIndex < physicalMaterialTypes.size()) {
                    for(auto& p : mat.params) {
                        if(p.first == "material") p.second = physicalMaterialTypes[mat.matTypeIndex];
                    }
                }
            }
            if (ImGui::Button("Apply Changes")) {
                for(auto& p : mat.params) {
                    if(p.first == "diffuseMap") p.second = mat.diffusePath;
                    if(p.first == "normalMap") p.second = mat.normalPath;
                    if(p.first == "glossMap") p.second = mat.glossPath;
                    if(p.first == "LumaMap") p.second = mat.lumaPath;
                    if(p.first == "detailmap") p.second = mat.detailPath;
                    if(p.first == "smoothness") p.second = std::to_string(mat.smoothness);
                    if(p.first == "reflectScale") p.second = std::to_string(mat.reflectScale);
                    if(p.first == "refractScale") p.second = std::to_string(mat.refractScale);
                    if(p.first == "aberrationScale") p.second = std::to_string(mat.aberrationScale);
                    if(p.first == "reliefScale") p.second = std::to_string(mat.reliefScale);
                    if(p.first == "swayHeight") p.second = std::to_string(mat.swayHeight);
                    if(p.first == "detailScale") p.second = mat.detailScale;
                    if(p.first == "material" && mat.matTypeIndex >= 0 && mat.matTypeIndex < physicalMaterialTypes.size()) {
                        p.second = physicalMaterialTypes[mat.matTypeIndex];
                    }
                }
                mat.syncParams();
                mat.loadTextures();
            }
            ImGui::SameLine();
            if (ImGui::Button("Save All")) {
               if (!materials.empty()) {
                    materials[currentMatIndex].syncParams();
                }
                SaveAllMaterials(currentFileName, materials);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add New Material")) {
                Material newMat;
                newMat.name = "NewMaterial_" + std::to_string(materials.size());
                newMat.params.push_back({"diffuseMap", "textures/default"});
                newMat.params.push_back({"normalMap", "textures/default_norm"});
                newMat.params.push_back({"smoothness", "1.0"});
                newMat.updateBuffers();

                materials.push_back(newMat);
                currentMatIndex = static_cast<int>(materials.size()) - 1;
            }
        } else {
            ImGui::Text("Select a valid material file.");
        }
        ImGui::Separator();
        ImGui::Combo("Model", &shapeType, "Cube\0Sphere\0");
        ImGui::End();
        
        ImGui::SetNextWindowPos(ImVec2(0+(float)display_w/3+(float)display_w/3, display_h - 200));
        ImGui::SetNextWindowSize(ImVec2((float)display_w/3, 200));
        ImGui::Begin("Light Parameters", nullptr,
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse);
        static bool useNormal = true;
        static bool useGloss = true;
        static bool useLuma = true;

        ImGui::Checkbox("Enable Normal Map", &useNormal);
        ImGui::Checkbox("Enable Gloss Map", &useGloss);
        ImGui::Checkbox("Enable Luma Map", &useLuma);

        ImGui::Separator();
        ImGui::Text("Light Settings");
        static int lightMode = 0;
        ImGui::Combo("Light Mode", &lightMode, "Camera\0Fixed\0");

        static glm::vec3 lightPos(2.0f, 2.0f, 2.0f);
        static float lightIntensity = 1.0f;
        static float lightColor[3] = {1.0f, 1.0f, 1.0f}; // Цвет света
        if (lightMode == 1) {
        ImGui::SliderFloat3("Light Position", &lightPos.x, -5.0f, 5.0f);
        }
        ImGui::SliderFloat("Light Intensity", &lightIntensity, 0.0f, 5.0f);
        ImGui::ColorEdit3("Light Color", lightColor);
        ImGui::End();
        ImGui::Render();

        // Вращение и Зум мышью
        static float yaw = 0.0f;
        static float pitch = 0.0f;
        static bool isDragging = false;
        static double lastX, lastY;

        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse) {
            // Вращение (ЛКМ)
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (!isDragging) {
                    isDragging = true;
                    glfwGetCursorPos(window, &lastX, &lastY);
                } else {
                    double currX, currY;
                    glfwGetCursorPos(window, &currX, &currY);
                    // Инвертируем или меняем знаки здесь, если нужно
                    yaw -= (float)(currX - lastX) * 0.01f;
                    pitch += (float)(currY - lastY) * 0.01f;

                    // Ограничиваем pitch, чтобы камера не "переворачивалась"
                    if (pitch > 1.5f) pitch = 1.5f;
                    if (pitch < -1.5f) pitch = -1.5f;

                    lastX = currX;
                    lastY = currY;
                }
            }
            // Зум (ПКМ)
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                double currX, currY;
                glfwGetCursorPos(window, &currX, &currY);
                if (!isDragging) {
                    isDragging = true;
                    lastX = currX; lastY = currY;
                } else {
                    zoom += (float)(currY - lastY) * 0.01f;
                    lastY = currY;
                }
            } else {
                isDragging = false;
            }

            // Ограничения зума
            if (zoom < 0.5f) zoom = 0.5f;
            if (zoom > 10.0f) zoom = 10.0f;
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        glUseProgram(shader);

        // Передаем скайбокс в шейдер
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, skyboxID);
        glUniform1i(glGetUniformLocation(shader, "skybox"), 4);

        // Орбитальная камера
        float camX = sin(yaw) * cos(pitch) * zoom;
        float camY = sin(pitch) * zoom;
        float camZ = cos(yaw) * cos(pitch) * zoom;
        glm::vec3 cameraPos = glm::vec3(camX, camY, camZ);

        // Режим освещения (свет следует за камерой или фиксирован)
        glm::vec3 finalLightPos = (lightMode == 0) ? cameraPos : lightPos;

        // Передаем viewPos и lightPos
        glUniform3fv(glGetUniformLocation(shader, "viewPos"), 1, &cameraPos.x);
        glUniform3fv(glGetUniformLocation(shader, "lightPos"), 1, &finalLightPos.x);
        glUniform1f(glGetUniformLocation(shader, "lightIntensity"), lightIntensity);
        glUniform3fv(glGetUniformLocation(shader, "lightColor"), 1, lightColor); // Передаем цвет
        glUniform3fv(glGetUniformLocation(shader, "albedo"), 1, &albedo.r);
        glUniform1f(glGetUniformLocation(shader, "metallic"), metallic);
        glUniform1f(glGetUniformLocation(shader, "roughness"), roughness);

        if (!materials.empty() && currentMatIndex >= 0 && currentMatIndex < materials.size()) {
            glUniform1f(glGetUniformLocation(shader, "reflectScale"), materials[currentMatIndex].reflectScale);
            glUniform1f(glGetUniformLocation(shader, "smoothness"), materials[currentMatIndex].smoothness);
        } else {
            glUniform1f(glGetUniformLocation(shader, "reflectScale"), 0.3f);
            glUniform1f(glGetUniformLocation(shader, "smoothness"), 1.0f);
        }

        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        float aspectRatio = (display_h > 0) ? (float)display_w / (float)display_h : 1.333f;
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);

        glViewport(0, 0, display_w, display_h);

        glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));

        glUniform1i(glGetUniformLocation(shader, "useDiffuse"), 0);
        glUniform1i(glGetUniformLocation(shader, "useNormal"), 0);
        glUniform1i(glGetUniformLocation(shader, "useGloss"), 0);
        glUniform1i(glGetUniformLocation(shader, "useLuma"), 0);

        if (!materials.empty()) {
            Material& mat = materials[currentMatIndex];
            if (mat.textures.count("diffuse") && mat.textures["diffuse"] != 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mat.textures["diffuse"]);
                glUniform1i(glGetUniformLocation(shader, "diffuseMap"), 0);
                glUniform1i(glGetUniformLocation(shader, "useDiffuse"), 1);
            }
            if (mat.textures.count("normal") && mat.textures["normal"] != 0) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, mat.textures["normal"]);
                glUniform1i(glGetUniformLocation(shader, "normalMap"), 1);
                glUniform1i(glGetUniformLocation(shader, "useNormal"), useNormal ? 1 : 0);
            }
            if (mat.textures.count("gloss")) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, mat.textures["gloss"]);
                glUniform1i(glGetUniformLocation(shader, "glossMap"), 2);
                glUniform1i(glGetUniformLocation(shader, "useGloss"), useGloss ? 1 : 0);
            }
            if (mat.textures.count("luma")) {
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, mat.textures["luma"]);
                glUniform1i(glGetUniformLocation(shader, "lumaMap"), 3);
                glUniform1i(glGetUniformLocation(shader, "useLuma"), useLuma ? 1 : 0);
            }
        }

        glUniform3fv(glGetUniformLocation(shader, "albedo"), 1, &albedo.r);
        glUniform1f(glGetUniformLocation(shader, "metallic"), metallic);
        glUniform1f(glGetUniformLocation(shader, "roughness"), roughness);
        glUniform3fv(glGetUniformLocation(shader, "lightPos"), 1, &finalLightPos.x);
        glUniform1f(glGetUniformLocation(shader, "lightIntensity"), lightIntensity);

        if(shapeType == 0) {
            glBindVertexArray(VAO_cube);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        } else {
            glBindVertexArray(VAO_sphere);
            glDrawElements(GL_TRIANGLES, (GLsizei)sphere.indices.size(), GL_UNSIGNED_INT, 0);
    }

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwTerminate();
    editorCfg.gamePath = gameRootPath.string();
    editorCfg.lastMatFile = currentFileName;
    SaveConfig(editorCfg);
    return 0;
}

