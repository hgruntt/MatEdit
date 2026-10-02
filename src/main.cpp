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
#include <filesystem>
#include <array>
#include <map>
#include <sstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Config.h"
#include "I18n.h"
#include "MaterialSystem.h"
#include "Shader.h"
#include "PostProcess.h"
#include "Skybox.h"
#include "EditorUI.h"
#include "Contributors.h"

static std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

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
        const float v = static_cast<float>(y);
        const float py = (v - 0.5f) * height;
        for (int x = 0; x <= slices; ++x) {
            const float u = static_cast<float>(x) / slices;
            const float a = u * glm::two_pi<float>();
            const glm::vec3 n(std::cos(a), 0.0f, std::sin(a));
            addVertex(mesh, glm::vec3(n.x * radius, py, n.z * radius), n, glm::vec2(u, v));
        }
    }

    const int row = slices + 1;
    for (int x = 0; x < slices; ++x) {
        const unsigned int a = base + static_cast<unsigned int>(x);
        const unsigned int b = a + 1;
        const unsigned int c = a + static_cast<unsigned int>(row);
        const unsigned int d = c + 1;
        addTri(mesh, a, c, b);
        addTri(mesh, b, c, d);
    }

    const unsigned int topCenter = vertexCount(mesh);
    addVertex(mesh, glm::vec3(0.0f, height * 0.5f, 0.0f), glm::vec3(0, 1, 0), glm::vec2(0.5f, 0.5f));
    const unsigned int bottomCenter = vertexCount(mesh);
    addVertex(mesh, glm::vec3(0.0f, -height * 0.5f, 0.0f), glm::vec3(0, -1, 0), glm::vec2(0.5f, 0.5f));

    for (int x = 0; x < slices; ++x) {
        const float u0 = static_cast<float>(x) / slices;
        const float u1 = static_cast<float>(x + 1) / slices;
        const float a0 = u0 * glm::two_pi<float>();
        const float a1 = u1 * glm::two_pi<float>();

        const unsigned int t0 = vertexCount(mesh);
        const glm::vec3 tp0(std::cos(a0) * radius, height * 0.5f, std::sin(a0) * radius);
        addVertex(mesh, tp0, glm::vec3(0, 1, 0), glm::vec2(0.5f + 0.5f * tp0.x / radius, 0.5f + 0.5f * tp0.z / radius));
        const unsigned int t1 = vertexCount(mesh);
        const glm::vec3 tp1(std::cos(a1) * radius, height * 0.5f, std::sin(a1) * radius);
        addVertex(mesh, tp1, glm::vec3(0, 1, 0), glm::vec2(0.5f + 0.5f * tp1.x / radius, 0.5f + 0.5f * tp1.z / radius));
        addTri(mesh, topCenter, t0, t1);

        const unsigned int b0 = vertexCount(mesh);
        const glm::vec3 bp0(std::cos(a0) * radius, -height * 0.5f, std::sin(a0) * radius);
        addVertex(mesh, bp0, glm::vec3(0, -1, 0), glm::vec2(0.5f + 0.5f * bp0.x / radius, 0.5f - 0.5f * bp0.z / radius));
        const unsigned int b1 = vertexCount(mesh);
        const glm::vec3 bp1(std::cos(a1) * radius, -height * 0.5f, std::sin(a1) * radius);
        addVertex(mesh, bp1, glm::vec3(0, -1, 0), glm::vec2(0.5f + 0.5f * bp1.x / radius, 0.5f - 0.5f * bp1.z / radius));
        addTri(mesh, bottomCenter, b1, b0);
    }
}

static void appendCone(MeshData& mesh, float radius, float height, int slices) {
    const unsigned int base = vertexCount(mesh);

    for (int x = 0; x <= slices; ++x) {
        const float u = static_cast<float>(x) / slices;
        const float a = u * glm::two_pi<float>();
        const glm::vec3 n = glm::normalize(glm::vec3(std::cos(a), radius / height, std::sin(a)));
        addVertex(mesh, glm::vec3(std::cos(a) * radius, -height * 0.5f, std::sin(a) * radius), n, glm::vec2(u, 0.0f));
    }

    const unsigned int apex = vertexCount(mesh);
    addVertex(mesh, glm::vec3(0.0f, height * 0.5f, 0.0f), glm::normalize(glm::vec3(0.0f, radius / height, 0.0f)), glm::vec2(0.5f, 1.0f));

    for (int x = 0; x < slices; ++x) {
        const unsigned int a = base + static_cast<unsigned int>(x);
        const unsigned int b = a + 1;
        addTri(mesh, a, apex, b);
    }

    const unsigned int bottomCenter = vertexCount(mesh);
    addVertex(mesh, glm::vec3(0.0f, -height * 0.5f, 0.0f), glm::vec3(0, -1, 0), glm::vec2(0.5f, 0.5f));
    for (int x = 0; x < slices; ++x) {
        const float u0 = static_cast<float>(x) / slices;
        const float u1 = static_cast<float>(x + 1) / slices;
        const float a0 = u0 * glm::two_pi<float>();
        const float a1 = u1 * glm::two_pi<float>();
        const unsigned int v0 = vertexCount(mesh);
        const glm::vec3 p0(std::cos(a0) * radius, -height * 0.5f, std::sin(a0) * radius);
        addVertex(mesh, p0, glm::vec3(0, -1, 0), glm::vec2(0.5f + 0.5f * p0.x / radius, 0.5f - 0.5f * p0.z / radius));
        const unsigned int v1 = vertexCount(mesh);
        const glm::vec3 p1(std::cos(a1) * radius, -height * 0.5f, std::sin(a1) * radius);
        addVertex(mesh, p1, glm::vec3(0, -1, 0), glm::vec2(0.5f + 0.5f * p1.x / radius, 0.5f - 0.5f * p1.z / radius));
        addTri(mesh, bottomCenter, v1, v0);
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

struct ViewportFramebuffer {
    GLuint fbo = 0;
    GLuint color = 0;
    GLuint depth = 0;
    GLuint msaaFbo = 0;
    GLuint msaaColor = 0;
    GLuint msaaDepth = 0;
    int width = 0;
    int height = 0;
    int samples = 1;
};

static ViewportFramebuffer g_viewportFramebuffer;

static void DestroyViewportFramebuffer(ViewportFramebuffer& target) {
    if (target.msaaDepth) glDeleteRenderbuffers(1, &target.msaaDepth);
    if (target.msaaColor) glDeleteRenderbuffers(1, &target.msaaColor);
    if (target.msaaFbo) glDeleteFramebuffers(1, &target.msaaFbo);
    if (target.depth) glDeleteRenderbuffers(1, &target.depth);
    if (target.color) glDeleteTextures(1, &target.color);
    if (target.fbo) glDeleteFramebuffers(1, &target.fbo);
    target = {};
}

static bool EnsureViewportFramebuffer(ViewportFramebuffer& target, int width, int height, int requestedSamples) {
    width = std::max(1, width);
    height = std::max(1, height);
    static GLint maxSamples = 0;
    if (maxSamples == 0) glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    const int samples = std::clamp(requestedSamples, 1, std::max(1, maxSamples));
    if (target.fbo != 0 && target.width == width && target.height == height && target.samples == samples) return true;
    DestroyViewportFramebuffer(target);

    glGenFramebuffers(1, &target.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);

    glGenTextures(1, &target.color);
    glBindTexture(GL_TEXTURE_2D, target.color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.color, 0);

    glGenRenderbuffers(1, &target.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, target.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.depth);

    bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        DestroyViewportFramebuffer(target);
        return false;
    }

    if (samples > 1) {
        glGenFramebuffers(1, &target.msaaFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, target.msaaFbo);

        glGenRenderbuffers(1, &target.msaaColor);
        glBindRenderbuffer(GL_RENDERBUFFER, target.msaaColor);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, target.msaaColor);

        glGenRenderbuffers(1, &target.msaaDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, target.msaaDepth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.msaaDepth);

        complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!complete) {
            DestroyViewportFramebuffer(target);
            return false;
        }
    }

    target.width = width;
    target.height = height;
    target.samples = samples;
    return true;
}

int main() {
    if (!glfwInit()) return -1;

    EditorConfig editorCfg;
    LoadConfig(editorCfg);
    SetLanguage(editorCfg.language);
    if (editorCfg.taaEnabled) {
        editorCfg.fxaaEnabled = false;
        editorCfg.msaaEnabled = false;
    } else if (editorCfg.msaaEnabled) {
        editorCfg.fxaaEnabled = false;
    }
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
    glfwSwapInterval(editorCfg.vsyncEnabled ? 1 : 0);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    SetTextureFilteringEnabled(editorCfg.textureFilteringEnabled);

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    std::vector<Material> materials;
    std::vector<PhysicalMaterialEntry> physicalMaterials;
    std::vector<std::string> matFiles;
    std::vector<std::string> defFiles;
    std::vector<std::string> ddsFiles;
    std::string currentFileName = "None";
    int currentMatIndex = 0;
    std::string currentDefFile = "scripts/materials.def";
    int currentPhysMatIndex = 0;
    int shapeType = std::clamp(editorCfg.shapeType, 0, 6);
    int lightMode = editorCfg.lightMode;
    bool useNormal = editorCfg.useNormal;
    bool useGloss = editorCfg.useGloss;
    bool useLuma = editorCfg.useLuma;
    bool useBump = editorCfg.useBump;
    float lightIntensity = 1.0f;
    float lightColor[3] = {1.0f, 1.0f, 1.0f};
    double fpsTime = glfwGetTime();
    int fpsFrames = 0;
    float fpsValue = 0.0f;

    auto releaseMaterials = [&]() {
        for (auto& material : materials) material.releaseTextures();
        materials.clear();
    };

    auto refreshData = [&](std::string& currFile, int& currIndex) {
        ReleaseEditorUIPreview();
        releaseMaterials();
        matFiles.clear();
        defFiles.clear();
        ddsFiles.clear();
        physicalMaterials.clear();
        currentPhysMatIndex = 0;
        currentDefFile = "scripts/materials.def";
        physicalMaterialTypes = LoadPhysicalMaterialTypes();

        auto scanScripts = [&](const fs::path& directory, bool recursive) {
            if (!fs::is_directory(directory)) return;
            std::error_code scanEc;
            if (recursive) {
                for (fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, scanEc), end; it != end; it.increment(scanEc)) {
                    if (scanEc) { scanEc.clear(); continue; }
                    std::error_code entryEc;
                    if (!it->is_regular_file(entryEc) || entryEc) continue;
                    const std::string extension = ToLower(it->path().extension().string());
                    if (extension != ".mat" && extension != ".def") continue;
                    std::error_code relEc;
                    const std::string relativePath = fs::relative(it->path(), gameRootPath, relEc).generic_string();
                    if (relEc) continue;
                    if (extension == ".mat") matFiles.push_back(relativePath);
                    else defFiles.push_back(relativePath);
                }
            } else {
                for (fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, scanEc), end; it != end; it.increment(scanEc)) {
                    if (scanEc) { scanEc.clear(); continue; }
                    std::error_code entryEc;
                    if (!it->is_regular_file(entryEc) || entryEc) continue;
                    const std::string extension = ToLower(it->path().extension().string());
                    if (extension != ".mat" && extension != ".def") continue;
                    std::error_code relEc;
                    const std::string relativePath = fs::relative(it->path(), gameRootPath, relEc).generic_string();
                    if (relEc) continue;
                    if (extension == ".mat") matFiles.push_back(relativePath);
                    else defFiles.push_back(relativePath);
                }
            }
        };

        if (fs::exists(gameRootPath) && fs::is_directory(gameRootPath)) {
            scanScripts(gameRootPath, false);
            scanScripts(gameRootPath / "scripts", true);
        }

        auto uniqueInsensitive = [](std::vector<std::string>& files) {
            std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
                std::string al = a;
                std::string bl = b;
                std::transform(al.begin(), al.end(), al.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::transform(bl.begin(), bl.end(), bl.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return al < bl;
            });
            files.erase(std::unique(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
                std::string al = a;
                std::string bl = b;
                std::transform(al.begin(), al.end(), al.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::transform(bl.begin(), bl.end(), bl.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return al == bl;
            }), files.end());
        };
        uniqueInsensitive(matFiles);
        uniqueInsensitive(defFiles);

        ClearWadArchives();
        if (editorCfg.autoLoadWads) ScanAndLoadAllWads();
        else LoadWadArchives(editorCfg.loadedWads);
        editorCfg.loadedWads = GetLoadedWadPaths();
        SaveConfig(editorCfg);

        if (!matFiles.empty()) {
            auto currentIt = std::find(matFiles.begin(), matFiles.end(), currFile);
            auto configIt = std::find(matFiles.begin(), matFiles.end(), editorCfg.lastMatFile);
            std::vector<std::string> candidates;
            if (currentIt != matFiles.end()) candidates.push_back(*currentIt);
            if (configIt != matFiles.end() && configIt != currentIt) candidates.push_back(*configIt);
            for (const auto& candidate : matFiles) {
                if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) candidates.push_back(candidate);
            }

            bool loadedMatFile = false;
            for (const auto& candidate : candidates) {
                if (!LoadAllMaterials(candidate, materials, editorCfg.autoAssignMaterialTextures)) continue;
                currFile = candidate;
                loadedMatFile = true;
                break;
            }
            currIndex = loadedMatFile && !materials.empty() ? 0 : -1;
            if (currIndex >= 0) materials[currIndex].loadTextures();
            if (!loadedMatFile) currFile = "None";
        } else {
            currFile = "None";
            currIndex = -1;
        }

        if (!defFiles.empty()) {
            auto currentDefIt = std::find(defFiles.begin(), defFiles.end(), currentDefFile);
            if (currentDefIt == defFiles.end()) currentDefFile = defFiles.front();
            LoadAllPhysicalMaterials(currentDefFile, physicalMaterials);
            currentPhysMatIndex = 0;
            if (!physicalMaterials.empty()) physicalMaterials[currentPhysMatIndex].updateBuffers();
        }

    };

    refreshData(currentFileName, currentMatIndex);

    GLuint shader = LoadShader();
    if (shader == 0) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    StartContributorRefresh();
    GLuint skyboxTexture = 0;
    fs::path envPath = gameRootPath / "gfx" / "env";
    std::error_code envEc;
    if (!fs::is_directory(envPath, envEc)) {
        envEc.clear();
        if (gameRootPath.filename() == "env" && fs::is_directory(gameRootPath, envEc)) envPath = gameRootPath;
        else if (gameRootPath.filename() == "gfx" && fs::is_directory(gameRootPath / "env", envEc)) envPath = gameRootPath / "env";
        else if (fs::is_directory(gameRootPath / "env", envEc)) envPath = gameRootPath / "env";
    }
    const std::vector<std::string> availableSkyboxes = FindSkyboxNames(envPath);

    if (!editorCfg.skyboxName.empty()) {
        skyboxTexture = LoadSkyboxCubemap((envPath / editorCfg.skyboxName).string());
        if (skyboxTexture == 0) editorCfg.skyboxName.clear();
    }

    if (skyboxTexture == 0 && !availableSkyboxes.empty()) {
        for (const std::string& name : availableSkyboxes) {
            const GLuint loaded = LoadSkyboxCubemap((envPath / name).string());
            if (loaded != 0) {
                skyboxTexture = loaded;
                editorCfg.skyboxName = name;
                break;
            }
        }
    }

    GLuint skyboxShader = LoadSkyboxShader();
    const GLint skyboxViewUniform = skyboxShader ? glGetUniformLocation(skyboxShader, "view") : -1;
    const GLint skyboxProjectionUniform = skyboxShader ? glGetUniformLocation(skyboxShader, "projection") : -1;
    const GLint skyboxSamplerUniform = skyboxShader ? glGetUniformLocation(skyboxShader, "skybox") : -1;
    GLuint fxaaShader = LoadFXAAShader();
    FXAATarget fxaaTarget;
    GLuint taaShader = LoadTAAShader();
    TAATarget taaTarget;
    bool previousTaaEnabled = false;
    bool taaInitialized = false;
    glm::vec3 previousTaaCameraPos(0.0f);
    glm::vec3 previousTaaCameraTarget(0.0f);
    int previousTaaMaterial = -1;
    std::size_t taaSequence = 0;

    struct ShaderUniforms {
        GLint diffuseMap;
        GLint normalMap;
        GLint glossMap;
        GLint lumaMap;
        GLint bumpMap;
        GLint detailMap;
        GLint skybox;
        GLint useSkybox;
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
        GLint refractScale;
        GLint aberrationScale;
        GLint model;
        GLint view;
        GLint projection;
        GLint useDiffuse;
        GLint useDiffuseAlpha;
        GLint useNormal;
        GLint normalMapMode;
        GLint hasNormalMap;
        GLint diffuseIsSRGB;
        GLint glossIsSRGB;
        GLint useGloss;
        GLint useLuma;
        GLint useBump;
        GLint useDetail;
        GLint detailScale;
        GLint textureScale;

        explicit ShaderUniforms(GLuint program)
            : diffuseMap(glGetUniformLocation(program, "diffuseMap")),
              normalMap(glGetUniformLocation(program, "normalMap")),
              glossMap(glGetUniformLocation(program, "glossMap")),
              lumaMap(glGetUniformLocation(program, "lumaMap")),
              bumpMap(glGetUniformLocation(program, "bumpMap")),
              detailMap(glGetUniformLocation(program, "detailMap")),
              skybox(glGetUniformLocation(program, "skybox")),
              useSkybox(glGetUniformLocation(program, "useSkybox")),
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
              refractScale(glGetUniformLocation(program, "refractScale")),
              aberrationScale(glGetUniformLocation(program, "aberrationScale")),
              model(glGetUniformLocation(program, "model")),
              view(glGetUniformLocation(program, "view")),
              projection(glGetUniformLocation(program, "projection")),
              useDiffuse(glGetUniformLocation(program, "useDiffuse")),
              useDiffuseAlpha(glGetUniformLocation(program, "useDiffuseAlpha")),
              useNormal(glGetUniformLocation(program, "useNormal")),
              normalMapMode(glGetUniformLocation(program, "normalMapMode")),
              hasNormalMap(glGetUniformLocation(program, "hasNormalMap")),
              diffuseIsSRGB(glGetUniformLocation(program, "diffuseIsSRGB")),
              glossIsSRGB(glGetUniformLocation(program, "glossIsSRGB")),
              useGloss(glGetUniformLocation(program, "useGloss")),
              useLuma(glGetUniformLocation(program, "useLuma")),
              useBump(glGetUniformLocation(program, "useBump")),
              useDetail(glGetUniformLocation(program, "useDetail")),
              detailScale(glGetUniformLocation(program, "detailScale")),
              textureScale(glGetUniformLocation(program, "textureScale")) {}
    };
    const ShaderUniforms uniforms(shader);

    glEnable(GL_DEPTH_TEST);

    GLuint VAO_cube = 0, VBO_cube = 0, EBO_cube = 0;
    glGenVertexArrays(1, &VAO_cube);
    glGenBuffers(1, &VBO_cube);
    glGenBuffers(1, &EBO_cube);
    glBindVertexArray(VAO_cube);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_cube);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kBuiltinCubeVertices), kBuiltinCubeVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_cube);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kBuiltinCubeIndices), kBuiltinCubeIndices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    GLuint VAO_sphere = 0, VBO_sphere = 0, EBO_sphere = 0;
    BuiltinSphere sphere(32, 32);
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

    std::vector<MeshGPU> customMeshes(5);
    for (int i = 2; i <= 6; ++i) {
        MeshData mesh = makeShape(i);
        customMeshes[static_cast<std::size_t>(i - 2)].upload(mesh);
    }

    glm::vec3 albedo(1.0f, 0.0f, 0.0f);
    float metallic = 0.5f;
    float roughness = 0.5f;
    static float zoom = 2.0f;
    glm::vec3 lightPos(2.0f, 2.0f, 2.0f);
    bool flightMode = false;
    bool modelVisible = true;
    bool zWasDown = false;
    glm::vec3 flightPosition(0.0f, 0.0f, 2.0f);
    float flightYaw = 0.0f;
    float flightPitch = 0.0f;
    double flightLastX = 0.0;
    double flightLastY = 0.0;
    double previousFrameTime = glfwGetTime();

    glUseProgram(shader);
    glUniform1i(uniforms.diffuseMap, 0);
    glUniform1i(uniforms.normalMap, 1);
    glUniform1i(uniforms.glossMap, 2);
    glUniform1i(uniforms.lumaMap, 3);
    glUniform1i(uniforms.bumpMap, 5);
    glUniform1i(uniforms.detailMap, 6);
    glUniform1i(uniforms.skybox, 4);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
#ifdef MATEDIT_FONT_PATH
    ImFont* uiFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        MATEDIT_FONT_PATH, 13.0f, nullptr, ImGui::GetIO().Fonts->GetGlyphRangesCyrillic());
    if (uiFont) {
        ImGui::GetIO().FontDefault = uiFont;
    } else {
        std::cerr << "Failed to load UI font with Cyrillic glyphs: " << MATEDIT_FONT_PATH << '\n';
    }
#else
    ImGui::GetIO().Fonts->AddFontDefault();
#endif
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    InitUI();
    SetEditorInputWindow(window);

    bool hWasDown = false;
    bool modelWasDown = false;
    bool pWasDown = false;
    bool hasRenderedFrame = false;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (hasRenderedFrame &&
            (glfwGetWindowAttrib(window, GLFW_ICONIFIED) || glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_FALSE)) {
            glfwWaitEventsTimeout(0.1);
            previousFrameTime = glfwGetTime();
            hWasDown = false;
            modelWasDown = false;
            pWasDown = false;
            zWasDown = false;
            continue;
        }

        ImGuiIO& hotkeyIO = ImGui::GetIO();
        const bool hDown = glfwGetKey(window, editorCfg.keyTogglePanels) == GLFW_PRESS;
        if (hDown && !hWasDown && !hotkeyIO.WantTextInput && !hotkeyIO.WantCaptureKeyboard && IsEditorViewportHovered()) ToggleEditorPanels();
        hWasDown = hDown;

        const bool modelDown = glfwGetKey(window, editorCfg.keyToggleModel) == GLFW_PRESS;
        if (modelDown && !modelWasDown && !hotkeyIO.WantTextInput && !hotkeyIO.WantCaptureKeyboard && (IsEditorViewportHovered() || !flightMode)) modelVisible = !modelVisible;
        modelWasDown = modelDown;

        const bool pDown = glfwGetKey(window, editorCfg.keyOpenSettings) == GLFW_PRESS;
        if (pDown && !pWasDown && !hotkeyIO.WantTextInput && !hotkeyIO.WantCaptureKeyboard) OpenEditorSettings();
        pWasDown = pDown;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        int window_w, window_h;
        int framebuffer_w, framebuffer_h;
        glfwGetWindowSize(window, &window_w, &window_h);
        glfwGetFramebufferSize(window, &framebuffer_w, &framebuffer_h);

        DrawEditorUI(window_w, window_h, editorCfg, materials, physicalMaterials, matFiles, defFiles, ddsFiles, currentFileName, currentMatIndex,
                     currentDefFile, currentPhysMatIndex, shapeType, modelVisible, lightMode, useNormal, useGloss, useLuma, useBump,
                     lightIntensity, lightColor, skyboxTexture, refreshData);

        static float yaw = 0.0f;
        static float pitch = 0.0f;
        static bool isDragging = false;
        static double lastX = 0.0, lastY = 0.0;
        ViewportFramebuffer& viewportFramebuffer = g_viewportFramebuffer;

        ImGuiIO& io = ImGui::GetIO();
        const bool zDown = glfwGetKey(window, editorCfg.keyFreeCam) == GLFW_PRESS;
        if (zDown && !zWasDown && !io.WantTextInput && !io.WantCaptureKeyboard && (flightMode || IsEditorViewportHovered())) {
            flightMode = !flightMode;
            if (flightMode) {
                const float camX = std::sin(yaw) * std::cos(pitch) * zoom;
                const float camY = std::sin(pitch) * zoom;
                const float camZ = std::cos(yaw) * std::cos(pitch) * zoom;
                flightPosition = glm::vec3(camX, camY, camZ);
                flightYaw = yaw;
                flightPitch = pitch;
                glfwGetCursorPos(window, &flightLastX, &flightLastY);
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            } else {
                const float distance = std::max(glm::length(flightPosition), 0.05f);
                zoom = editorCfg.unlimitedZoom ? distance : std::max(distance, 1.6f);
                yaw = std::atan2(flightPosition.x, flightPosition.z);
                pitch = std::asin(std::clamp(flightPosition.y / distance, -1.0f, 1.0f));
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        }
        zWasDown = zDown;

        const double currentFrameTime = glfwGetTime();
        const float frameDelta = static_cast<float>(std::min(currentFrameTime - previousFrameTime, 0.1));
        previousFrameTime = currentFrameTime;

        if (flightMode) {
            double currX = 0.0, currY = 0.0;
            glfwGetCursorPos(window, &currX, &currY);
            flightYaw -= static_cast<float>(currX - flightLastX) * 0.0035f;
            flightPitch -= static_cast<float>(currY - flightLastY) * 0.0035f;
            flightPitch = std::clamp(flightPitch, -1.54f, 1.54f);
            flightLastX = currX;
            flightLastY = currY;

            glm::vec3 forward(
                std::sin(flightYaw) * std::cos(flightPitch),
                std::sin(flightPitch),
                std::cos(flightYaw) * std::cos(flightPitch));
            forward = glm::normalize(forward);
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            float moveSpeed = glfwGetKey(window, editorCfg.keySprint) == GLFW_PRESS ? 6.0f : 2.5f;
            if (glfwGetKey(window, editorCfg.keyForward) == GLFW_PRESS) flightPosition += forward * moveSpeed * frameDelta;
            if (glfwGetKey(window, editorCfg.keyBackward) == GLFW_PRESS) flightPosition -= forward * moveSpeed * frameDelta;
            if (glfwGetKey(window, editorCfg.keyLeft) == GLFW_PRESS) flightPosition -= right * moveSpeed * frameDelta;
            if (glfwGetKey(window, editorCfg.keyRight) == GLFW_PRESS) flightPosition += right * moveSpeed * frameDelta;
            if (glfwGetKey(window, editorCfg.keyUp) == GLFW_PRESS) flightPosition += up * moveSpeed * frameDelta;
            if (glfwGetKey(window, editorCfg.keyDown) == GLFW_PRESS) flightPosition -= up * moveSpeed * frameDelta;
        } else if (IsEditorViewportHovered()) {
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
            if (io.MouseWheel != 0.0f) zoom -= io.MouseWheel * 0.35f;
            const float minZoom = editorCfg.unlimitedZoom ? 0.05f : 1.6f;
            zoom = std::clamp(zoom, minZoom, 10.0f);
        } else {
            isDragging = false;
        }

        float viewport_x = 0.0f;
        float viewport_y = 0.0f;
        float viewport_w = static_cast<float>(window_w);
        float viewport_h = static_cast<float>(window_h);
        GetEditorViewportRect(viewport_x, viewport_y, viewport_w, viewport_h);

        const float scale_x = window_w > 0 ? static_cast<float>(framebuffer_w) / static_cast<float>(window_w) : 1.0f;
        const float scale_y = window_h > 0 ? static_cast<float>(framebuffer_h) / static_cast<float>(window_h) : 1.0f;
        const int viewportPixelW = std::max(10, static_cast<int>(std::round(viewport_w * scale_x)));
        const int viewportPixelH = std::max(10, static_cast<int>(std::round(viewport_h * scale_y)));
        const int requestedMsaaSamples = editorCfg.msaaEnabled ? editorCfg.msaaSamples : 1;
        const bool viewportReady = EnsureViewportFramebuffer(viewportFramebuffer, viewportPixelW, viewportPixelH, requestedMsaaSamples);
        const bool fxaaReady = editorCfg.fxaaEnabled && viewportReady && fxaaShader != 0 && EnsureFXAATarget(fxaaTarget, viewportPixelW, viewportPixelH);
        const bool taaReady = editorCfg.taaEnabled && viewportReady && taaShader != 0 && EnsureTAATarget(taaTarget, viewportPixelW, viewportPixelH);
        const bool msaaReady = editorCfg.msaaEnabled && viewportReady && viewportFramebuffer.samples > 1;

        glBindFramebuffer(GL_FRAMEBUFFER, msaaReady ? viewportFramebuffer.msaaFbo : (viewportReady ? viewportFramebuffer.fbo : 0));
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, viewportReady ? viewportFramebuffer.width : framebuffer_w, viewportReady ? viewportFramebuffer.height : framebuffer_h);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(shader);

        glm::vec3 cameraPos;
        glm::vec3 cameraTarget;
        glm::vec3 cameraUp(0.0f, 1.0f, 0.0f);
        if (flightMode) {
            const glm::vec3 forward(
                std::sin(flightYaw) * std::cos(flightPitch),
                std::sin(flightPitch),
                std::cos(flightYaw) * std::cos(flightPitch));
            cameraPos = flightPosition;
            cameraTarget = cameraPos + glm::normalize(forward);
        } else {
            const float camX = std::sin(yaw) * std::cos(pitch) * zoom;
            const float camY = std::sin(pitch) * zoom;
            const float camZ = std::cos(yaw) * std::cos(pitch) * zoom;
            cameraPos = glm::vec3(camX, camY, camZ);
            cameraTarget = glm::vec3(0.0f);
        }
        glm::vec3 finalLightPos = lightPos;
        if (lightMode == 0) {
            finalLightPos = cameraPos;
        } else if (lightMode == 2) {
            const float angle = static_cast<float>(glfwGetTime()) * editorCfg.dynamicLightSpeed;
            const float radius = std::max(1.0f, editorCfg.dynamicLightRadius);
            finalLightPos = glm::vec3(
                std::cos(angle) * radius,
                1.2f + std::sin(angle * 0.7f) * 0.8f,
                std::sin(angle) * radius
            );
        }

        glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, cameraUp);
        const float aspectRatio = viewport_h > 0.0f ? viewport_w / viewport_h : 1.333f;
        glm::mat4 proj = glm::perspective(glm::radians(editorCfg.fov), aspectRatio, 0.1f, 100.0f);

        const bool taaCameraMoving = taaInitialized && (glm::length(cameraPos - previousTaaCameraPos) > 0.0005f || glm::length(cameraTarget - previousTaaCameraTarget) > 0.0005f);
        const bool taaMaterialChanged = taaInitialized && currentMatIndex != previousTaaMaterial;
        const bool taaResetHistory = !editorCfg.taaEnabled || !previousTaaEnabled || taaCameraMoving || taaMaterialChanged || !taaTarget.historyValid;
        if (editorCfg.taaEnabled && taaReady && !taaCameraMoving) {
            static const glm::vec2 taaPattern[8] = {
                glm::vec2(0.0f, -0.1667f), glm::vec2(-0.25f, 0.1667f), glm::vec2(0.25f, -0.3889f), glm::vec2(-0.375f, -0.0556f),
                glm::vec2(0.125f, 0.2778f), glm::vec2(-0.125f, -0.2778f), glm::vec2(0.375f, 0.0556f), glm::vec2(-0.4375f, 0.3889f)
            };
            const glm::vec2 jitter = taaPattern[taaSequence % 8];
            proj[2][0] += (2.0f * jitter.x) / static_cast<float>(viewportPixelW);
            proj[2][1] += (2.0f * jitter.y) / static_cast<float>(viewportPixelH);
            ++taaSequence;
        }
        previousTaaCameraPos = cameraPos;
        previousTaaCameraTarget = cameraTarget;
        previousTaaMaterial = currentMatIndex;
        taaInitialized = true;
        previousTaaEnabled = editorCfg.taaEnabled;

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
            glUniform1f(uniforms.refractScale, materials[currentMatIndex].refractScale);
            glUniform1f(uniforms.aberrationScale, materials[currentMatIndex].aberrationScale);
        } else {
            glUniform1f(uniforms.reflectScale, 0.3f);
            glUniform1f(uniforms.smoothness, 1.0f);
            glUniform1f(uniforms.reliefScale, 0.0f);
            glUniform1f(uniforms.refractScale, 0.0f);
            glUniform1f(uniforms.aberrationScale, 0.0f);
        }

        glm::mat4 model(1.0f);

        if (!IsMaterialCreatorOpen() && skyboxTexture != 0 && skyboxShader != 0) {
            glUseProgram(skyboxShader);
            const glm::mat4 skyboxView = glm::mat4(glm::mat3(view));
            glUniformMatrix4fv(skyboxViewUniform, 1, GL_FALSE, glm::value_ptr(skyboxView));
            glUniformMatrix4fv(skyboxProjectionUniform, 1, GL_FALSE, glm::value_ptr(proj));
            glUniform1i(skyboxSamplerUniform, 4);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTexture);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            glBindVertexArray(VAO_cube);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }

        glUseProgram(shader);
        if (!IsMaterialCreatorOpen() && skyboxTexture != 0) {
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTexture);
        }
        glUniformMatrix4fv(uniforms.view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(uniforms.projection, 1, GL_FALSE, glm::value_ptr(proj));
        glUniformMatrix4fv(uniforms.model, 1, GL_FALSE, glm::value_ptr(model));

        glUniform1i(uniforms.useDiffuse, 0);
        glUniform1i(uniforms.useDiffuseAlpha, 0);
        glUniform1i(uniforms.useNormal, 0);
        glUniform1i(uniforms.normalMapMode, 0);
        glUniform1i(uniforms.hasNormalMap, 0);
        glUniform1i(uniforms.diffuseIsSRGB, 0);
        glUniform1i(uniforms.glossIsSRGB, 0);
        glUniform1i(uniforms.useGloss, 0);
        glUniform1i(uniforms.useLuma, 0);
        glUniform1i(uniforms.useBump, 0);
        glUniform1i(uniforms.useDetail, 0);
        glUniform2f(uniforms.detailScale, 1.0f, 1.0f);
        glUniform2f(uniforms.textureScale, 1.0f, 1.0f);
        glUniform1i(uniforms.useSkybox, (!IsMaterialCreatorOpen() && skyboxTexture != 0) ? 1 : 0);
        bool diffuseWadTransparencyEnabled = false;

        if (!materials.empty() && currentMatIndex >= 0 && static_cast<std::size_t>(currentMatIndex) < materials.size()) {
            Material& mat = materials[currentMatIndex];
            auto diffuseIt = mat.textures.find("diffuse");
            if (diffuseIt != mat.textures.end() && diffuseIt->second != 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, diffuseIt->second);
                const TextureFormatInfo diffuseInfo = GetTextureFormatInfo(diffuseIt->second);
                // вадовская дифузка уже загружены с декодированными цветами
                // НЕ НАДО ПРОПУСКАТЬ ВАДНИКИ ЧЕРЕЗ SRGB!
                const bool diffuseNeedsNoDecode = !diffuseInfo.valid || diffuseInfo.srgb;
                glUniform1i(uniforms.diffuseIsSRGB, diffuseNeedsNoDecode ? 1 : 0);
                glUniform1i(uniforms.useDiffuse, mat.diffuseVisible ? 1 : 0);
                diffuseWadTransparencyEnabled = mat.diffuseVisible && mat.diffuseWadTransparency;
                glUniform1i(uniforms.useDiffuseAlpha, diffuseWadTransparencyEnabled ? 1 : 0);
            }
            auto normalIt = mat.textures.find("normal");
            if (normalIt != mat.textures.end() && normalIt->second != 0) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, normalIt->second);
                const TextureFormatInfo normalInfo = GetTextureFormatInfo(normalIt->second);
                glUniform1i(uniforms.normalMapMode, normalInfo.bc5 ? 1 : 0);
                glUniform1i(uniforms.hasNormalMap, 1);
                glUniform1i(uniforms.useNormal, useNormal && mat.normalVisible ? 1 : 0);
            }
            auto glossIt = mat.textures.find("gloss");
            if (glossIt != mat.textures.end() && glossIt->second != 0) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, glossIt->second);
                const TextureFormatInfo glossInfo = GetTextureFormatInfo(glossIt->second);
                glUniform1i(uniforms.glossIsSRGB, glossInfo.srgb ? 1 : 0);
                glUniform1i(uniforms.useGloss, useGloss && mat.glossVisible ? 1 : 0);
            }
            auto lumaIt = mat.textures.find("luma");
            if (lumaIt != mat.textures.end() && lumaIt->second != 0) {
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, lumaIt->second);
                glUniform1i(uniforms.useLuma, useLuma && mat.lumaVisible ? 1 : 0);
            }
            auto bumpIt = mat.textures.find("bump");
            if (bumpIt != mat.textures.end() && bumpIt->second != 0) {
                glActiveTexture(GL_TEXTURE5);
                glBindTexture(GL_TEXTURE_2D, bumpIt->second);
                glUniform1i(uniforms.useBump, useBump && mat.bumpVisible ? 1 : 0);
            }
            auto detailIt = mat.textures.find("detail");
            if (detailIt != mat.textures.end() && detailIt->second != 0) {
                glActiveTexture(GL_TEXTURE6);
                glBindTexture(GL_TEXTURE_2D, detailIt->second);
                glUniform1i(uniforms.useDetail, mat.detailVisible ? 1 : 0);

                float detailX = 1.0f;
                float detailY = 1.0f;
                std::istringstream detailStream(mat.detailScale);
                if (!(detailStream >> detailX)) {
                    detailX = 1.0f;
                }
                if (!(detailStream >> detailY)) {
                    detailY = detailX;
                }
                glUniform2f(uniforms.detailScale, detailX, detailY);
            }
            glUniform2f(uniforms.textureScale, std::max(0.1f, mat.textureScaleX), std::max(0.1f, mat.textureScaleY));
        }

        if (modelVisible) {
            if (shapeType == 0) {
                glBindVertexArray(VAO_cube);
                glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
            } else if (shapeType == 1) {
                glBindVertexArray(VAO_sphere);
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sphere.indices.size()), GL_UNSIGNED_INT, nullptr);
            } else if (shapeType >= 2 && shapeType <= 6) {
                customMeshes[static_cast<std::size_t>(shapeType - 2)].draw();
            }
        }

        if (msaaReady) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, viewportFramebuffer.msaaFbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, viewportFramebuffer.fbo);
            glBlitFramebuffer(0, 0, viewportFramebuffer.width, viewportFramebuffer.height, 0, 0, viewportFramebuffer.width, viewportFramebuffer.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, viewportFramebuffer.fbo);
        }

        GLuint outputTexture = viewportFramebuffer.color;
        if (taaReady) {
            RenderTAA(taaShader, viewportFramebuffer.color, taaTarget, taaResetHistory);
            outputTexture = taaTarget.color;
        } else if (fxaaReady) {
            RenderFXAA(fxaaShader, viewportFramebuffer.color, fxaaTarget);
            outputTexture = fxaaTarget.color;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, framebuffer_w, framebuffer_h);
        glClearColor(editorCfg.backgroundColor[0], editorCfg.backgroundColor[1], editorCfg.backgroundColor[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (viewportReady) DrawEditorViewportTexture(outputTexture);

        ++fpsFrames;
        const double fpsNow = glfwGetTime();
        if (fpsNow - fpsTime >= 0.25) {
            fpsValue = static_cast<float>(fpsFrames / (fpsNow - fpsTime));
            fpsFrames = 0;
            fpsTime = fpsNow;
        }
        if (editorCfg.showFps) DrawEditorViewportFPS(fpsValue);

        ImGui::Render();

        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, framebuffer_w, framebuffer_h);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        hasRenderedFrame = true;
    }

    StopContributorRefresh();
    DestroyTAATarget(taaTarget);
    if (taaShader) glDeleteProgram(taaShader);
    DestroyFXAATarget(fxaaTarget);
    DestroyFXAAResources(fxaaShader);
    DestroyViewportFramebuffer(g_viewportFramebuffer);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    ReleaseEditorUIPreview();
    for (auto& material : materials) material.releaseTextures();
    for (auto& mesh : customMeshes) mesh.release();
    if (skyboxTexture != 0) glDeleteTextures(1, &skyboxTexture);
    if (skyboxShader != 0) glDeleteProgram(skyboxShader);
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
