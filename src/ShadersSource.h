#pragma once

#include <string>

// Веретексный шейдер
const std::string vertexShaderSource = R"(
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

    // Правильный расчет нормали
    Normal = mat3(transpose(inverse(model))) * aNormal;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}

)";

// Фрагментный шейдер
const std::string fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

// Входные данные от вершинного шейдера
in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

// Uniforms
uniform sampler2D diffuseMap;
uniform sampler2D normalMap;
uniform sampler2D glossMap;
uniform sampler2D lumaMap;
uniform sampler2D bumpMap;
uniform sampler2D skybox; 

uniform int useDiffuse;
uniform int useNormal;
uniform int useGloss;
uniform int useLuma;
uniform int useBump;

uniform vec3 albedo;
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform float lightIntensity;
uniform float smoothness;
uniform float reflectScale;
uniform float reliefScale;

// Сэмплирование куба из атласа 4x3
vec3 sampleSkybox(vec3 R) {
    vec3 absR = abs(R);
    float maxVal = max(absR.x, max(absR.y, absR.z));
    vec2 uv;
    vec2 offset;
    
    if(absR.y == maxVal) {
        if(R.y > 0.0) { offset = vec2(1.0, 0.0); uv = vec2(R.x, R.z); } 
        else { offset = vec2(1.0, 2.0); uv = vec2(R.x, -R.z); }
    } else if(absR.x == maxVal) {
        if(R.x > 0.0) { offset = vec2(2.0, 1.0); uv = vec2(-R.z, R.y); }
        else { offset = vec2(0.0, 1.0); uv = vec2(R.z, R.y); }
    } else {
        if(R.z > 0.0) { offset = vec2(1.0, 1.0); uv = vec2(R.x, R.y); }
        else { offset = vec2(3.0, 1.0); uv = vec2(-R.x, R.y); }
    }
    uv = (uv / maxVal) * 0.5 + 0.5;
    uv = (uv + offset) / vec2(4.0, 3.0);
    return texture(skybox, uv).rgb;
}

vec2 ParallaxMapping(vec2 texCoords, vec3 viewDir, mat3 TBN) {
    // Переводим направление взгляда в касательное пространство (Tangent Space)
    vec3 V = normalize(viewDir * TBN);
    
    // Получаем высоту из bump карты (используем красный или яркостный канал)
    float height = texture(bumpMap, texCoords).r;
    
    // Смещение координат
    vec2 p = V.xy / (V.z + 0.42) * (height * reliefScale);
    return texCoords - p;
}

void main() {
    vec3 N = normalize(Normal);
    vec3 I = normalize(FragPos - viewPos);
    vec3 V = -I;

    // Расчет базиса TBN (нужен и для Normal map, и для Bump map)
    vec3 Q1 = dFdx(FragPos);
    vec3 Q2 = dFdy(FragPos);
    vec2 st1 = dFdx(TexCoord);
    vec2 st2 = dFdy(TexCoord);
    vec3 T = normalize(Q1 * st2.t - Q2 * st1.t);
    vec3 B = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    // Применяем Parallax / Bump mapping к текстурным координатам если включено
    vec2 sampledTexCoord = TexCoord;
    if (useBump == 1) {
        sampledTexCoord = ParallaxMapping(TexCoord, V, TBN);
    }

    vec3 baseColor = albedo;
    if (useDiffuse == 1) baseColor = texture(diffuseMap, sampledTexCoord).rgb;
    
    if (useNormal == 1) {
        vec3 mapN = texture(normalMap, sampledTexCoord).rgb * 2.0 - 1.0;
        N = normalize(TBN * mapN);
    }
    
    vec3 L = normalize(lightPos - FragPos);
    vec3 H = normalize(L + V);
    vec3 R = reflect(I, N);

    float ao = 1.0;
    float specIntensity = 0.0;
    if (useGloss == 1) {
        vec4 glossData = texture(glossMap, sampledTexCoord);
        ao = mix(1.0, glossData.b, 0.5);
        specIntensity = dot(glossData.rgb, vec3(0.333));
    }

    float diff = max(dot(N, L), 0.0);
    float fresnel = pow(1.0 - max(dot(V, N), 0.0), 5.0);
    fresnel = 0.02 + (0.98) * fresnel;

    float spec = 0.0;
    if (useGloss == 1) {
        float shininess = 32.0; 
        float dotNH = max(dot(N, H), 0.0);
        spec = pow(dotNH, shininess);
        float glossVal = texture(glossMap, sampledTexCoord).r;
        spec *= (glossVal) * smoothness; 
    }
    
    vec3 envColor = vec3(0.05);
    vec3 reflection = envColor * fresnel * reflectScale * (0.5 + smoothness * 0.5);
    
    vec3 ambient = vec3(0.05) * ao; 
    vec3 lightFactor = lightColor * lightIntensity;
    vec3 lighting = (ambient + diff * lightFactor) * baseColor + (spec) * lightFactor;
    
    if (useLuma == 1) {
        lighting += texture(lumaMap, sampledTexCoord).rgb * 8.0; 
    }
    
    FragColor = vec4(lighting, 1.0);
}
)";