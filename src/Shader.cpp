#include "Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::filesystem::path ResolveShaderPath(const char* path) {
    std::filesystem::path input(path);
    if (input.is_absolute() || std::filesystem::exists(input)) return input;

#ifdef _WIN32
    char buffer[4096] = {};
    unsigned long length = GetModuleFileNameA(nullptr, buffer, static_cast<unsigned long>(sizeof(buffer)));
    if (length > 0 && length < sizeof(buffer)) {
        return std::filesystem::path(buffer).parent_path() / input;
    }
#else
    std::error_code ec;
    std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !executable.empty()) return executable.parent_path() / input;
#endif

    return input;
}
}

GLuint LoadShader(const char* vertexPath, const char* fragmentPath) {
    auto readShader = [](const std::filesystem::path& path) -> std::string {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "ERROR: Failed to open shader file: " << path.string() << std::endl;
            return {};
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    };

    const std::filesystem::path resolvedVertexPath = ResolveShaderPath(vertexPath);
    const std::filesystem::path resolvedFragmentPath = ResolveShaderPath(fragmentPath);
    std::string vCode = readShader(resolvedVertexPath);
    std::string fCode = readShader(resolvedFragmentPath);
    if (vCode.empty() || fCode.empty()) return 0;

    const char* vSource = vCode.c_str();
    const char* fSource = fCode.c_str();
    GLint success = GL_FALSE;
    char infoLog[2048] = {};

    GLuint v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, &vSource, nullptr);
    glCompileShader(v);
    glGetShaderiv(v, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(v, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR: Vertex shader compilation failed (" << resolvedVertexPath.string() << "):\n" << infoLog << std::endl;
        glDeleteShader(v);
        return 0;
    }

    GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, &fSource, nullptr);
    glCompileShader(f);
    glGetShaderiv(f, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(f, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR: Fragment shader compilation failed (" << resolvedFragmentPath.string() << "):\n" << infoLog << std::endl;
        glDeleteShader(v);
        glDeleteShader(f);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR: Shader program linking failed:\n" << infoLog << std::endl;
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(v);
    glDeleteShader(f);
    return program;
}
