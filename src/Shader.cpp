#include "Shader.h"
#include <fstream>
#include <sstream>

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

    glDeleteShader(v);
    glDeleteShader(f);

    return program;
}
