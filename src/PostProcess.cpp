#include "PostProcess.h"
#include <iostream>
#include <algorithm>

namespace {
GLuint g_quadVAO = 0;
GLuint g_quadVBO = 0;

struct TAAUniformLocations {
    GLuint program = 0;
    GLint source = -1;
    GLint history = -1;
    GLint inverseResolution = -1;
    GLint resetHistory = -1;
};

struct FXAAUniformLocations {
    GLuint program = 0;
    GLint source = -1;
    GLint inverseResolution = -1;
};

TAAUniformLocations g_taaUniforms;
FXAAUniformLocations g_fxaaUniforms;

const TAAUniformLocations& GetTAAUniforms(GLuint program) {
    if (g_taaUniforms.program != program) {
        g_taaUniforms.program = program;
        g_taaUniforms.source = glGetUniformLocation(program, "source");
        g_taaUniforms.history = glGetUniformLocation(program, "history");
        g_taaUniforms.inverseResolution = glGetUniformLocation(program, "inverseResolution");
        g_taaUniforms.resetHistory = glGetUniformLocation(program, "resetHistory");
    }
    return g_taaUniforms;
}

const FXAAUniformLocations& GetFXAAUniforms(GLuint program) {
    if (g_fxaaUniforms.program != program) {
        g_fxaaUniforms.program = program;
        g_fxaaUniforms.source = glGetUniformLocation(program, "source");
        g_fxaaUniforms.inverseResolution = glGetUniformLocation(program, "inverseResolution");
    }
    return g_fxaaUniforms;
}

const char* kVertexShader = R"SHADER(
#version 330 core
out vec2 TexCoord;
void main() {
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0)
    );
    vec2 p = positions[gl_VertexID];
    TexCoord = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)SHADER";


const char* kTAAFragmentShader = R"SHADER(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D source;
uniform sampler2D history;
uniform vec2 inverseResolution;
uniform int resetHistory;

vec3 SampleColor(vec2 uv) {
    return texture(source, uv).rgb;
}

void main() {
    vec4 currentSample = texture(source, TexCoord);
    vec3 current = currentSample.rgb;
    if (resetHistory != 0) {
        FragColor = currentSample;
        return;
    }

    vec3 minimum = current;
    vec3 maximum = current;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec3 sampleColor = SampleColor(TexCoord + vec2(float(x), float(y)) * inverseResolution);
            minimum = min(minimum, sampleColor);
            maximum = max(maximum, sampleColor);
        }
    }

    vec3 previous = texture(history, TexCoord).rgb;
    previous = clamp(previous, minimum, maximum);
    FragColor = vec4(mix(current, previous, 0.82), currentSample.a);
}
)SHADER";

const char* kFragmentShader = R"SHADER(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D source;
uniform vec2 inverseResolution;

float Luma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main() {
    const float EDGE_THRESHOLD_MIN = 0.0312;
    const float EDGE_THRESHOLD_MAX = 0.125;
    const float SUBPIXEL_QUALITY = 0.75;

    vec4 centerSample = texture(source, TexCoord);
    vec3 rgbM = centerSample.rgb;
    vec3 rgbNW = texture(source, TexCoord + vec2(-1.0, -1.0) * inverseResolution).rgb;
    vec3 rgbNE = texture(source, TexCoord + vec2(1.0, -1.0) * inverseResolution).rgb;
    vec3 rgbSW = texture(source, TexCoord + vec2(-1.0, 1.0) * inverseResolution).rgb;
    vec3 rgbSE = texture(source, TexCoord + vec2(1.0, 1.0) * inverseResolution).rgb;

    float lumaM = Luma(rgbM);
    float lumaNW = Luma(rgbNW);
    float lumaNE = Luma(rgbNE);
    float lumaSW = Luma(rgbSW);
    float lumaSE = Luma(rgbSE);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    float lumaRange = lumaMax - lumaMin;

    if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX)) {
        FragColor = centerSample;
        return;
    }

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max(
        (lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * SUBPIXEL_QUALITY),
        1.0 / 128.0
    );
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * inverseResolution;

    vec3 rgbA = 0.5 * (
        texture(source, TexCoord + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture(source, TexCoord + dir * (2.0 / 3.0 - 0.5)).rgb
    );

    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(source, TexCoord + dir * -0.5).rgb +
        texture(source, TexCoord + dir * 0.5).rgb
    );

    float lumaB = Luma(rgbB);
    if (lumaB < lumaMin || lumaB > lumaMax) {
        FragColor = vec4(rgbA, centerSample.a);
    } else {
        FragColor = vec4(rgbB, centerSample.a);
    }
}
)SHADER";

void EnsureQuad() {
    if (g_quadVAO != 0) return;
    glGenVertexArrays(1, &g_quadVAO);
}
}


GLuint LoadTAAShader() {
    GLint success = GL_FALSE;
    char infoLog[4096] = {};
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &kVertexShader, nullptr);
    glCompileShader(vertex);
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertex, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR: TAA vertex shader compilation failed:\n" << infoLog << std::endl;
        glDeleteShader(vertex);
        return 0;
    }

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &kTAAFragmentShader, nullptr);
    glCompileShader(fragment);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragment, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR: TAA fragment shader compilation failed:\n" << infoLog << std::endl;
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR: TAA program linking failed:\n" << infoLog << std::endl;
        glDeleteProgram(program);
        program = 0;
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

void DestroyTAATarget(TAATarget& target) {
    if (target.history) glDeleteTextures(1, &target.history);
    if (target.color) glDeleteTextures(1, &target.color);
    if (target.fbo) glDeleteFramebuffers(1, &target.fbo);
    target = {};
}

bool EnsureTAATarget(TAATarget& target, int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (target.fbo != 0 && target.width == width && target.height == height) return true;
    DestroyTAATarget(target);

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

    glGenTextures(1, &target.history);
    glBindTexture(GL_TEXTURE_2D, target.history);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        DestroyTAATarget(target);
        return false;
    }
    target.width = width;
    target.height = height;
    target.historyValid = false;
    return true;
}

void RenderTAA(GLuint shader, GLuint sourceTexture, TAATarget& target, bool resetHistory) {
    if (shader == 0 || sourceTexture == 0 || target.fbo == 0 || target.history == 0) return;
    EnsureQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.width, target.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(shader);
    const TAAUniformLocations& uniforms = GetTAAUniforms(shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glUniform1i(uniforms.source, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, target.history);
    glUniform1i(uniforms.history, 1);
    glUniform2f(uniforms.inverseResolution, 1.0f / target.width, 1.0f / target.height);
    glUniform1i(uniforms.resetHistory, (resetHistory || !target.historyValid) ? 1 : 0);
    glBindVertexArray(g_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glBindTexture(GL_TEXTURE_2D, target.history);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, target.width, target.height);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    target.historyValid = true;
}

GLuint LoadFXAAShader() {
    GLint success = GL_FALSE;
    char infoLog[4096] = {};
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &kVertexShader, nullptr);
    glCompileShader(vertex);
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertex, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR: FXAA vertex shader compilation failed:\n" << infoLog << std::endl;
        glDeleteShader(vertex);
        return 0;
    }

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &kFragmentShader, nullptr);
    glCompileShader(fragment);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragment, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR: FXAA fragment shader compilation failed:\n" << infoLog << std::endl;
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR: FXAA program linking failed:\n" << infoLog << std::endl;
        glDeleteProgram(program);
        program = 0;
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

void DestroyFXAATarget(FXAATarget& target) {
    if (target.color) glDeleteTextures(1, &target.color);
    if (target.fbo) glDeleteFramebuffers(1, &target.fbo);
    target = {};
}

bool EnsureFXAATarget(FXAATarget& target, int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (target.fbo != 0 && target.width == width && target.height == height) return true;
    DestroyFXAATarget(target);

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
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        DestroyFXAATarget(target);
        return false;
    }
    target.width = width;
    target.height = height;
    return true;
}

void RenderFXAA(GLuint shader, GLuint sourceTexture, FXAATarget& target) {
    if (shader == 0 || sourceTexture == 0 || target.fbo == 0) return;
    EnsureQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.width, target.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(shader);
    const FXAAUniformLocations& uniforms = GetFXAAUniforms(shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glUniform1i(uniforms.source, 0);
    glUniform2f(uniforms.inverseResolution, 1.0f / target.width, 1.0f / target.height);
    glBindVertexArray(g_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void DestroyFXAAResources(GLuint& shader) {
    if (shader) glDeleteProgram(shader);
    shader = 0;
    if (g_quadVBO) glDeleteBuffers(1, &g_quadVBO);
    if (g_quadVAO) glDeleteVertexArrays(1, &g_quadVAO);
    g_quadVBO = 0;
    g_quadVAO = 0;
}
