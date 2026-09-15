#include "MaterialSystem.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <gli/gli.hpp>
#define _CRT_SECURE_NO_WARNINGS

fs::path gameRootPath = "";
std::vector<std::string> physicalMaterialTypes = {"default"};

std::vector<std::string> LoadPhysicalMaterialTypes() {
    std::vector<std::string> types;
    fs::path defPath = gameRootPath / "scripts" / "materials.def";
    if (!fs::exists(defPath)) {
        defPath = gameRootPath / "materials.def";
    }
    
    std::ifstream file(defPath);
    if (!file.is_open()) {
        return {"default"};
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

namespace {
void CopyString(char* destination, std::size_t capacity, const std::string& value) {
    if (capacity == 0) return;
    std::strncpy(destination, value.c_str(), capacity - 1);
    destination[capacity - 1] = '\0';
}

GLuint UploadDDS2D(const gli::texture& texture, const std::string& sourcePath) {
    if (texture.empty() || texture.levels() == 0) return 0;

    gli::gl GL(gli::gl::PROFILE_GL33);
    const gli::gl::format Format = GL.translate(texture.format(), texture.swizzles());
    if (Format.Internal == GL_NONE) {
        std::cerr << "Unsupported DDS format: " << sourcePath << std::endl;
        return 0;
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(texture.levels() - 1));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture.levels() > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const bool compressed = Format.External == GL_NONE || Format.Type == GL_NONE;
    for (std::size_t level = 0; level < texture.levels(); ++level) {
        const GLsizei width = static_cast<GLsizei>(texture.extent(level).x);
        const GLsizei height = static_cast<GLsizei>(texture.extent(level).y);
        const GLsizei size = static_cast<GLsizei>(texture.size(level));

        if (compressed) {
            glCompressedTexImage2D(GL_TEXTURE_2D,
                                   static_cast<GLint>(level),
                                   Format.Internal,
                                   width,
                                   height,
                                   0,
                                   size,
                                   texture.data(0, 0, level));
        } else {
            glTexImage2D(GL_TEXTURE_2D,
                         static_cast<GLint>(level),
                         Format.Internal,
                         width,
                         height,
                         0,
                         Format.External,
                         Format.Type,
                         texture.data(0, 0, level));
        }
    }

    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &textureID);
        std::cerr << "Failed to upload DDS texture: " << sourcePath << std::endl;
        return 0;
    }

    return textureID;
}
}

GLuint LoadDDSTexture(const std::string& path) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::string ddsFilePath = fullPath.string() + ".dds";
    gli::texture texture = gli::load(ddsFilePath);
    if (texture.empty()) {
        std::cerr << "Failed to load DDS: " << ddsFilePath << std::endl;
        return 0;
    }
    return UploadDDS2D(texture, ddsFilePath);
}

GLuint LoadDDS_Cubemap(const std::string& path) {
    gli::texture texture = gli::load(path + ".dds");
    if (texture.empty()) return 0;

    gli::gl GL(gli::gl::PROFILE_GL33);
    const gli::gl::format Format = GL.translate(texture.format(), texture.swizzles());
    if (Format.Internal == GL_NONE || texture.faces() != 6) return 0;

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(texture.levels() - 1));
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, texture.levels() > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    const bool compressed = Format.External == GL_NONE || Format.Type == GL_NONE;
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t level = 0; level < texture.levels(); ++level) {
            const GLsizei width = static_cast<GLsizei>(texture.extent(level).x);
            const GLsizei height = static_cast<GLsizei>(texture.extent(level).y);
            const GLsizei size = static_cast<GLsizei>(texture.size(level));
            const GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face);

            if (compressed) {
                glCompressedTexImage2D(target,
                                       static_cast<GLint>(level),
                                       Format.Internal,
                                       width,
                                       height,
                                       0,
                                       size,
                                       texture.data(face, 0, level));
            } else {
                glTexImage2D(target,
                             static_cast<GLint>(level),
                             Format.Internal,
                             width,
                             height,
                             0,
                             Format.External,
                             Format.Type,
                             texture.data(face, 0, level));
            }
        }
    }

    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &textureID);
        return 0;
    }
    return textureID;
}

GLuint LoadSkyboxAs2D(const std::string& path) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::string ddsFilePath = fullPath.string() + ".dds";
    gli::texture texture = gli::load(ddsFilePath);
    if (texture.empty()) {
        std::cerr << "Failed to load Skybox DDS: " << ddsFilePath << std::endl;
        return 0;
    }
    return UploadDDS2D(texture, ddsFilePath);
}

void Material::updateBuffers() {
    diffusePath[0] = '\0';
    normalPath[0] = '\0';
    glossPath[0] = '\0';
    lumaPath[0] = '\0';
    bumpPath[0] = '\0';
    detailPath[0] = '\0';
    CopyString(detailScale, sizeof(detailScale), "1 1");
    smoothness = 0.0f;
    reflectScale = 0.0f;
    refractScale = 0.0f;
    aberrationScale = 0.0f;
    reliefScale = 0.0f;
    swayHeight = 0;
    matTypeIndex = 0;

    for (const auto& p : params) {
        if (p.first == "diffuseMap") CopyString(diffusePath, sizeof(diffusePath), p.second);
        if (p.first == "normalMap") CopyString(normalPath, sizeof(normalPath), p.second);
        if (p.first == "glossMap") CopyString(glossPath, sizeof(glossPath), p.second);
        if (p.first == "LumaMap") CopyString(lumaPath, sizeof(lumaPath), p.second);
        if (p.first == "bumpMap" || p.first == "bump") CopyString(bumpPath, sizeof(bumpPath), p.second);
        if (p.first == "detailmap") CopyString(detailPath, sizeof(detailPath), p.second);
        if (p.first == "detailScale") CopyString(detailScale, sizeof(detailScale), p.second);
        if (p.first == "smoothness") try { smoothness = std::stof(p.second); } catch(...) {}
        if (p.first == "reflectScale") try { reflectScale = std::stof(p.second); } catch(...) {}
        if (p.first == "refractScale") try { refractScale = std::stof(p.second); } catch(...) {}
        if (p.first == "aberrationScale") try { aberrationScale = std::stof(p.second); } catch(...) {}
        if (p.first == "reliefScale") try { reliefScale = std::stof(p.second); } catch(...) {}
        if (p.first == "swayHeight") try { swayHeight = std::stoi(p.second); } catch(...) {}
        if (p.first == "material") {
            for (std::size_t i = 0; i < physicalMaterialTypes.size(); ++i) {
                if (p.second == physicalMaterialTypes[i]) {
                    matTypeIndex = static_cast<int>(i);
                    break;
                }
            }
        }
    }
}

void Material::syncParams() {
    auto setParam = [&](const std::string& key, const std::string& val) {
        for (auto& p : params) {
            if (p.first == key) {
                p.second = val;
                return;
            }
        }
        params.push_back({key, val});
    };

    auto setOptionalParam = [&](const std::string& key, const char* value) {
        if (value[0] != '\0') {
            setParam(key, value);
        } else {
            params.erase(std::remove_if(params.begin(), params.end(), [&](const auto& p) {
                return p.first == key;
            }), params.end());
        }
    };

    setOptionalParam("diffuseMap", diffusePath);
    setOptionalParam("normalMap", normalPath);
    setOptionalParam("glossMap", glossPath);
    setOptionalParam("LumaMap", lumaPath);
    setOptionalParam("bumpMap", bumpPath);
    setOptionalParam("detailmap", detailPath);
    setParam("detailScale", detailScale);
    setParam("smoothness", std::to_string(smoothness));
    setParam("reflectScale", std::to_string(reflectScale));
    setParam("refractScale", std::to_string(refractScale));
    setParam("aberrationScale", std::to_string(aberrationScale));
    setParam("reliefScale", std::to_string(reliefScale));
    setParam("swayHeight", std::to_string(swayHeight));
    
    if (matTypeIndex >= 0 && matTypeIndex < (int)physicalMaterialTypes.size()) {
        setParam("material", physicalMaterialTypes[matTypeIndex]);
    }
}

void Material::releaseTextures() {
    for (const auto& [key, id] : textures) {
        if (id != 0) glDeleteTextures(1, &id);
    }
    textures.clear();
}

void Material::loadTextures() {
    releaseTextures();
    for(auto& p : params) {
        if(p.first == "diffuseMap") textures["diffuse"] = LoadDDSTexture(p.second);
        if(p.first == "normalMap") textures["normal"] = LoadDDSTexture(p.second);
        if(p.first == "glossMap") textures["gloss"] = LoadDDSTexture(p.second);
        if(p.first == "LumaMap") textures["luma"] = LoadDDSTexture(p.second);
        if(p.first == "bumpMap" || p.first == "bump") textures["bump"] = LoadDDSTexture(p.second);
    }
}

void PhysicalMaterialEntry::updateBuffers() {
    impactDecal[0] = '\0';
    impactPartsBuf[0] = '\0';
    impactSoundBuf[0] = '\0';
    stepSoundBuf[0] = '\0';

    auto joinVec = [](const std::vector<std::string>& vec) {
        std::string res;
        for (size_t i = 0; i < vec.size(); ++i) {
            res += vec[i];
            if (i + 1 < vec.size()) res += " ";
        }
        return res;
    };

    for (auto& [key, vals] : multiParams) {
        std::string joined = joinVec(vals);
        if (key == "impact_decal") CopyString(impactDecal, sizeof(impactDecal), vals.empty() ? "" : vals[0]);
        if (key == "impact_parts") CopyString(impactPartsBuf, sizeof(impactPartsBuf), joined);
        if (key == "impact_sound") CopyString(impactSoundBuf, sizeof(impactSoundBuf), joined);
        if (key == "step_sound") CopyString(stepSoundBuf, sizeof(stepSoundBuf), joined);
    }
}

void PhysicalMaterialEntry::syncParams() {
    auto splitToVec = [](const std::string& str) {
        std::vector<std::string> res;
        std::stringstream ss(str);
        std::string item;
        while (ss >> item) {
            res.push_back(item);
        }
        return res;
    };

    if (strlen(impactDecal) > 0) multiParams["impact_decal"] = {impactDecal};
    else multiParams.erase("impact_decal");

    std::vector<std::string> parts = splitToVec(impactPartsBuf);
    if (!parts.empty()) multiParams["impact_parts"] = parts;
    else multiParams.erase("impact_parts");

    std::vector<std::string> impSounds = splitToVec(impactSoundBuf);
    if (!impSounds.empty()) multiParams["impact_sound"] = impSounds;
    else multiParams.erase("impact_sound");

    std::vector<std::string> stepSounds = splitToVec(stepSoundBuf);
    if (!stepSounds.empty()) multiParams["step_sound"] = stepSounds;
    else multiParams.erase("step_sound");
}

void LoadAllPhysicalMaterials(const std::string& path, std::vector<PhysicalMaterialEntry>& physMats) {
    physMats.clear();
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ifstream file(fullPath);
    if (!file.is_open()) return;

    std::string line, lastLine;
    PhysicalMaterialEntry* currentMat = nullptr;

    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();

        if (line.find('{') != std::string::npos) {
            physMats.emplace_back();
            currentMat = &physMats.back();
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
            if (q2 == std::string::npos) continue;
            std::string key = line.substr(q1 + 1, q2 - q1 - 1);

            std::vector<std::string> values;
            size_t searchPos = q2 + 1;
            while (true) {
                size_t v1 = line.find('\"', searchPos);
                if (v1 == std::string::npos) break;
                size_t v2 = line.find('\"', v1 + 1);
                if (v2 == std::string::npos) break;
                values.push_back(line.substr(v1 + 1, v2 - v1 - 1));
                searchPos = v2 + 1;
            }
            if (!values.empty()) {
                currentMat->multiParams[key] = values;
            }
        } else {
            if (!line.empty()) lastLine = line;
        }
    }
}

void SaveAllPhysicalMaterials(const std::string& path, const std::vector<PhysicalMaterialEntry>& physMats) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ofstream file(fullPath);
    if (!file.is_open()) return;

    for (const auto& mat : physMats) {
        file << "\"" << mat.name << "\"\n{\n";
        for (const auto& [key, vals] : mat.multiParams) {
            file << "\t\"" << key << "\"";
            for (const auto& v : vals) {
                file << "\t\"" << v << "\"";
            }
            file << "\n";
        }
        file << "}\n";
    }
}

void LoadAllMaterials(const std::string& path, std::vector<Material>& materials) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ifstream file(fullPath);
    if (!file.is_open()) return;

    std::string line, lastLine;
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

void SaveAllMaterials(const std::string& path, const std::vector<Material>& materials) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ofstream file(fullPath);
    if (!file.is_open()) return;
    for (const auto& mat : materials) {
        file << "\"" << mat.name << "\"\n{\n";
        for (const auto& p : mat.params) {
            file << "\t\"" << p.first << "\"\t\"" << p.second << "\"\n";
        }
        file << "}\n";
    }
}
