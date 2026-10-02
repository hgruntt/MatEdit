#include "Shader.h"
#include <iostream>

namespace {
const char* kVertexShader = R"SHADER(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec2 TexCoord;
out vec3 Normal;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    TexCoord = aTexCoord;

    Normal = mat3(transpose(inverse(model))) * aNormal;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)SHADER";

const char* kFragmentShader = R"SHADER(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

uniform sampler2D diffuseMap;
uniform sampler2D normalMap;
uniform sampler2D glossMap;
uniform sampler2D lumaMap;
uniform sampler2D bumpMap;
uniform sampler2D detailMap;
uniform samplerCube skybox;
uniform int useSkybox;

uniform int useDiffuse;
uniform int useDiffuseAlpha;
uniform int useNormal;
uniform int normalMapMode;
uniform int hasNormalMap;
uniform int diffuseIsSRGB;
uniform int glossIsSRGB;
uniform int useGloss;
uniform int useLuma;
uniform int useBump;
uniform int useDetail;

uniform vec3 albedo;
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform float lightIntensity;
uniform float smoothness;
uniform float glossIntensity;
uniform float reflectScale;
uniform float reliefScale;
uniform float refractScale;
uniform float aberrationScale;
uniform vec2 detailScale;
uniform vec2 textureScale;

float ComputeLOD(const vec2 texCoord) {
    vec2 dx = dFdx(texCoord);
    vec2 dy = dFdy(texCoord);
    vec2 mag = (abs(dx) + abs(dy)) * vec2(textureSize(bumpMap, 0));
    return log2(max(mag.x, mag.y));
}

float GetHeightMapSample(const vec2 texCoord) {
    return texture(bumpMap, texCoord).r;
}

float GetHeightMapSampleLOD(const vec2 texCoord, float lod) {
    return textureLod(bumpMap, texCoord, lod).r;
}

float GetDepthMapSample(const vec2 texCoord) {
    return 1.0 - GetHeightMapSample(texCoord);
}

float GetDepthMapSampleLOD(const vec2 texCoord, float lod) {
    return 1.0 - GetHeightMapSampleLOD(texCoord, lod);
}

float GetPrimeXTNormalZ(const vec2 texCoord) {
    if (hasNormalMap == 0) return 1.0;
    vec4 sampleValue = texture(normalMap, texCoord);
    if (normalMapMode == 1) {
        vec2 xy = sampleValue.rg * 2.0 - 1.0;
        float z = 1.0 - min(dot(xy, xy), 1.0);
        return normalize(vec3(xy, z)).z;
    }
    return normalize(sampleValue.rgb * 2.0 - 1.0).z;
}

vec2 ParallaxOffsetMap(const vec2 texCoord, const vec3 viewVec) {
    float bumpScale = reliefScale * 0.1;
    vec3 newCoords = vec3(texCoord, 0.0);
    float lod = ComputeLOD(texCoord);

    for (int i = 0; i < 15; ++i) {
        float nz = GetPrimeXTNormalZ(newCoords.xy);
        float h = GetHeightMapSampleLOD(newCoords.xy, lod);
        float height = h * bumpScale;
        newCoords += (height - newCoords.z) * nz * vec3(viewVec.x, -viewVec.y, viewVec.z);
    }

    return newCoords.xy;
}

vec3 ParallaxOcclusionMap(const vec2 texCoord, const vec3 viewVec) {
    const float PARALLAX_STEPS = 15.0;
    float stepSize = 1.0 / PARALLAX_STEPS;
    float bumpScale = 0.2 * reliefScale;
    float lod = ComputeLOD(texCoord);

    vec2 delta = bumpScale * vec2(viewVec.x, -viewVec.y) / (viewVec.z * PARALLAX_STEPS);

    float depth0 = GetDepthMapSample(texCoord);
    float currentLayer = 1.0 - stepSize;
    vec2 offset = texCoord + delta;
    float depth1 = GetDepthMapSample(offset);

    for (int i = 0; i < int(PARALLAX_STEPS); ++i) {
        if (depth1 >= currentLayer) {
            break;
        }

        depth0 = depth1;
        currentLayer -= stepSize;
        offset += delta;
        depth1 = GetDepthMapSampleLOD(offset, lod);
    }

    vec2 offsetBest = offset;
    float error = 1.0;
    float layer1 = currentLayer;
    float layer0 = layer1 + stepSize;
    float delta1 = layer1 - depth1;
    float delta0 = layer0 - depth0;
    vec4 intersect = vec4(delta * PARALLAX_STEPS, delta * PARALLAX_STEPS + texCoord);
    float t = 0.0;

    for (int i = 0; i < 10; ++i) {
        if (abs(error) <= 0.01) {
            break;
        }

        float denom = delta1 - delta0;
        t = (layer0 * delta1 - layer1 * delta0) / denom;
        offsetBest = -t * intersect.xy + intersect.zw;

        float depth = GetDepthMapSampleLOD(offsetBest, lod);
        error = t - depth;
        if (error < 0.0) {
            delta1 = error;
            layer1 = t;
        } else {
            delta0 = error;
            layer0 = t;
        }
    }

    return vec3(offsetBest, t);
}

vec3 FresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 SRGBToLinear(vec3 value) {
    vec3 linearRGBLo = value / 12.92;
    vec3 linearRGBHi = pow((value + 0.055) / 1.055, vec3(2.4));
    return mix(linearRGBLo, linearRGBHi, greaterThan(value, vec3(0.04045)));
}

void main() {
    vec3 N = normalize(Normal);
    vec3 I = normalize(FragPos - viewPos);
    vec3 V = -I;

    vec3 Q1 = dFdx(FragPos);
    vec3 Q2 = dFdy(FragPos);
    vec2 st1 = dFdx(TexCoord);
    vec2 st2 = dFdy(TexCoord);
    float uvDet = st1.x * st2.y - st1.y * st2.x;
    vec3 T = normalize(Q1 * st2.y - Q2 * st1.y);
    vec3 B = normalize(Q2 * st1.x - Q1 * st2.x);
    T = normalize(T - N * dot(N, T));
    B = normalize(cross(N, T));
    if (uvDet < 0.0) B = -B;
    mat3 TBN = mat3(T, B, N);

    vec3 L = normalize(lightPos - FragPos);
    vec3 lightDirTangent = normalize(transpose(TBN) * L);
    vec3 viewDirTangent = normalize(transpose(TBN) * V);

    vec2 tiledTexCoord = TexCoord * textureScale;
    vec2 sampledTexCoord = tiledTexCoord;
    float shadowFactor = 1.0;

    if (useBump == 1 && reliefScale > 0.0) {
        vec3 tangentView = normalize(viewDirTangent);
        vec3 pomResult = ParallaxOcclusionMap(tiledTexCoord, tangentView);
        sampledTexCoord = pomResult.xy;
        shadowFactor = 1.0;
    }

    vec3 baseColor = albedo;
    if (useDiffuse == 1) {
        vec4 diffuseSample = texture(diffuseMap, sampledTexCoord);
        if (useDiffuseAlpha == 1 && diffuseSample.a < 0.25) discard;
        vec3 diffuseColor = diffuseSample.rgb;
        baseColor = diffuseIsSRGB == 1 ? diffuseColor : SRGBToLinear(diffuseColor);
    }

    if (useDetail == 1) {
        vec3 detail = texture(detailMap, tiledTexCoord * detailScale).rgb;
        baseColor *= detail * 2.0;
    }

    vec3 tangentSurfaceNormal = vec3(0.0, 0.0, 1.0);

    if (useNormal == 1) {
        vec4 normalSample = texture(normalMap, sampledTexCoord);
        if (normalMapMode == 1) {
            vec2 encodedNormal = normalSample.rg * 2.0 - 1.0;
            encodedNormal.y = -encodedNormal.y;
            float xyLengthSq = min(dot(encodedNormal, encodedNormal), 1.0);
            float normalZ = sqrt(max(0.0, 1.0 - xyLengthSq));
            tangentSurfaceNormal = normalize(vec3(encodedNormal, normalZ));
        } else {
            tangentSurfaceNormal = normalize(normalSample.rgb * 2.0 - 1.0);
            tangentSurfaceNormal.y = -tangentSurfaceNormal.y;
        }
    }

    if (useNormal == 1) {
        N = normalize(TBN * tangentSurfaceNormal);
    }

    float glossSpecularIntensity = clamp(smoothness, 0.0, 1.0);

    if (useGloss == 1) {
        vec4 glossData = texture(glossMap, sampledTexCoord);
        vec3 glossColor = glossIsSRGB == 1 ? glossData.rgb : SRGBToLinear(glossData.rgb);
        glossSpecularIntensity = clamp(glossColor.r, 0.0, 1.0);
    }
    glossSpecularIntensity *= clamp(glossIntensity, 0.0, 1.0);

    float NdotL = max(dot(N, L), 0.0);
    vec3 H = normalize(V + L);

    float specular = pow(max(dot(N, H), 0.0), 32.0) * glossSpecularIntensity;
    vec3 directSpecular = (lightColor * lightIntensity) * NdotL * specular * shadowFactor;
    vec3 directDiffuse = baseColor * (lightColor * lightIntensity) * NdotL * shadowFactor;

    vec3 ambient = baseColor * vec3(0.05);
    vec3 lighting = ambient + directDiffuse + directSpecular;

    if (useLuma == 1) {
        lighting += texture(lumaMap, sampledTexCoord).rgb;
    }

    vec3 reflection = vec3(0.0);
    if (useSkybox == 1) {
        vec3 reflectDir = normalize(reflect(I, N));
        if (abs(reflectDir.y) > max(abs(reflectDir.x), abs(reflectDir.z))) {
            reflectDir = vec3(-reflectDir.z, reflectDir.y, reflectDir.x);
        }
        vec3 reflected = texture(skybox, reflectDir).rgb;
        float reflectionCosTheta = dot(V, N);
        float reflectionFresnel = reflectionCosTheta >= 0.0
            ? 0.02 + 0.98 * pow(clamp(1.0 - reflectionCosTheta, 0.0, 1.0), 5.0)
            : 0.0;
        float reflectAmount = max(reflectScale, 0.0) * glossSpecularIntensity;
        reflection = reflected * reflectionFresnel * reflectAmount;

        float refractAmount = max(refractScale, 0.0);
        if (refractAmount > 0.0) {
            const float eta = 0.82;
            vec3 refractDir = normalize(refract(I, N, eta));
            float chroma = max(aberrationScale, 0.0) * 0.02;
            vec3 refracted;
            vec3 refractDirR = normalize(refractDir + vec3(chroma, 0.0, 0.0));
            vec3 refractDirG = refractDir;
            vec3 refractDirB = normalize(refractDir - vec3(chroma, 0.0, 0.0));
            if (abs(refractDirR.y) > max(abs(refractDirR.x), abs(refractDirR.z))) refractDirR = vec3(-refractDirR.z, refractDirR.y, refractDirR.x);
            if (abs(refractDirG.y) > max(abs(refractDirG.x), abs(refractDirG.z))) refractDirG = vec3(-refractDirG.z, refractDirG.y, refractDirG.x);
            if (abs(refractDirB.y) > max(abs(refractDirB.x), abs(refractDirB.z))) refractDirB = vec3(-refractDirB.z, refractDirB.y, refractDirB.x);
            refracted.r = texture(skybox, refractDirR).r;
            refracted.g = texture(skybox, refractDirG).g;
            refracted.b = texture(skybox, refractDirB).b;
            reflection += refracted * refractAmount;
        }
    }

    FragColor = vec4(max(lighting + reflection, vec3(0.0)), 1.0);
}
)SHADER";
}

GLuint LoadShader() {
    GLint success = GL_FALSE;
    char infoLog[4096] = {};

    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &kVertexShader, nullptr);
    glCompileShader(vertex);
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertex, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR: Embedded vertex shader compilation failed:\n" << infoLog << std::endl;
        glDeleteShader(vertex);
        return 0;
    }

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &kFragmentShader, nullptr);
    glCompileShader(fragment);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragment, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR: Embedded fragment shader compilation failed:\n" << infoLog << std::endl;
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
        std::cerr << "ERROR: Embedded shader program linking failed:\n" << infoLog << std::endl;
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}


namespace {
const char* kSkyboxVertexShader = R"SHADER(
#version 330 core
layout (location = 0) in vec3 aPos;
out vec3 TexCoord;
uniform mat4 view;
uniform mat4 projection;
void main() {
    TexCoord = aPos;
    vec4 position = projection * view * vec4(aPos, 1.0);
    gl_Position = position.xyww;
}
)SHADER";

const char* kSkyboxFragmentShader = R"SHADER(
#version 330 core
in vec3 TexCoord;
out vec4 FragColor;
uniform samplerCube skybox;

void main() {
    vec3 direction = TexCoord;
    if (abs(direction.y) > max(abs(direction.x), abs(direction.z))) {
        direction = vec3(-direction.z, direction.y, direction.x);
    }
    FragColor = texture(skybox, direction);
}
)SHADER";
}

GLuint LoadSkyboxShader() {
    GLint success = GL_FALSE;
    char infoLog[4096] = {};
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &kSkyboxVertexShader, nullptr);
    glCompileShader(vertex);
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertex, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR: Embedded skybox vertex shader compilation failed:\n" << infoLog << std::endl;
        glDeleteShader(vertex);
        return 0;
    }
    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &kSkyboxFragmentShader, nullptr);
    glCompileShader(fragment);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragment, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "ERROR: Embedded skybox fragment shader compilation failed:\n" << infoLog << std::endl;
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
        std::cerr << "ERROR: Embedded skybox shader program linking failed:\n" << infoLog << std::endl;
        glDeleteProgram(program);
        program = 0;
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}
