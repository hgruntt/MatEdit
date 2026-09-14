#include "Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>

GLuint LoadShader(const char* vertexPath, const char* fragmentPath) {
    auto readShader = [](const char* path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "ERROR: Failed to open shader file: " << path << std::endl;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    };

    std::string vCode = readShader(vertexPath);
    std::string fCode = readShader(fragmentPath);
    const char* vSource = vCode.c_str();
    const char* fSource = fCode.c_str();

    int success;
    char infoLog[512];

    // Вершинный шейдер
    GLuint v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, &vSource, NULL);
    glCompileShader(v);
    glGetShaderiv(v, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(v, 512, NULL, infoLog);
        std::cerr << "ERROR: Vertex shader compilation failed (" << vertexPath << "):\n" << infoLog << std::endl;
    }

    // Фрагментный шейдер
    GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, &fSource, NULL);
    glCompileShader(f);
    glGetShaderiv(f, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(f, 512, NULL, infoLog);
        std::cerr << "ERROR: Fragment shader compilation failed (" << fragmentPath << "):\n" << infoLog << std::endl;
    }

    // Шейдерная программа
    GLuint program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cerr << "ERROR: Shader program linking failed:\n" << infoLog << std::endl;
    }

    glDeleteShader(v);
    glDeleteShader(f);

    return program;
}
