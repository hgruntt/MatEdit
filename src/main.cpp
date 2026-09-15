#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <thread>
#include <chrono>
#include <algorithm>

#include "Config.h"
#include "MaterialSystem.h"
#include "Shader.h"
#include "EditorUI.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "cube_data.h"
#include "sphere_data.h"

int main() {
    if (!glfwInit()) return -1;
   
    EditorConfig editorCfg;
    LoadConfig(editorCfg);
    gameRootPath = editorCfg.gamePath.empty() ? fs::current_path() : fs::path(editorCfg.gamePath);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1200, 900, "MaterialEditor", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;


    std::vector<Material> materials;
    std::vector<PhysicalMaterialEntry> physicalMaterials;
    std::vector<std::string> matFiles;
    std::string currentFileName = "None";
    int currentMatIndex = 0;
    std::string currentDefFile = "scripts/materials.def";
    int currentPhysMatIndex = 0;
    int shapeType = 0; int lightMode = 0;
    bool useNormal = true; bool useGloss = true; bool useLuma = true; bool useBump = true;
    float lightIntensity = 1.0f;
    float lightColor[3] = {1.0f, 1.0f, 1.0f};

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
            auto it = std::find(matFiles.begin(), matFiles.end(), std::string(editorCfg.lastMatFile));
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

        // Загрузка physical materials (.def)
        fs::path defPath = scriptsDir / "materials.def";
        if (!fs::exists(defPath)) defPath = gameRootPath / "materials.def";
        if (fs::exists(defPath)) {
            currentDefFile = fs::relative(defPath, gameRootPath).string();
            LoadAllPhysicalMaterials(currentDefFile, physicalMaterials);
            currentPhysMatIndex = 0;
            if (!physicalMaterials.empty()) physicalMaterials[currentPhysMatIndex].updateBuffers();
        }
    };

    refreshData(currentFileName, currentMatIndex);

    GLuint shader = LoadShader("basic.vert", "basic.frag");
    GLuint skyboxID = LoadSkyboxAs2D("textures/sky");

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
    
    glm::vec3 albedo(1.0f, 0.0f, 0.0f);
    float metallic = 0.5f;
    float roughness = 0.5f;
    static float zoom = 2.0f;
    glm::vec3 lightPos(2.0f, 2.0f, 2.0f);

    glUseProgram(shader);
    glUniform1i(glGetUniformLocation(shader, "diffuseMap"), 0);
    glUniform1i(glGetUniformLocation(shader, "normalMap"), 1);
    glUniform1i(glGetUniformLocation(shader, "glossMap"), 2);
    glUniform1i(glGetUniformLocation(shader, "lumaMap"), 3);
    glUniform1i(glGetUniformLocation(shader, "bumpMap"), 5); // Слот для Bump карты

  IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    SetupModernDarkStyle();

    // Переменные для ограничения FPS (60 FPS)
    const double targetFPS = 60.0;
    const double targetFrameTime = 1.0 / targetFPS;
    double lastFrameTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Ограничение FPS: вычисляем время кадра и при необходимости ждем
        double currentTime = glfwGetTime();
        double elapsedTime = currentTime - lastFrameTime;
        if (elapsedTime < targetFrameTime) {
            double sleepTime = targetFrameTime - elapsedTime;
            std::this_thread::sleep_for(std::chrono::milliseconds((int)(sleepTime * 1000.0)));
        }
        lastFrameTime = glfwGetTime();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);

        DrawEditorUI(display_w, display_h, editorCfg, materials, physicalMaterials, matFiles, currentFileName, currentMatIndex, 
                     currentDefFile, currentPhysMatIndex, shapeType, lightMode, useNormal, useGloss, useLuma, useBump,
                     lightIntensity, lightColor, skyboxID, refreshData);

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

       if (!materials.empty() && currentMatIndex >= 0 && static_cast<size_t>(currentMatIndex) < materials.size()) {
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
        glUniform1i(glGetUniformLocation(shader, "useBump"), 0);
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
            if (mat.textures.count("bump") && mat.textures["bump"] != 0) {
                glActiveTexture(GL_TEXTURE5);
                glBindTexture(GL_TEXTURE_2D, mat.textures["bump"]);
                glUniform1i(glGetUniformLocation(shader, "bumpMap"), 5);
                glUniform1i(glGetUniformLocation(shader, "useBump"), useBump ? 1 : 0);
            }
        }

        glUniform3fv(glGetUniformLocation(shader, "albedo"), 1, &albedo.r);
        glUniform1f(glGetUniformLocation(shader, "metallic"), metallic);
        glUniform1f(glGetUniformLocation(shader, "roughness"), roughness);
        glUniform3fv(glGetUniformLocation(shader, "lightPos"), 1, &finalLightPos.x);
        glUniform1f(glGetUniformLocation(shader, "lightIntensity"), lightIntensity);
        if (!materials.empty()) {
            Material& mat = materials[currentMatIndex];
            glUniform1f(glGetUniformLocation(shader, "reliefScale"), mat.reliefScale);
        }
        if(shapeType == 0) {
            glBindVertexArray(VAO_cube);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        } else {
            glBindVertexArray(VAO_sphere);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sphere.indices.size()), GL_UNSIGNED_INT, 0);
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

