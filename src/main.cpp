#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <utility>
#include <chrono>
#include <thread>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Config.h"
#include "MaterialSystem.h"
#include "Shader.h"
#include "EditorUI.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "cube_data.h"
#include "sphere_data.h"

struct MeshData {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
};

#include "builtin_models.h"

struct MeshGPU {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;

    void upload(const MeshData& mesh) {
        indexCount = static_cast<GLsizei>(mesh.indices.size());
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(float)),
                     mesh.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(unsigned int)),
                     mesh.indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }

    void draw() const {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    }

    void release() {
        if (ebo) glDeleteBuffers(1, &ebo);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        vao = vbo = ebo = 0;
        indexCount = 0;
    }
};

static void addVertex(MeshData& mesh, const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv) {
    mesh.vertices.push_back(p.x);
    mesh.vertices.push_back(p.y);
    mesh.vertices.push_back(p.z);
    mesh.vertices.push_back(n.x);
    mesh.vertices.push_back(n.y);
    mesh.vertices.push_back(n.z);
    mesh.vertices.push_back(uv.x);
    mesh.vertices.push_back(uv.y);
}

static void addTri(MeshData& mesh, unsigned int a, unsigned int b, unsigned int c) {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
}

static unsigned int vertexCount(const MeshData& mesh) {
    return static_cast<unsigned int>(mesh.vertices.size() / 8);
}

static void appendPlane(MeshData& mesh, float size, int divisions) {
    const unsigned int base = vertexCount(mesh);
    for (int y = 0; y <= divisions; ++y) {
        float v = static_cast<float>(y) / divisions;
        float z = (v - 0.5f) * size;
        for (int x = 0; x <= divisions; ++x) {
            float u = static_cast<float>(x) / divisions;
            float px = (u - 0.5f) * size;
            addVertex(mesh, glm::vec3(px, 0.0f, z), glm::vec3(0, 1, 0), glm::vec2(u, v));
        }
    }
    int row = divisions + 1;
    for (int y = 0; y < divisions; ++y) {
        for (int x = 0; x < divisions; ++x) {
            unsigned int a = base + static_cast<unsigned int>(y * row + x);
            unsigned int b = a + 1;
            unsigned int c = a + static_cast<unsigned int>(row);
            unsigned int d = c + 1;
            addTri(mesh, a, b, c);
            addTri(mesh, b, d, c);
        }
    }
}

static void appendCylinder(MeshData& mesh, float radius, float height, int slices) {
    const unsigned int base = vertexCount(mesh);
    for (int y = 0; y <= 1; ++y) {
        float py = (static_cast<float>(y) - 0.5f) * height;
        for (int x = 0; x <= slices; ++x) {
            float u = static_cast<float>(x) / slices;
            float a = u * glm::two_pi<float>();
            glm::vec3 n(std::cos(a), 0, std::sin(a));
            addVertex(mesh, glm::vec3(n.x * radius, py, n.z * radius), n, glm::vec2(u, static_cast<float>(y)));
        }
    }
    int row = slices + 1;
    for (int x = 0; x < slices; ++x) {
        unsigned int a = base + static_cast<unsigned int>(x);
        unsigned int b = a + 1;
        unsigned int c = a + static_cast<unsigned int>(row);
        unsigned int d = c + 1;
        addTri(mesh, a, c, b);
        addTri(mesh, b, c, d);
    }

    unsigned int topCenter = vertexCount(mesh);
    addVertex(mesh, glm::vec3(0, height * 0.5f, 0), glm::vec3(0, 1, 0), glm::vec2(0.5f, 0.5f));
    unsigned int bottomCenter = vertexCount(mesh);
    addVertex(mesh, glm::vec3(0, -height * 0.5f, 0), glm::vec3(0, -1, 0), glm::vec2(0.5f, 0.5f));
    for (int x = 0; x < slices; ++x) {
        float u0 = static_cast<float>(x) / slices;
        float u1 = static_cast<float>(x + 1) / slices;
        float a0 = u0 * glm::two_pi<float>();
        float a1 = u1 * glm::two_pi<float>();
        unsigned int t0 = vertexCount(mesh);
        addVertex(mesh, glm::vec3(std::cos(a0) * radius, height * 0.5f, std::sin(a0) * radius), glm::vec3(0, 1, 0), glm::vec2(0, 0));
        unsigned int t1 = vertexCount(mesh);
        addVertex(mesh, glm::vec3(std::cos(a1) * radius, height * 0.5f, std::sin(a1) * radius), glm::vec3(0, 1, 0), glm::vec2(0, 0));
        addTri(mesh, topCenter, t0, t1);
        unsigned int b0 = vertexCount(mesh);
        addVertex(mesh, glm::vec3(std::cos(a0) * radius, -height * 0.5f, std::sin(a0) * radius), glm::vec3(0, -1, 0), glm::vec2(0, 0));
        unsigned int b1 = vertexCount(mesh);
        addVertex(mesh, glm::vec3(std::cos(a1) * radius, -height * 0.5f, std::sin(a1) * radius), glm::vec3(0, -1, 0), glm::vec2(0, 0));
        addTri(mesh, bottomCenter, b1, b0);
    }
}

static void appendCone(MeshData& mesh, float radius, float height, int slices) {
    const unsigned int base = vertexCount(mesh);
    for (int y = 0; y <= 1; ++y) {
        float t = static_cast<float>(y);
        float py = (t - 0.5f) * height;
        float r = radius * (1.0f - t);
        for (int x = 0; x <= slices; ++x) {
            float u = static_cast<float>(x) / slices;
            float a = u * glm::two_pi<float>();
            glm::vec3 n = glm::normalize(glm::vec3(std::cos(a), radius / height, std::sin(a)));
            addVertex(mesh, glm::vec3(std::cos(a) * r, py, std::sin(a) * r), n, glm::vec2(u, t));
        }
    }
    int row = slices + 1;
    for (int x = 0; x < slices; ++x) {
        unsigned int a = base + static_cast<unsigned int>(x);
        unsigned int b = a + 1;
        unsigned int c = a + static_cast<unsigned int>(row);
        unsigned int d = c + 1;
        addTri(mesh, a, c, b);
        addTri(mesh, b, c, d);
    }
}

static void appendTorus(MeshData& mesh, float majorRadius, float minorRadius, int majorSegments, int minorSegments) {
    const unsigned int base = vertexCount(mesh);
    for (int i = 0; i <= majorSegments; ++i) {
        float u = static_cast<float>(i) / majorSegments;
        float a = u * glm::two_pi<float>();
        for (int j = 0; j <= minorSegments; ++j) {
            float v = static_cast<float>(j) / minorSegments;
            float b = v * glm::two_pi<float>();
            glm::vec3 n(std::cos(a) * std::cos(b), std::sin(b), std::sin(a) * std::cos(b));
            glm::vec3 p((majorRadius + minorRadius * std::cos(b)) * std::cos(a),
                        minorRadius * std::sin(b),
                        (majorRadius + minorRadius * std::cos(b)) * std::sin(a));
            addVertex(mesh, p, glm::normalize(n), glm::vec2(u, v));
        }
    }
    int row = minorSegments + 1;
    for (int i = 0; i < majorSegments; ++i) {
        for (int j = 0; j < minorSegments; ++j) {
            unsigned int a = base + static_cast<unsigned int>(i * row + j);
            unsigned int b = a + static_cast<unsigned int>(row);
            unsigned int c = a + 1;
            unsigned int d = b + 1;
            addTri(mesh, a, b, c);
            addTri(mesh, c, b, d);
        }
    }
}

static MeshData makeTeapot() { return makeNewellTeapot(); }

static MeshData makeShape(int type) {
    MeshData mesh;
    switch (type) {
        case 2:
            appendPlane(mesh, 2.2f, 32);
            break;
        case 3:
            appendCylinder(mesh, 0.72f, 1.35f, 48);
            break;
        case 4:
            appendCone(mesh, 0.82f, 1.55f, 48);
            break;
        case 5:
            appendTorus(mesh, 0.62f, 0.23f, 48, 20);
            break;
        case 6:
            return makeTeapot();
        default:
            break;
    }
    return mesh;
}

int main() {
    if (!glfwInit()) return -1;

    EditorConfig editorCfg;
    LoadConfig(editorCfg);
    gameRootPath = editorCfg.gamePath.empty() ? fs::current_path() : fs::path(editorCfg.gamePath);
    std::error_code gameRootError;
    gameRootPath = fs::weakly_canonical(gameRootPath, gameRootError);
    if (gameRootError || !fs::is_directory(gameRootPath)) gameRootPath = fs::current_path();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1200, 900, "MaterialEditor", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    std::vector<Material> materials;
    std::vector<PhysicalMaterialEntry> physicalMaterials;
    std::vector<std::string> matFiles;
    std::string currentFileName = "None";
    int currentMatIndex = 0;
    std::string currentDefFile = "scripts/materials.def";
    int currentPhysMatIndex = 0;
    int shapeType = std::clamp(editorCfg.shapeType, 0, 7);
    int lightMode = editorCfg.lightMode;
    bool useNormal = editorCfg.useNormal;
    bool useGloss = editorCfg.useGloss;
    bool useLuma = editorCfg.useLuma;
    bool useBump = editorCfg.useBump;
    float lightIntensity = 1.0f;
    float lightColor[3] = {1.0f, 1.0f, 1.0f};

    auto releaseMaterials = [&]() {
        for (auto& material : materials) material.releaseTextures();
        materials.clear();
    };

    auto refreshData = [&](std::string& currFile, int& currIndex) {
        releaseMaterials();
        matFiles.clear();
        physicalMaterials.clear();
        currentPhysMatIndex = 0;
        currentDefFile = "scripts/materials.def";
        physicalMaterialTypes = LoadPhysicalMaterialTypes();

        fs::path scriptsDir = gameRootPath / "scripts";
        if (fs::exists(scriptsDir) && fs::is_directory(scriptsDir)) {
            std::error_code directoryError;
            for (const auto& entry : fs::directory_iterator(scriptsDir, fs::directory_options::skip_permission_denied, directoryError)) {
                if (directoryError) break;
                std::error_code entryError;
                if (!entry.is_regular_file(entryError) || entryError) continue;
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (extension == ".mat") {
                    std::string relativePath = fs::relative(entry.path(), gameRootPath, entryError).string();
                    if (!entryError) matFiles.push_back(std::move(relativePath));
                }
            }
        }
        std::sort(matFiles.begin(), matFiles.end());

        if (!matFiles.empty()) {
            auto currentIt = std::find(matFiles.begin(), matFiles.end(), currFile);
            auto configIt = std::find(matFiles.begin(), matFiles.end(), editorCfg.lastMatFile);
            if (currentIt != matFiles.end()) currFile = *currentIt;
            else if (configIt != matFiles.end()) currFile = *configIt;
            else currFile = matFiles.front();
            LoadAllMaterials(currFile, materials);
            currIndex = 0;
            if (!materials.empty()) materials[currIndex].loadTextures();
        } else {
            currFile = "None";
            currIndex = 0;
        }

        fs::path defPath = scriptsDir / "materials.def";
        if (!fs::exists(defPath)) defPath = gameRootPath / "materials.def";
        if (fs::exists(defPath)) {
            std::error_code relativeError;
            currentDefFile = fs::relative(defPath, gameRootPath, relativeError).string();
            LoadAllPhysicalMaterials(currentDefFile, physicalMaterials);
            if (!physicalMaterials.empty()) physicalMaterials[currentPhysMatIndex].updateBuffers();
        }
    };

    refreshData(currentFileName, currentMatIndex);

    GLuint shader = LoadShaderFromMemory();
    if (shader == 0) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    GLuint skyboxID = LoadSkyboxAs2D("textures/sky");

    struct ShaderUniforms {
        GLint diffuseMap;
        GLint normalMap;
        GLint glossMap;
        GLint lumaMap;
        GLint bumpMap;
        GLint skybox;
        GLint viewPos;
        GLint lightPos;
        GLint lightIntensity;
        GLint lightColor;
        GLint albedo;
        GLint metallic;
        GLint roughness;
        GLint reflectScale;
        GLint smoothness;
        GLint reliefScale;
        GLint model;
        GLint view;
        GLint projection;
        GLint useDiffuse;
        GLint useNormal;
        GLint useGloss;
        GLint useLuma;
        GLint useBump;

        explicit ShaderUniforms(GLuint program)
            : diffuseMap(glGetUniformLocation(program, "diffuseMap")),
              normalMap(glGetUniformLocation(program, "normalMap")),
              glossMap(glGetUniformLocation(program, "glossMap")),
              lumaMap(glGetUniformLocation(program, "lumaMap")),
              bumpMap(glGetUniformLocation(program, "bumpMap")),
              skybox(glGetUniformLocation(program, "skybox")),
              viewPos(glGetUniformLocation(program, "viewPos")),
              lightPos(glGetUniformLocation(program, "lightPos")),
              lightIntensity(glGetUniformLocation(program, "lightIntensity")),
              lightColor(glGetUniformLocation(program, "lightColor")),
              albedo(glGetUniformLocation(program, "albedo")),
              metallic(glGetUniformLocation(program, "metallic")),
              roughness(glGetUniformLocation(program, "roughness")),
              reflectScale(glGetUniformLocation(program, "reflectScale")),
              smoothness(glGetUniformLocation(program, "smoothness")),
              reliefScale(glGetUniformLocation(program, "reliefScale")),
              model(glGetUniformLocation(program, "model")),
              view(glGetUniformLocation(program, "view")),
              projection(glGetUniformLocation(program, "projection")),
              useDiffuse(glGetUniformLocation(program, "useDiffuse")),
              useNormal(glGetUniformLocation(program, "useNormal")),
              useGloss(glGetUniformLocation(program, "useGloss")),
              useLuma(glGetUniformLocation(program, "useLuma")),
              useBump(glGetUniformLocation(program, "useBump")) {}
    };
    const ShaderUniforms uniforms(shader);

    glEnable(GL_DEPTH_TEST);

    GLuint VAO_cube = 0, VBO_cube = 0, EBO_cube = 0;
    glGenVertexArrays(1, &VAO_cube);
    glGenBuffers(1, &VBO_cube);
    glGenBuffers(1, &EBO_cube);
    glBindVertexArray(VAO_cube);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_cube);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_cube);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    GLuint VAO_sphere = 0, VBO_sphere = 0, EBO_sphere = 0;
    Sphere sphere(32, 32);
    glGenVertexArrays(1, &VAO_sphere);
    glGenBuffers(1, &VBO_sphere);
    glGenBuffers(1, &EBO_sphere);
    glBindVertexArray(VAO_sphere);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_sphere);
    glBufferData(GL_ARRAY_BUFFER, sphere.vertices.size() * sizeof(float), sphere.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_sphere);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sphere.indices.size() * sizeof(unsigned int), sphere.indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    std::vector<MeshGPU> customMeshes(6);
    for (int i = 2; i <= 7; ++i) {
        MeshData mesh = makeShape(i);
        customMeshes[static_cast<std::size_t>(i - 2)].upload(mesh);
    }

    glm::vec3 albedo(1.0f, 0.0f, 0.0f);
    float metallic = 0.5f;
    float roughness = 0.5f;
    static float zoom = 2.0f;
    glm::vec3 lightPos(2.0f, 2.0f, 2.0f);

    glUseProgram(shader);
    glUniform1i(uniforms.diffuseMap, 0);
    glUniform1i(uniforms.normalMap, 1);
    glUniform1i(uniforms.glossMap, 2);
    glUniform1i(uniforms.lumaMap, 3);
    glUniform1i(uniforms.bumpMap, 5);
    glUniform1i(uniforms.skybox, 4);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    SetupModernDarkStyle();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) || glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_FALSE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        int window_w, window_h;
        int framebuffer_w, framebuffer_h;
        glfwGetWindowSize(window, &window_w, &window_h);
        glfwGetFramebufferSize(window, &framebuffer_w, &framebuffer_h);

        DrawEditorUI(window_w, window_h, editorCfg, materials, physicalMaterials, matFiles, currentFileName, currentMatIndex,
                     currentDefFile, currentPhysMatIndex, shapeType, lightMode, useNormal, useGloss, useLuma, useBump,
                     lightIntensity, lightColor, skyboxID, refreshData);

        ImGui::Render();

        static float yaw = 0.0f;
        static float pitch = 0.0f;
        static bool isDragging = false;
        static double lastX = 0.0, lastY = 0.0;

        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (!isDragging) {
                    isDragging = true;
                    glfwGetCursorPos(window, &lastX, &lastY);
                } else {
                    double currX, currY;
                    glfwGetCursorPos(window, &currX, &currY);
                    yaw -= static_cast<float>(currX - lastX) * 0.01f;
                    pitch += static_cast<float>(currY - lastY) * 0.01f;
                    pitch = std::clamp(pitch, -1.5f, 1.5f);
                    lastX = currX;
                    lastY = currY;
                }
            } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                double currX, currY;
                glfwGetCursorPos(window, &currX, &currY);
                if (!isDragging) {
                    isDragging = true;
                    lastX = currX;
                    lastY = currY;
                } else {
                    zoom += static_cast<float>(currY - lastY) * 0.01f;
                    lastY = currY;
                }
            } else {
                isDragging = false;
            }
            zoom = std::clamp(zoom, 0.5f, 10.0f);
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(shader);

        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, skyboxID);

        float camX = std::sin(yaw) * std::cos(pitch) * zoom;
        float camY = std::sin(pitch) * zoom;
        float camZ = std::cos(yaw) * std::cos(pitch) * zoom;
        glm::vec3 cameraPos(camX, camY, camZ);
        glm::vec3 finalLightPos = (lightMode == 0) ? cameraPos : lightPos;

        glUniform3fv(uniforms.viewPos, 1, &cameraPos.x);
        glUniform3fv(uniforms.lightPos, 1, &finalLightPos.x);
        glUniform1f(uniforms.lightIntensity, lightIntensity);
        glUniform3fv(uniforms.lightColor, 1, lightColor);
        glUniform3fv(uniforms.albedo, 1, &albedo.r);
        glUniform1f(uniforms.metallic, metallic);
        glUniform1f(uniforms.roughness, roughness);

        if (!materials.empty() && currentMatIndex >= 0 && static_cast<std::size_t>(currentMatIndex) < materials.size()) {
            glUniform1f(uniforms.reflectScale, materials[currentMatIndex].reflectScale);
            glUniform1f(uniforms.smoothness, materials[currentMatIndex].smoothness);
            glUniform1f(uniforms.reliefScale, materials[currentMatIndex].reliefScale);
        } else {
            glUniform1f(uniforms.reflectScale, 0.3f);
            glUniform1f(uniforms.smoothness, 1.0f);
            glUniform1f(uniforms.reliefScale, 0.0f);
        }

        glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        float aspectRatio = framebuffer_h > 0 ? static_cast<float>(framebuffer_w) / static_cast<float>(framebuffer_h) : 1.333f;
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
        glm::mat4 model(1.0f);

        glViewport(0, 0, framebuffer_w, framebuffer_h);
        glUniformMatrix4fv(uniforms.view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(uniforms.projection, 1, GL_FALSE, glm::value_ptr(proj));
        glUniformMatrix4fv(uniforms.model, 1, GL_FALSE, glm::value_ptr(model));

        glUniform1i(uniforms.useDiffuse, 0);
        glUniform1i(uniforms.useNormal, 0);
        glUniform1i(uniforms.useGloss, 0);
        glUniform1i(uniforms.useLuma, 0);
        glUniform1i(uniforms.useBump, 0);

        if (!materials.empty() && currentMatIndex >= 0 && static_cast<std::size_t>(currentMatIndex) < materials.size()) {
            Material& mat = materials[currentMatIndex];
            auto diffuseIt = mat.textures.find("diffuse");
            if (diffuseIt != mat.textures.end() && diffuseIt->second != 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, diffuseIt->second);
                glUniform1i(uniforms.useDiffuse, 1);
            }
            auto normalIt = mat.textures.find("normal");
            if (normalIt != mat.textures.end() && normalIt->second != 0) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, normalIt->second);
                glUniform1i(uniforms.useNormal, useNormal ? 1 : 0);
            }
            auto glossIt = mat.textures.find("gloss");
            if (glossIt != mat.textures.end() && glossIt->second != 0) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, glossIt->second);
                glUniform1i(uniforms.useGloss, useGloss ? 1 : 0);
            }
            auto lumaIt = mat.textures.find("luma");
            if (lumaIt != mat.textures.end() && lumaIt->second != 0) {
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, lumaIt->second);
                glUniform1i(uniforms.useLuma, useLuma ? 1 : 0);
            }
            auto bumpIt = mat.textures.find("bump");
            if (bumpIt != mat.textures.end() && bumpIt->second != 0) {
                glActiveTexture(GL_TEXTURE5);
                glBindTexture(GL_TEXTURE_2D, bumpIt->second);
                glUniform1i(uniforms.useBump, useBump ? 1 : 0);
            }
        }

        if (shapeType == 0) {
            glBindVertexArray(VAO_cube);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        } else if (shapeType == 1) {
            glBindVertexArray(VAO_sphere);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sphere.indices.size()), GL_UNSIGNED_INT, nullptr);
        } else if (shapeType >= 2 && shapeType <= 7) {
            customMeshes[static_cast<std::size_t>(shapeType - 2)].draw();
        }

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    for (auto& material : materials) material.releaseTextures();
    for (auto& mesh : customMeshes) mesh.release();
    if (skyboxID != 0) glDeleteTextures(1, &skyboxID);
    if (shader != 0) glDeleteProgram(shader);
    glDeleteBuffers(1, &EBO_cube);
    glDeleteBuffers(1, &VBO_cube);
    glDeleteVertexArrays(1, &VAO_cube);
    glDeleteBuffers(1, &EBO_sphere);
    glDeleteBuffers(1, &VBO_sphere);
    glDeleteVertexArrays(1, &VAO_sphere);

    glfwDestroyWindow(window);
    glfwTerminate();

    editorCfg.gamePath = gameRootPath.string();
    editorCfg.lastMatFile = currentFileName;
    editorCfg.shapeType = shapeType;
    editorCfg.lightMode = lightMode;
    editorCfg.useNormal = useNormal;
    editorCfg.useGloss = useGloss;
    editorCfg.useLuma = useLuma;
    editorCfg.useBump = useBump;
    SaveConfig(editorCfg);
    return 0;
}
