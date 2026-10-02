#include "MaterialSystem.h"
#include "ImageLoader.h"
#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <gli/gli.hpp>
#ifdef _WIN32
#include <windows.h>
#endif

fs::path gameRootPath = "";
std::vector<std::string> physicalMaterialTypes = {"default"};

std::string ResolveWadTextureReference(const std::string& reference);

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
std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

fs::path g_materialTextureIndexRoot;
std::unordered_map<std::string, std::vector<fs::path>> g_materialTextureIndex;

void RebuildMaterialTextureIndex() {
    g_materialTextureIndexRoot.clear();
    g_materialTextureIndex.clear();
    if (gameRootPath.empty()) return;

    std::error_code rootError;
    const fs::path normalizedRoot = fs::weakly_canonical(gameRootPath, rootError);
    if (rootError) return;
    g_materialTextureIndexRoot = normalizedRoot;

    std::error_code ec;
    fs::recursive_directory_iterator it(normalizedRoot, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (it != end) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }
        std::error_code typeError;
        if (it->is_regular_file(typeError) && !typeError &&
            ToLower(it->path().extension().string()) == ".dds") {
            g_materialTextureIndex[ToLower(it->path().filename().string())].push_back(it->path());
        }
        it.increment(ec);
    }
}

std::string FindMaterialTextureReferenceByName(const std::string& textureName) {
    if (gameRootPath.empty() || textureName.empty()) return {};
    const std::string fileName = fs::path(textureName).stem().string() + ".dds";

    std::error_code rootError;
    const fs::path normalizedRoot = fs::weakly_canonical(gameRootPath, rootError);
    if (rootError) return {};
    if (g_materialTextureIndexRoot != normalizedRoot) {
        RebuildMaterialTextureIndex();
    }

    const auto matches = g_materialTextureIndex.find(ToLower(fileName));
    if (matches != g_materialTextureIndex.end() && !matches->second.empty()) {
        const fs::path texturesRoot = normalizedRoot / "textures";
        const fs::path& candidate = *std::min_element(matches->second.begin(), matches->second.end(),
            [&](const fs::path& left, const fs::path& right) {
                const bool leftInTextures = left.parent_path() == texturesRoot;
                const bool rightInTextures = right.parent_path() == texturesRoot;
                if (leftInTextures != rightInTextures) return leftInTextures;
                return ToLower(left.generic_string()) < ToLower(right.generic_string());
            });
        std::error_code ec;
        const fs::path relative = fs::relative(candidate, gameRootPath, ec);
        return ec ? candidate.generic_string() : relative.generic_string();
    }

    return ResolveWadTextureReference(textureName);
}

struct CachedDDS {
    GLuint texture = 0;
    std::filesystem::file_time_type writeTime{};
    std::uintmax_t fileSize = 0;
    std::size_t refs = 0;
    TextureFormatInfo formatInfo{};
};

std::unordered_map<std::string, CachedDDS> g_ddsCache;
std::unordered_map<GLuint, std::size_t> g_retiredDDSTextures;
std::unordered_map<GLuint, TextureFormatInfo> g_textureFormatInfo;
std::unordered_map<GLuint, GLint> g_textureMaxMipLevels;
bool g_textureFilteringEnabled = true;
std::vector<WadArchive> g_wadArchives;
std::unordered_map<std::string, std::size_t> g_wadArchiveLookup;
std::unordered_map<std::string, std::pair<std::size_t, std::size_t>> g_wadTextureLookup;
std::unordered_map<std::string, std::pair<std::size_t, std::size_t>> g_wadTextureNameLookup;
struct CachedWadFile {
    std::shared_ptr<std::vector<std::uint8_t>> data;
    std::uint64_t sourceSize = 0;
    std::uint64_t hashFirst = 0;
    std::uint64_t hashSecond = 0;
    std::uint64_t lastUse = 0;
};
constexpr std::size_t kWadFileCacheLimit = 128u * 1024u * 1024u;
std::unordered_map<std::string, CachedWadFile> g_wadFileCache;
std::size_t g_wadFileCacheBytes = 0;
std::uint64_t g_wadCacheClock = 0;

std::string MakeWadTextureLookupKey(const std::string& wadPath, const std::string& textureName) {
    return ToLower(wadPath) + '\0' + ToLower(textureName);
}

bool FindWadTexture(const std::string& wadPath, const std::string& textureName,
                    const WadArchive*& wad, const WadTexture*& texture) {
    const auto lookup = g_wadTextureLookup.find(MakeWadTextureLookupKey(wadPath, textureName));
    if (lookup == g_wadTextureLookup.end() || lookup->second.first >= g_wadArchives.size()) return false;
    const WadArchive& candidateWad = g_wadArchives[lookup->second.first];
    if (lookup->second.second >= candidateWad.textures.size()) return false;
    wad = &candidateWad;
    texture = &candidateWad.textures[lookup->second.second];
    return true;
}

std::string NormalizeMaterialTextureReference(std::string value) {
    const std::string lower = ToLower(value);
    if (lower.rfind("wad://", 0) == 0 || lower.rfind("wad:/", 0) == 0) {
        const std::size_t separator = value.rfind('#');
        if (separator != std::string::npos) value = value.substr(separator + 1);
        const std::string extension = ToLower(fs::path(value).extension().string());
        if (extension == ".png" || extension == ".tga" || extension == ".dds") value = fs::path(value).stem().string();
        return value;
    }

    const std::string extension = ToLower(fs::path(value).extension().string());
    if (extension == ".png" || extension == ".tga") {
        const std::string wadReference = ResolveWadTextureReference(value);
        if (!wadReference.empty()) {
            const std::size_t separator = wadReference.rfind('#');
            if (separator != std::string::npos) value = wadReference.substr(separator + 1);
            const std::string resolvedExtension = ToLower(fs::path(value).extension().string());
            if (resolvedExtension == ".png" || resolvedExtension == ".tga" || resolvedExtension == ".dds") value = fs::path(value).stem().string();
        }
    }
    return value;
}

constexpr GLenum kGLCompressedRed_RGTC1 = 0x8DBB;
constexpr GLenum kGLCompressedRG_RGTC2 = 0x8DBD;
constexpr GLenum kGLCompressedSignedRG_RGTC2 = 0x8DBE;
constexpr GLenum kGLCompressedRGBA_BPTC_UNORM = 0x8E8C;
constexpr GLenum kGLCompressedSRGBAlpha_BPTC_UNORM = 0x8E8D;
constexpr GLenum kGLCompressedRGB_BPTC_SIGNED_FLOAT = 0x8E8E;
constexpr GLenum kGLCompressedRGB_BPTC_UNSIGNED_FLOAT = 0x8E8F;
constexpr GLenum kGLCompressedSRGB_S3TC_DXT1_EXT = 0x8C4C;
constexpr GLenum kGLCompressedSRGBAlpha_S3TC_DXT1_EXT = 0x8C4D;
constexpr GLenum kGLCompressedSRGBAlpha_S3TC_DXT3_EXT = 0x8C4E;
constexpr GLenum kGLCompressedSRGBAlpha_S3TC_DXT5_EXT = 0x8C4F;

TextureFormatInfo DetectTextureFormatInfo(GLenum internalFormat, GLenum externalFormat) {
    TextureFormatInfo info;
    info.valid = internalFormat != GL_NONE;
    info.compressed = externalFormat == GL_NONE;
    info.channels = 4;

    switch (internalFormat) {
        case kGLCompressedRed_RGTC1:
            info.bc4 = true;
            info.channels = 1;
            break;
        case kGLCompressedRG_RGTC2:
        case kGLCompressedSignedRG_RGTC2:
            info.bc5 = true;
            info.channels = 2;
            break;
        case kGLCompressedRGBA_BPTC_UNORM:
            info.channels = 4;
            break;
        case kGLCompressedSRGBAlpha_BPTC_UNORM:
            info.channels = 4;
            info.srgb = true;
            break;
        case kGLCompressedRGB_BPTC_SIGNED_FLOAT:
        case kGLCompressedRGB_BPTC_UNSIGNED_FLOAT:
            info.channels = 3;
            break;
        case kGLCompressedSRGB_S3TC_DXT1_EXT:
        case kGLCompressedSRGBAlpha_S3TC_DXT1_EXT:
        case kGLCompressedSRGBAlpha_S3TC_DXT3_EXT:
        case kGLCompressedSRGBAlpha_S3TC_DXT5_EXT:
            info.srgb = true;
            break;
        case GL_R8:
        case GL_R16F:
        case GL_R32F:
            info.channels = 1;
            break;
        case GL_RG8:
        case GL_RG16F:
        case GL_RG32F:
            info.channels = 2;
            break;
        case GL_RGB8:
        case GL_RGB16F:
        case GL_RGB32F:
            info.channels = 3;
            break;
        case GL_SRGB8:
            info.channels = 3;
            info.srgb = true;
            break;
        case GL_SRGB8_ALPHA8:
            info.channels = 4;
            info.srgb = true;
            break;
        default:
            break;
    }

    return info;
}

void CopyString(char* destination, std::size_t capacity, const std::string& value) {
    if (capacity == 0) return;
    std::snprintf(destination, capacity, "%s", value.c_str());
}

struct UploadedDDS {
    GLuint texture = 0;
    TextureFormatInfo formatInfo{};
};

void ApplyTextureSampling(GLuint texture, GLint maxMipLevel) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, g_textureFilteringEnabled ? maxMipLevel : 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    g_textureFilteringEnabled && maxMipLevel > 0 ? GL_LINEAR_MIPMAP_LINEAR :
                    (g_textureFilteringEnabled ? GL_LINEAR : GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_textureFilteringEnabled ? GL_LINEAR : GL_NEAREST);
}

void RegisterTextureSampling(GLuint texture, GLint maxMipLevel) {
    if (texture == 0) return;
    g_textureMaxMipLevels[texture] = std::max(0, maxMipLevel);
    ApplyTextureSampling(texture, g_textureMaxMipLevels[texture]);
}

GLint GetFullMipLevel(int width, int height) {
    GLint level = 0;
    for (int size = std::max(width, height); size > 1; size /= 2) ++level;
    return level;
}

UploadedDDS UploadDDS2D(const gli::texture& texture, const std::string& sourcePath) {
    if (texture.empty() || texture.levels() == 0) return {};

    GLenum previousError = glGetError();
    while (previousError != GL_NO_ERROR) {
        std::cerr << "MatEdit: clearing pre-existing OpenGL error before DDS upload (0x"
                  << std::hex << previousError << std::dec << ")" << std::endl;
        previousError = glGetError();
    }

    gli::gl GL(gli::gl::PROFILE_GL33);
    const gli::gl::format Format = GL.translate(texture.format(), texture.swizzles());
    if (Format.Internal == GL_NONE) {
        std::cerr << "Unsupported DDS format: " << sourcePath << std::endl;
        return {};
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(texture.levels() - 1));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture.levels() > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    RegisterTextureSampling(textureID, static_cast<GLint>(texture.levels() - 1));

    const bool compressed = Format.External == GL_NONE || Format.Type == GL_NONE;
    const TextureFormatInfo formatInfo = DetectTextureFormatInfo(Format.Internal, Format.External);
    if (formatInfo.bc4) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
    }
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

    const GLenum uploadError = glGetError();
    if (uploadError != GL_NO_ERROR) {
        g_textureMaxMipLevels.erase(textureID);
        glDeleteTextures(1, &textureID);
        std::cerr << "Failed to upload DDS texture: " << sourcePath
                  << " (OpenGL error 0x" << std::hex << uploadError << std::dec << ")" << std::endl;
        return {};
    }

    UploadedDDS uploaded;
    uploaded.texture = textureID;
    uploaded.formatInfo = formatInfo;
    return uploaded;
}
}

void SetTextureFilteringEnabled(bool enabled) {
    g_textureFilteringEnabled = enabled;
    GLint previousTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    for (const auto& [texture, maxMipLevel] : g_textureMaxMipLevels) {
        if (glIsTexture(texture) == GL_FALSE) continue;
        ApplyTextureSampling(texture, maxMipLevel);
    }
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
}

GLuint LoadDDSTexture(const std::string& path) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    if (fullPath.extension().empty()) fullPath += ".dds";
    if (ToLower(fullPath.extension().string()) != ".dds") return 0;
    std::error_code ec;
    const fs::path canonicalPath = fs::weakly_canonical(fullPath, ec);
    if (!ec) fullPath = canonicalPath;
    ec.clear();
    const std::string key = fullPath.generic_string();
    const bool fileExists = fs::exists(fullPath, ec) && !ec;
    const auto writeTime = fileExists ? fs::last_write_time(fullPath, ec) : fs::file_time_type{};
    const bool hasWriteTime = fileExists && !ec;
    ec.clear();
    const std::uintmax_t fileSize = fileExists ? fs::file_size(fullPath, ec) : 0;
    const bool hasFileFingerprint = hasWriteTime && !ec;
    auto cacheIt = g_ddsCache.find(key);
    if (cacheIt != g_ddsCache.end()) {
        if (hasFileFingerprint && cacheIt->second.texture != 0 &&
            cacheIt->second.writeTime == writeTime && cacheIt->second.fileSize == fileSize) {
            ++cacheIt->second.refs;
            return cacheIt->second.texture;
        }
        if (cacheIt->second.texture != 0) g_retiredDDSTextures[cacheIt->second.texture] = cacheIt->second.refs;
        g_ddsCache.erase(cacheIt);
    }

    const std::string ddsFilePath = fullPath.string();
    gli::texture texture = gli::load(ddsFilePath);
    if (texture.empty()) {
        std::cerr << "Failed to load DDS: " << ddsFilePath << std::endl;
        return 0;
    }
    const UploadedDDS uploaded = UploadDDS2D(texture, ddsFilePath);
    if (uploaded.texture == 0) return 0;

    g_ddsCache.emplace(key, CachedDDS{uploaded.texture, writeTime, fileSize, 1, uploaded.formatInfo});
    g_textureFormatInfo[uploaded.texture] = uploaded.formatInfo;
    return uploaded.texture;
}

void ReleaseDDSTexture(GLuint texture) {
    if (texture == 0) return;
    for (auto it = g_ddsCache.begin(); it != g_ddsCache.end(); ++it) {
        if (it->second.texture != texture) continue;
        if (it->second.refs > 1) {
            --it->second.refs;
        } else {
            g_textureFormatInfo.erase(it->second.texture);
            g_textureMaxMipLevels.erase(it->second.texture);
            glDeleteTextures(1, &it->second.texture);
            g_ddsCache.erase(it);
        }
        return;
    }
    auto retiredIt = g_retiredDDSTextures.find(texture);
    if (retiredIt != g_retiredDDSTextures.end()) {
        if (retiredIt->second > 1) {
            --retiredIt->second;
        } else {
            g_textureFormatInfo.erase(texture);
            g_textureMaxMipLevels.erase(texture);
            glDeleteTextures(1, &texture);
            g_retiredDDSTextures.erase(retiredIt);
        }
        return;
    }
    g_textureFormatInfo.erase(texture);
    g_textureMaxMipLevels.erase(texture);
    glDeleteTextures(1, &texture);
}

TextureFormatInfo GetTextureFormatInfo(GLuint texture) {
    if (texture == 0) return {};
    auto it = g_textureFormatInfo.find(texture);
    if (it != g_textureFormatInfo.end()) return it->second;

    GLint internalFormat = GL_NONE;
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (internalFormat == GL_NONE) return {};
    return DetectTextureFormatInfo(static_cast<GLenum>(internalFormat), GL_NONE);
}

GLuint LoadDDS_Cubemap(const std::string& path) {
    fs::path base = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    if (base.extension() == ".dds" || base.extension() == ".DDS") base.replace_extension();

    const fs::path singlePath = base.string() + ".dds";
    gli::texture single = gli::load(singlePath.string());
    if (!single.empty() && single.faces() == 6) {
        gli::gl GL(gli::gl::PROFILE_GL33);
        const gli::gl::format Format = GL.translate(single.format(), single.swizzles());
        if (Format.Internal == GL_NONE) return 0;

        GLuint textureID = 0;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        const bool compressed = Format.External == GL_NONE || Format.Type == GL_NONE;
        for (std::size_t face = 0; face < 6; ++face) {
            for (std::size_t level = 0; level < single.levels(); ++level) {
                const GLsizei width = static_cast<GLsizei>(single.extent(level).x);
                const GLsizei height = static_cast<GLsizei>(single.extent(level).y);
                const GLsizei size = static_cast<GLsizei>(single.size(level));
                const GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face);
                if (compressed) {
                    glCompressedTexImage2D(target, static_cast<GLint>(level), Format.Internal, width, height, 0, size, single.data(face, 0, level));
                } else {
                    glTexImage2D(target, static_cast<GLint>(level), Format.Internal, width, height, 0, Format.External, Format.Type, single.data(face, 0, level));
                }
            }
        }
        if (glGetError() != GL_NO_ERROR) {
            glDeleteTextures(1, &textureID);
            return 0;
        }
        return textureID;
    }

    const std::array<std::string, 6> suffixes = {"rt", "lf", "up", "dn", "bk", "ft"};
    std::array<gli::texture, 6> faces;
    for (std::size_t i = 0; i < suffixes.size(); ++i) {
        fs::path facePath = base.parent_path() / (base.filename().string() + suffixes[i] + ".dds");
        faces[i] = gli::load(facePath.string());
        if (faces[i].empty() || faces[i].faces() != 1 || faces[i].levels() == 0) return 0;
    }

    gli::gl GL(gli::gl::PROFILE_GL33);
    const gli::gl::format Format = GL.translate(faces[0].format(), faces[0].swizzles());
    if (Format.Internal == GL_NONE) return 0;

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    const bool compressed = Format.External == GL_NONE || Format.Type == GL_NONE;
    for (std::size_t face = 0; face < 6; ++face) {
        if (faces[face].format() != faces[0].format() || faces[face].levels() != faces[0].levels()) {
            glDeleteTextures(1, &textureID);
            return 0;
        }
        for (std::size_t level = 0; level < faces[face].levels(); ++level) {
            const GLsizei width = static_cast<GLsizei>(faces[face].extent(level).x);
            const GLsizei height = static_cast<GLsizei>(faces[face].extent(level).y);
            const GLsizei size = static_cast<GLsizei>(faces[face].size(level));
            const GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face);
            if (compressed) {
                glCompressedTexImage2D(target, static_cast<GLint>(level), Format.Internal, width, height, 0, size, faces[face].data(0, 0, level));
            } else {
                glTexImage2D(target, static_cast<GLint>(level), Format.Internal, width, height, 0, Format.External, Format.Type, faces[face].data(0, 0, level));
            }
        }
    }

    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &textureID);
        return 0;
    }
    return textureID;
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
    textureScaleX = 1.0f;
    textureScaleY = 1.0f;
    swayHeight = 0;
    matTypeIndex = 0;

    for (const auto& p : params) {
        if (p.first == "diffuseMap") CopyString(diffusePath, sizeof(diffusePath), NormalizeMaterialTextureReference(p.second));
        if (p.first == "normalMap") CopyString(normalPath, sizeof(normalPath), NormalizeMaterialTextureReference(p.second));
        if (p.first == "glossMap") CopyString(glossPath, sizeof(glossPath), NormalizeMaterialTextureReference(p.second));
        if (p.first == "LumaMap") CopyString(lumaPath, sizeof(lumaPath), NormalizeMaterialTextureReference(p.second));
        if (p.first == "bumpMap" || p.first == "bump") CopyString(bumpPath, sizeof(bumpPath), NormalizeMaterialTextureReference(p.second));
        if (p.first == "detailmap") CopyString(detailPath, sizeof(detailPath), NormalizeMaterialTextureReference(p.second));
        if (p.first == "detailScale") CopyString(detailScale, sizeof(detailScale), p.second);
        if (p.first == "smoothness") try { smoothness = std::stof(p.second); } catch(...) {}
        if (p.first == "reflectScale") try { reflectScale = std::stof(p.second); } catch(...) {}
        if (p.first == "refractScale") try { refractScale = std::stof(p.second); } catch(...) {}
        if (p.first == "aberrationScale") try { aberrationScale = std::stof(p.second); } catch(...) {}
        if (p.first == "reliefScale") try { reliefScale = std::stof(p.second); } catch(...) {}
        if (p.first == "textureScale") {
            std::istringstream scaleStream(p.second);
            if (!(scaleStream >> textureScaleX)) textureScaleX = 1.0f;
            if (!(scaleStream >> textureScaleY)) textureScaleY = textureScaleX;
        }
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
    const bool hasSmoothness = std::any_of(params.begin(), params.end(), [](const auto& param) {
        return ToLower(param.first) == "smoothness";
    });
    if (!hasSmoothness) {
        const auto legacyGloss = std::find_if(params.begin(), params.end(), [](const auto& param) {
            return ToLower(param.first) == "glossexp";
        });
        if (legacyGloss != params.end()) {
            try {
                const float exponent = std::clamp(std::stof(legacyGloss->second), 0.0f, 256.0f);
                smoothness = std::sqrt(exponent / 256.0f);
            } catch (...) {
            }
        }
    }
    params.erase(std::remove_if(params.begin(), params.end(), [](const auto& param) {
        return ToLower(param.first) == "glossexp";
    }), params.end());
    if (!hasSmoothness) params.push_back({"smoothness", std::to_string(smoothness)});
    if (diffusePath[0] == '\0') {
        const std::string diffuseReference = FindMaterialTextureReferenceByName(name);
        CopyString(diffusePath, sizeof(diffusePath), diffuseReference);
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

    const std::string normalizedDiffuse = NormalizeMaterialTextureReference(diffusePath);
    const std::string normalizedNormal = NormalizeMaterialTextureReference(normalPath);
    const std::string normalizedGloss = NormalizeMaterialTextureReference(glossPath);
    const std::string normalizedLuma = NormalizeMaterialTextureReference(lumaPath);
    const std::string normalizedBump = NormalizeMaterialTextureReference(bumpPath);
    const std::string normalizedDetail = NormalizeMaterialTextureReference(detailPath);
    CopyString(diffusePath, sizeof(diffusePath), normalizedDiffuse);
    CopyString(normalPath, sizeof(normalPath), normalizedNormal);
    CopyString(glossPath, sizeof(glossPath), normalizedGloss);
    CopyString(lumaPath, sizeof(lumaPath), normalizedLuma);
    CopyString(bumpPath, sizeof(bumpPath), normalizedBump);
    CopyString(detailPath, sizeof(detailPath), normalizedDetail);
    const std::string materialStem = ToLower(fs::path(name).stem().string());
    const std::string diffuseStem = ToLower(fs::path(diffusePath).stem().string());
    if (diffusePath[0] != '\0' && diffuseStem != materialStem) {
        setOptionalParam("diffuseMap", diffusePath);
    } else {
        setOptionalParam("diffuseMap", "");
    }
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
    setParam("textureScale", std::to_string(textureScaleX) + " " + std::to_string(textureScaleY));
    setParam("swayHeight", std::to_string(swayHeight));
    
    if (matTypeIndex >= 0 && matTypeIndex < (int)physicalMaterialTypes.size()) {
        setParam("material", physicalMaterialTypes[matTypeIndex]);
    }
        params.erase(std::remove_if(params.begin(), params.end(), [](const auto& param) {
            return ToLower(param.first) == "glossexp";
        }), params.end());
}

void RefreshMaterialTextureIndex() {
    RebuildMaterialTextureIndex();
}

bool AutoAssignMaterialTexturesByName(Material& material) {
    const std::string stem = fs::path(material.name).stem().string();
    if (stem.empty() || gameRootPath.empty()) return false;

    bool changed = false;
    const auto assignIfMissing = [&](char* target, std::size_t capacity, const std::string& textureName) {
        if (target[0] != '\0') return;
        const std::string reference = FindMaterialTextureReferenceByName(textureName);
        if (reference.empty()) return;
        CopyString(target, capacity, reference);
        changed = true;
    };

    assignIfMissing(material.diffusePath, sizeof(material.diffusePath), stem);
    assignIfMissing(material.normalPath, sizeof(material.normalPath), stem + "_norm");
    assignIfMissing(material.glossPath, sizeof(material.glossPath), stem + "_gloss");
    if (material.glossPath[0] == '\0') {
        assignIfMissing(material.glossPath, sizeof(material.glossPath), stem + "_pbr");
    }
    if (material.glossPath[0] == '\0') {
        assignIfMissing(material.glossPath, sizeof(material.glossPath), stem + "_spec");
    }
    assignIfMissing(material.bumpPath, sizeof(material.bumpPath), stem + "_hmap");
    assignIfMissing(material.lumaPath, sizeof(material.lumaPath), stem + "_luma");
    assignIfMissing(material.detailPath, sizeof(material.detailPath), stem + "_detail");
    if (changed) material.syncParams();
    return changed;
}

void Material::releaseTextures() {
    for (const auto& [key, id] : textures) {
        if (id != 0) ReleaseDDSTexture(id);
    }
    textures.clear();
}

void Material::loadTextures() {
    releaseTextures();
    diffuseWadTransparency = false;
    bool diffuseLoaded = false;
    for (const auto& p : params) {
        if (p.first == "diffuseMap") {
            textures["diffuse"] = LoadTextureReference(p.second, &diffuseWadTransparency);
            diffuseLoaded = true;
        }
        if (p.first == "normalMap") textures["normal"] = LoadTextureReference(p.second);
        if (p.first == "glossMap") textures["gloss"] = LoadTextureReference(p.second);
        if (p.first == "LumaMap") textures["luma"] = LoadTextureReference(p.second);
        if (p.first == "bumpMap" || p.first == "bump") textures["bump"] = LoadTextureReference(p.second);
        if (p.first == "detailmap") textures["detail"] = LoadTextureReference(p.second);
    }
    if (!diffuseLoaded && diffusePath[0] != '\0') {
        textures["diffuse"] = LoadTextureReference(diffusePath, &diffuseWadTransparency);
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

bool LoadAllMaterials(const std::string& path, std::vector<Material>& materials, bool autoAssignTextures) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ifstream file(fullPath);
    if (!file.is_open()) return false;

    RefreshMaterialTextureIndex();
    std::vector<Material> loadedMaterials;
    std::string line, lastLine;
    Material* currentMat = nullptr;

    while (std::getline(file, line)) {
        if (line.find('{') != std::string::npos) {
            loadedMaterials.emplace_back();
            currentMat = &loadedMaterials.back();
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

    if (file.bad()) return false;
    std::vector<Material> uniqueMaterials;
    std::unordered_map<std::string, std::size_t> materialIndices;
    for (Material& loaded : loadedMaterials) {
        const std::string key = ToLower(loaded.name);
        const auto existing = materialIndices.find(key);
        if (existing == materialIndices.end()) {
            materialIndices.emplace(key, uniqueMaterials.size());
            uniqueMaterials.push_back(std::move(loaded));
            continue;
        }

        Material& original = uniqueMaterials[existing->second];
        for (const auto& param : loaded.params) {
            const bool alreadyPresent = std::any_of(original.params.begin(), original.params.end(), [&](const auto& existingParam) {
                return existingParam.first == param.first;
            });
            if (!alreadyPresent) original.params.push_back(param);
        }
        original.updateBuffers();
    }
    for (auto& material : materials) material.releaseTextures();
    materials = std::move(uniqueMaterials);
    if (autoAssignTextures) {
        bool changed = false;
        for (Material& material : materials) {
            changed |= AutoAssignMaterialTexturesByName(material);
        }
        if (changed) SaveAllMaterials(path, materials);
    }
    return true;
}

namespace {
std::string CanonicalMaterialParamName(const std::string& name) {
    const std::string lower = ToLower(name);
    if (lower == "material") return "material";
    if (lower == "smoothness") return "smoothness";
    if (lower == "detailscale") return "detailScale";
    if (lower == "detailmap") return "detailmap";
    if (lower == "diffusemap") return "diffuseMap";
    if (lower == "normalmap") return "normalMap";
    if (lower == "glossmap") return "glossMap";
    if (lower == "aberrationscale") return "aberrationScale";
    if (lower == "reflectscale") return "reflectScale";
    if (lower == "refractscale") return "refractScale";
    if (lower == "swayheight") return "swayHeight";
    if (lower == "reliefscale") return "reliefScale";
    return {};
}
}

bool SaveAllMaterials(const std::string& path, const std::vector<Material>& materials) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ofstream file(fullPath);
    if (!file.is_open()) {
        std::cerr << "MatEdit: failed to open material file for writing: " << fullPath << std::endl;
        return false;
    }
    for (const auto& mat : materials) {
        file << "\"" << mat.name << "\"\n{\n";
        std::unordered_set<std::string> writtenParams;
        for (const auto& p : mat.params) {
            const std::string key = CanonicalMaterialParamName(p.first);
            if (key.empty() || !writtenParams.insert(key).second) continue;
            std::string value = p.second;
            if (key == "diffuseMap" || key == "normalMap" || key == "glossMap" || key == "detailmap") {
                value = NormalizeMaterialTextureReference(value);
                if (key == "diffuseMap" &&
                    ToLower(fs::path(value).stem().string()) == ToLower(fs::path(mat.name).stem().string())) {
                    continue;
                }
                if (ToLower(fs::path(value).extension().string()) == ".dds") {
                    value = fs::path(value).replace_extension().string();
                }
            }
            file << "\t\"" << key << "\"\t\"" << value << "\"\n";
        }
        file << "}\n";
    }
    file.flush();
    if (!file) {
        std::cerr << "MatEdit: failed to write material file: " << fullPath << std::endl;
        return false;
    }
    return true;
}

namespace {
#pragma pack(push, 1)
struct WadHeaderRaw { char identification[4]; std::int32_t numLumps; std::int32_t infoTableOffset; };
struct WadLumpRaw { std::int32_t filePos; std::int32_t diskSize; std::int32_t size; std::uint8_t type; std::uint8_t compression; std::uint16_t padding; char name[16]; };
#pragma pack(pop)

struct WadContentHash {
    std::uint64_t first = 14695981039346656037ull;
    std::uint64_t second = 7809847782465536322ull;
};

void UpdateWadHash(WadContentHash& hash, const char* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        const auto byte = static_cast<std::uint8_t>(data[i]);
        hash.first = (hash.first ^ byte) * 1099511628211ull;
        hash.second ^= static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull + (hash.second << 6u) + (hash.second >> 2u);
        hash.second *= 0xbf58476d1ce4e5b9ull;
    }
}

WadContentHash HashWadPath(const std::string& value) {
    WadContentHash hash;
    UpdateWadHash(hash, value.data(), value.size());
    return hash;
}

bool GetWadFingerprint(const fs::path& path, std::uint64_t& size, WadContentHash& hash) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    const std::streamoff end = file.tellg();
    if (end < 0) return false;
    size = static_cast<std::uint64_t>(end);
    file.seekg(0, std::ios::beg);

    std::array<char, 64 * 1024> buffer{};
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = file.gcount();
        if (count > 0) UpdateWadHash(hash, buffer.data(), static_cast<std::size_t>(count));
    }
    return file.eof() && !file.bad();
}

fs::path GetWadCachePath(const std::string& sourceKey) {
    const WadContentHash key = HashWadPath(sourceKey);
    std::array<char, 48> filename{};
    std::snprintf(filename.data(), filename.size(), "%016llx%016llx.wadcache",
                  static_cast<unsigned long long>(key.first),
                  static_cast<unsigned long long>(key.second));
    return GetConfigDirectory() / "wad-cache" / filename.data();
}

template <typename T>
bool ReadWadCacheValue(std::istream& stream, T& value) {
    stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(stream);
}

template <typename T>
bool WriteWadCacheValue(std::ostream& stream, const T& value) {
    stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return static_cast<bool>(stream);
}

bool LoadWadCache(const fs::path& cachePath, const std::string& sourceKey,
                  std::uint64_t sourceSize, const WadContentHash& sourceHash, WadArchive& out) {
    std::ifstream cache(cachePath, std::ios::binary);
    if (!cache) return false;
    std::array<char, 8> magic{};
    cache.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!cache || magic != std::array<char, 8>{'M', 'A', 'T', 'W', 'A', 'D', '0', '1'}) return false;

    std::uint32_t version = 0;
    std::uint32_t pathLength = 0;
    std::uint64_t cachedSize = 0;
    std::uint64_t hashFirst = 0;
    std::uint64_t hashSecond = 0;
    std::uint32_t textureCount = 0;
    if (!ReadWadCacheValue(cache, version) || version != 1 ||
        !ReadWadCacheValue(cache, pathLength) || pathLength > 32768) return false;
    std::string cachedPath(pathLength, '\0');
    cache.read(cachedPath.data(), static_cast<std::streamsize>(cachedPath.size()));
    if (!cache || cachedPath != sourceKey ||
        !ReadWadCacheValue(cache, cachedSize) ||
        !ReadWadCacheValue(cache, hashFirst) ||
        !ReadWadCacheValue(cache, hashSecond) ||
        !ReadWadCacheValue(cache, textureCount) ||
        cachedSize != sourceSize || hashFirst != sourceHash.first || hashSecond != sourceHash.second ||
        textureCount > 1000000) return false;

    WadArchive cached;
    cached.relativePath = out.relativePath;
    cached.displayName = out.displayName;
    cached.textures.reserve(textureCount);
    for (std::uint32_t i = 0; i < textureCount; ++i) {
        std::uint8_t nameLength = 0;
        WadTexture texture;
        std::int32_t width = 0;
        std::int32_t height = 0;
        if (!ReadWadCacheValue(cache, nameLength) || nameLength == 0 || nameLength > 16) return false;
        texture.name.resize(nameLength);
        cache.read(texture.name.data(), static_cast<std::streamsize>(texture.name.size()));
        if (!cache || !ReadWadCacheValue(cache, width) || !ReadWadCacheValue(cache, height) ||
            !ReadWadCacheValue(cache, texture.pixelOffset) || !ReadWadCacheValue(cache, texture.paletteOffset)) return false;
        if (width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
            static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > 64ull * 1024ull * 1024ull ||
            texture.pixelOffset == 0 || texture.paletteOffset == 0 ||
            static_cast<std::uint64_t>(texture.pixelOffset) + static_cast<std::uint64_t>(width) * height > sourceSize ||
            static_cast<std::uint64_t>(texture.paletteOffset) + 770ull > sourceSize) return false;
        texture.width = width;
        texture.height = height;
        cached.textures.push_back(std::move(texture));
    }
    if (cache.peek() != std::char_traits<char>::eof()) return false;
    out = std::move(cached);
    return true;
}

bool SaveWadCache(const fs::path& cachePath, const std::string& sourceKey,
                  std::uint64_t sourceSize, const WadContentHash& sourceHash, const WadArchive& archive) {
    std::error_code ec;
    fs::create_directories(cachePath.parent_path(), ec);
    if (ec) {
        std::cerr << "MatEdit: cannot create WAD cache directory '" << cachePath.parent_path().string()
                  << "': " << ec.message() << std::endl;
        return false;
    }

    const fs::path temporaryPath = cachePath.string() + ".tmp";
    std::ofstream cache(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!cache) {
        std::cerr << "MatEdit: cannot write WAD cache '" << temporaryPath.string() << "'" << std::endl;
        return false;
    }
    const std::array<char, 8> magic{'M', 'A', 'T', 'W', 'A', 'D', '0', '1'};
    const std::uint32_t version = 1;
    const std::uint32_t pathLength = static_cast<std::uint32_t>(sourceKey.size());
    const std::uint32_t textureCount = static_cast<std::uint32_t>(archive.textures.size());
    cache.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    bool valid = WriteWadCacheValue(cache, version) &&
                 WriteWadCacheValue(cache, pathLength);
    if (valid) cache.write(sourceKey.data(), static_cast<std::streamsize>(sourceKey.size()));
    valid = valid && WriteWadCacheValue(cache, sourceSize) &&
            WriteWadCacheValue(cache, sourceHash.first) &&
            WriteWadCacheValue(cache, sourceHash.second) &&
            WriteWadCacheValue(cache, textureCount);
    for (const WadTexture& texture : archive.textures) {
        if (texture.name.empty() || texture.name.size() > 16) {
            valid = false;
            break;
        }
        const auto nameLength = static_cast<std::uint8_t>(texture.name.size());
        const auto width = static_cast<std::int32_t>(texture.width);
        const auto height = static_cast<std::int32_t>(texture.height);
        valid = WriteWadCacheValue(cache, nameLength);
        if (valid) cache.write(texture.name.data(), static_cast<std::streamsize>(texture.name.size()));
        valid = valid && WriteWadCacheValue(cache, width) && WriteWadCacheValue(cache, height) &&
                WriteWadCacheValue(cache, texture.pixelOffset) && WriteWadCacheValue(cache, texture.paletteOffset);
        if (!valid) break;
    }
    cache.close();
    if (!valid || !cache) {
        std::cerr << "MatEdit: failed writing WAD cache '" << temporaryPath.string() << "'" << std::endl;
        fs::remove(temporaryPath, ec);
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(temporaryPath.c_str(), cachePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::cerr << "MatEdit: cannot replace WAD cache '" << cachePath.string()
                  << "' (Windows error " << GetLastError() << ")" << std::endl;
        fs::remove(temporaryPath, ec);
        return false;
    }
#else
    fs::rename(temporaryPath, cachePath, ec);
    if (ec) {
        std::cerr << "MatEdit: cannot replace WAD cache '" << cachePath.string() << "': " << ec.message() << std::endl;
        fs::remove(temporaryPath, ec);
        return false;
    }
#endif
    return true;
}

fs::path ResolveWadPath(const std::string& relativePath) {
    fs::path fullPath = fs::path(relativePath).is_absolute() ? fs::path(relativePath) : gameRootPath / relativePath;
    std::error_code ec;
    fs::path canonicalPath = fs::weakly_canonical(fullPath, ec);
    if (!ec) return canonicalPath;
    ec.clear();
    fs::path absolutePath = fs::absolute(fullPath, ec);
    return ec ? fullPath.lexically_normal() : absolutePath.lexically_normal();
}

bool ParseWadArchive(const std::string& relativePath, const fs::path& fullPath,
                     std::uint64_t sourceSize, WadArchive& out) {
    std::ifstream file(fullPath, std::ios::binary);
    if (!file) return false;

    WadHeaderRaw header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || (std::strncmp(header.identification, "WAD2", 4) != 0 && std::strncmp(header.identification, "WAD3", 4) != 0)) return false;
    if (header.numLumps < 0 || header.numLumps > 1000000 || header.infoTableOffset < 0) return false;
    if (static_cast<std::uint64_t>(header.infoTableOffset) +
        static_cast<std::uint64_t>(header.numLumps) * sizeof(WadLumpRaw) > sourceSize) return false;

    file.seekg(header.infoTableOffset, std::ios::beg);
    if (!file) return false;

    out = {};
    out.relativePath = relativePath;
    out.displayName = fullPath.filename().string();

    for (std::int32_t i = 0; i < header.numLumps; ++i) {
        WadLumpRaw lump{};
        file.read(reinterpret_cast<char*>(&lump), sizeof(lump));
        if (!file) return false;
        if (lump.type != 0x43 || lump.compression != 0) continue;
        if (lump.filePos < 0 || lump.diskSize < 40 || lump.diskSize > 256 * 1024 * 1024) continue;
        if (static_cast<std::uint64_t>(lump.filePos) + static_cast<std::uint64_t>(lump.diskSize) > sourceSize) continue;

        const auto directoryReturn = file.tellg();
        file.seekg(lump.filePos, std::ios::beg);
        if (!file) { file.clear(); file.seekg(directoryReturn); continue; }

        char textureName[16]{};
        std::int32_t width = 0;
        std::int32_t height = 0;
        std::int32_t offsets[4]{};
        file.read(textureName, sizeof(textureName));
        file.read(reinterpret_cast<char*>(&width), sizeof(width));
        file.read(reinterpret_cast<char*>(&height), sizeof(height));
        file.read(reinterpret_cast<char*>(offsets), sizeof(offsets));

        if (!file || width <= 0 || height <= 0 || width > 8192 || height > 8192 || offsets[0] < 40) {
            file.clear();
            file.seekg(directoryReturn);
            continue;
        }

        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        if (pixelCount > 64u * 1024u * 1024u) {
            file.clear();
            file.seekg(directoryReturn);
            continue;
        }

        WadTexture texture;
        texture.name.assign(textureName, strnlen(textureName, sizeof(textureName)));
        if (texture.name.empty()) {
            file.clear();
            file.seekg(directoryReturn);
            continue;
        }
        texture.width = width;
        texture.height = height;

        const std::streamoff lumpStart = static_cast<std::streamoff>(lump.filePos);
        const std::streamoff pixelOffset = lumpStart + static_cast<std::streamoff>(offsets[0]);
        const std::streamoff paletteOffset = lumpStart + static_cast<std::streamoff>(offsets[3]) + static_cast<std::streamoff>((width / 8) * (height / 8));
        if (pixelOffset < 0 || paletteOffset < 0 ||
            pixelOffset + static_cast<std::streamoff>(pixelCount) > lumpStart + lump.diskSize ||
            paletteOffset + 770 > lumpStart + lump.diskSize ||
            static_cast<std::uint64_t>(pixelOffset) > std::numeric_limits<std::uint32_t>::max() ||
            static_cast<std::uint64_t>(paletteOffset) > std::numeric_limits<std::uint32_t>::max()) {
            file.clear();
            file.seekg(directoryReturn);
            continue;
        }

        texture.pixelOffset = static_cast<std::uint32_t>(pixelOffset);
        texture.paletteOffset = static_cast<std::uint32_t>(paletteOffset);
        out.textures.push_back(std::move(texture));
        file.clear();
        file.seekg(directoryReturn);
    }
    return true;
}

bool ReadWadArchive(const std::string& relativePath, WadArchive& out) {
    const fs::path fullPath = ResolveWadPath(relativePath);
    std::uint64_t sourceSize = 0;
    WadContentHash sourceHash;
    if (!GetWadFingerprint(fullPath, sourceSize, sourceHash)) return false;

    std::error_code pathError;
    const std::string sourceKey = fs::weakly_canonical(fullPath, pathError).generic_string();
    const std::string cacheKey = pathError ? fullPath.generic_string() : sourceKey;
    const fs::path cachePath = GetWadCachePath(cacheKey);

    out = {};
    out.relativePath = relativePath;
    out.displayName = fullPath.filename().string();
    if (!LoadWadCache(cachePath, cacheKey, sourceSize, sourceHash, out)) {
        if (!ParseWadArchive(relativePath, fullPath, sourceSize, out)) return false;
        SaveWadCache(cachePath, cacheKey, sourceSize, sourceHash, out);
    }
    out.sourceSize = sourceSize;
    out.contentHashFirst = sourceHash.first;
    out.contentHashSecond = sourceHash.second;
    return true;
}

std::shared_ptr<const std::vector<std::uint8_t>> LoadWadSourceData(const WadArchive& archive) {
    const fs::path path = ResolveWadPath(archive.relativePath);
    const std::string key = path.generic_string();
    auto cached = g_wadFileCache.find(key);
    if (cached != g_wadFileCache.end()) {
        if (cached->second.sourceSize == archive.sourceSize &&
            cached->second.hashFirst == archive.contentHashFirst &&
            cached->second.hashSecond == archive.contentHashSecond) {
            cached->second.lastUse = ++g_wadCacheClock;
            return cached->second.data;
        }
        g_wadFileCacheBytes -= cached->second.data->size();
        g_wadFileCache.erase(cached);
    }

    if (archive.sourceSize > std::numeric_limits<std::size_t>::max() ||
        archive.sourceSize > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) return {};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamoff end = file.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) != archive.sourceSize) return {};
    file.seekg(0, std::ios::beg);

    auto data = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(archive.sourceSize));
    file.read(reinterpret_cast<char*>(data->data()), static_cast<std::streamsize>(data->size()));
    if (!file || static_cast<std::size_t>(file.gcount()) != data->size()) return {};
    WadContentHash hash;
    UpdateWadHash(hash, reinterpret_cast<const char*>(data->data()), data->size());
    if (hash.first != archive.contentHashFirst || hash.second != archive.contentHashSecond) {
        std::cerr << "MatEdit: WAD file changed after it was indexed; reload WADs: " << path.string() << std::endl;
        return {};
    }

    if (data->size() > kWadFileCacheLimit) return data;
    while (g_wadFileCacheBytes + data->size() > kWadFileCacheLimit) {
        auto oldest = g_wadFileCache.end();
        for (auto it = g_wadFileCache.begin(); it != g_wadFileCache.end(); ++it) {
            if (it->second.data.use_count() != 1) continue;
            if (oldest == g_wadFileCache.end() || it->second.lastUse < oldest->second.lastUse) oldest = it;
        }
        if (oldest == g_wadFileCache.end()) return data;
        g_wadFileCacheBytes -= oldest->second.data->size();
        g_wadFileCache.erase(oldest);
    }

    g_wadFileCacheBytes += data->size();
    g_wadFileCache.emplace(key, CachedWadFile{
        data, archive.sourceSize, archive.contentHashFirst, archive.contentHashSecond, ++g_wadCacheClock
    });
    return data;
}

bool ReadWadTextureBytes(const WadArchive& archive, const WadTexture& texture,
                         std::vector<std::uint8_t>& indices,
                         std::array<std::uint8_t, 256 * 3>& palette) {
    const std::size_t pixelCount = static_cast<std::size_t>(texture.width) * static_cast<std::size_t>(texture.height);
    if (static_cast<std::uint64_t>(texture.pixelOffset) + pixelCount > archive.sourceSize ||
        static_cast<std::uint64_t>(texture.paletteOffset) + 770u > archive.sourceSize) return false;
    indices.resize(pixelCount);

    if (archive.sourceSize <= kWadFileCacheLimit) {
        const std::shared_ptr<const std::vector<std::uint8_t>> wadData = LoadWadSourceData(archive);
        if (!wadData) return false;
        std::memcpy(indices.data(), wadData->data() + texture.pixelOffset, indices.size());
        std::uint16_t paletteCount = 0;
        std::memcpy(&paletteCount, wadData->data() + texture.paletteOffset, sizeof(paletteCount));
        if (paletteCount < 256) return false;
        std::memcpy(palette.data(), wadData->data() + texture.paletteOffset + sizeof(paletteCount), palette.size());
        return true;
    }

    const fs::path path = ResolveWadPath(archive.relativePath);
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(static_cast<std::streamoff>(texture.pixelOffset), std::ios::beg);
    file.read(reinterpret_cast<char*>(indices.data()), static_cast<std::streamsize>(indices.size()));
    if (!file) return false;
    file.seekg(static_cast<std::streamoff>(texture.paletteOffset), std::ios::beg);
    std::uint16_t paletteCount = 0;
    file.read(reinterpret_cast<char*>(&paletteCount), sizeof(paletteCount));
    if (!file || paletteCount < 256) return false;
    file.read(reinterpret_cast<char*>(palette.data()), static_cast<std::streamsize>(palette.size()));
    return static_cast<bool>(file);
}

}

std::string ResolveWadTextureReference(const std::string& reference);

TexturePreviewInfo LoadTexturePreview(const std::string& reference) {
    TexturePreviewInfo info{};
    const WadArchive* wad = nullptr;
    const WadTexture* wadTexture = nullptr;
    std::string resolvedReference = reference;

    if (reference.rfind("wad://", 0) != 0) {
        const std::string wadReference = ResolveWadTextureReference(reference);
        if (!wadReference.empty()) resolvedReference = wadReference;
    }

    if (resolvedReference.rfind("wad://", 0) == 0) {
        const std::string encoded = resolvedReference.substr(6);
        const std::size_t separator = encoded.rfind('#');
        if (separator == std::string::npos) return info;
        const std::string wadPath = encoded.substr(0, separator);
        const std::string textureName = encoded.substr(separator + 1);
        if (!FindWadTexture(wadPath, textureName, wad, wadTexture) ||
            wadTexture->pixelOffset == 0 || wadTexture->paletteOffset == 0) return info;

        const std::size_t pixelCount = static_cast<std::size_t>(wadTexture->width) * static_cast<std::size_t>(wadTexture->height);
        std::vector<std::uint8_t> indices;
        std::array<std::uint8_t, 256 * 3> palette{};
        if (!ReadWadTextureBytes(*wad, *wadTexture, indices, palette)) return info;

        std::vector<unsigned char> rgba(pixelCount * 4u);
        for (std::size_t px = 0; px < pixelCount; ++px) {
            const unsigned int index = indices[px];
            rgba[px * 4u + 0] = palette[index * 3u + 0];
            rgba[px * 4u + 1] = palette[index * 3u + 1];
            rgba[px * 4u + 2] = palette[index * 3u + 2];
            rgba[px * 4u + 3] = (!wadTexture->name.empty() && wadTexture->name[0] == '{' && index == 255u) ? 0u : 255u;
        }

        glGenTextures(1, &info.texture);
        glBindTexture(GL_TEXTURE_2D, info.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        RegisterTextureSampling(info.texture, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, wadTexture->width, wadTexture->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (glGetError() != GL_NO_ERROR) {
            ReleaseTexturePreview(info);
            return {};
        }
        info.width = wadTexture->width;
        info.height = wadTexture->height;
        info.valid = true;
        g_textureFormatInfo[info.texture] = TextureFormatInfo{true, false, false, false, 4};
        return info;
    }

    const fs::path referencePath(reference);
    const std::string extension = ToLower(referencePath.extension().string());
    if (extension == ".png" || extension == ".tga" || extension.empty() || extension != ".dds") return info;

    const GLuint texture = LoadDDSTexture(reference);
    if (!texture) return info;
    GLint width = 0, height = 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    glBindTexture(GL_TEXTURE_2D, 0);
    info.texture = texture;
    info.width = std::max(0, width);
    info.height = std::max(0, height);
    info.valid = width > 0 && height > 0;
    info.cachedDDS = true;
    if (!info.valid) ReleaseTexturePreview(info);
    return info;
}

std::string ResolveWadTextureReference(const std::string& reference) {
    std::string name = reference;
    if (name.rfind("wad://", 0) == 0) {
        const std::size_t separator = name.rfind('#');
        if (separator == std::string::npos) return {};
        name = name.substr(separator + 1);
    } else if (name.rfind("wad:/", 0) == 0) {
        name = name.substr(5);
        const std::size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
    }

    const std::string extension = ToLower(fs::path(name).extension().string());
    if (extension == ".png" || extension == ".tga" || extension == ".dds") name = fs::path(name).stem().string();
    if (name.empty()) return {};

    const auto lookup = g_wadTextureNameLookup.find(ToLower(name));
    if (lookup == g_wadTextureNameLookup.end() ||
        lookup->second.first >= g_wadArchives.size()) return {};
    const WadArchive& wad = g_wadArchives[lookup->second.first];
    if (lookup->second.second >= wad.textures.size()) return {};
    return MakeWadTextureReference(wad, wad.textures[lookup->second.second]);
}

GLuint LoadTextureReference(const std::string& reference, bool* goldSrcWadTransparency) {
    if (goldSrcWadTransparency) *goldSrcWadTransparency = false;
    const fs::path referencePath(reference);
    const std::string extension = ToLower(referencePath.extension().string());
    if (extension == ".png" || extension == ".tga") return 0;

    const bool isWadReference = reference.rfind("wad://", 0) == 0 || reference.rfind("wad:/", 0) == 0;
    if (!isWadReference && extension == ".dds") return LoadDDSTexture(reference);

    const std::string wadReference = ResolveWadTextureReference(reference);
    if (!wadReference.empty()) {
        TexturePreviewInfo preview = LoadTexturePreview(wadReference);
        if (preview.texture != 0 && goldSrcWadTransparency) {
            const std::size_t separator = wadReference.rfind('#');
            if (separator != std::string::npos) {
                const std::string textureName = wadReference.substr(separator + 1);
                *goldSrcWadTransparency = !textureName.empty() && textureName[0] == '{';
            }
        }
        const GLuint texture = preview.texture;
        preview.texture = 0;
        ReleaseTexturePreview(preview);
        return texture;
    }

    if (isWadReference || !extension.empty()) return 0;
    return LoadDDSTexture(reference);
}

void ReleaseTexturePreview(TexturePreviewInfo& preview) {
    if (preview.texture) {
        if (preview.cachedDDS) ReleaseDDSTexture(preview.texture);
        else {
            g_textureFormatInfo.erase(preview.texture);
            g_textureMaxMipLevels.erase(preview.texture);
            glDeleteTextures(1, &preview.texture);
        }
    }
    preview = {};
}

const std::vector<WadArchive>& GetWadArchives() { return g_wadArchives; }

bool AddWadArchive(const std::string& relativePath) {
    if (g_wadArchiveLookup.find(ToLower(relativePath)) != g_wadArchiveLookup.end()) return true;
    WadArchive wad;
    if (!ReadWadArchive(relativePath, wad)) return false;
    g_wadArchives.push_back(std::move(wad));
    const std::size_t archiveIndex = g_wadArchives.size() - 1;
    const WadArchive& loaded = g_wadArchives.back();
    g_wadArchiveLookup.emplace(ToLower(loaded.relativePath), archiveIndex);
    for (std::size_t textureIndex = 0; textureIndex < loaded.textures.size(); ++textureIndex) {
        const WadTexture& texture = loaded.textures[textureIndex];
        g_wadTextureLookup.emplace(MakeWadTextureLookupKey(loaded.relativePath, texture.name),
                                   std::make_pair(archiveIndex, textureIndex));
        g_wadTextureNameLookup.try_emplace(ToLower(texture.name), archiveIndex, textureIndex);
        const std::string extension = ToLower(fs::path(texture.name).extension().string());
        if (extension == ".png" || extension == ".tga" || extension == ".dds") {
            g_wadTextureNameLookup.try_emplace(ToLower(fs::path(texture.name).stem().string()), archiveIndex, textureIndex);
        }
    }
    return true;
}

void LoadWadArchives(const std::vector<std::string>& paths) {
    ClearWadArchives();
    for (const auto& path : paths) AddWadArchive(path);
}

void ScanAndLoadAllWads() {
    ClearWadArchives();
    if (gameRootPath.empty() || !fs::exists(gameRootPath)) return;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(gameRootPath, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".wad" && it->path().extension() != ".WAD") continue;
        std::error_code rec;
        const std::string relative = fs::relative(it->path(), gameRootPath, rec).generic_string();
        if (!rec) AddWadArchive(relative);
    }
}

void ClearWadArchives() {
    g_wadArchives.clear();
    g_wadArchiveLookup.clear();
    g_wadTextureLookup.clear();
    g_wadTextureNameLookup.clear();
}

std::vector<std::string> GetLoadedWadPaths() {
    std::vector<std::string> paths;
    paths.reserve(g_wadArchives.size());
    for (const auto& wad : g_wadArchives) paths.push_back(wad.relativePath);
    return paths;
}

std::string MakeWadTextureReference(const WadArchive& wad, const WadTexture& texture) {
    return std::string("wad://") + wad.relativePath + "#" + texture.name;
}


namespace {
#pragma pack(push, 1)
struct DDS_PIXELFORMAT_RAW {
    std::uint32_t size;
    std::uint32_t flags;
    std::uint32_t fourCC;
    std::uint32_t rgbBitCount;
    std::uint32_t rMask;
    std::uint32_t gMask;
    std::uint32_t bMask;
    std::uint32_t aMask;
};

struct DDS_HEADER_RAW {
    std::uint32_t size;
    std::uint32_t flags;
    std::uint32_t height;
    std::uint32_t width;
    std::uint32_t pitchOrLinearSize;
    std::uint32_t depth;
    std::uint32_t mipMapCount;
    std::uint32_t reserved1[11];
    DDS_PIXELFORMAT_RAW ddspf;
    std::uint32_t caps;
    std::uint32_t caps2;
    std::uint32_t caps3;
    std::uint32_t caps4;
    std::uint32_t reserved2;
};

struct DDS_HEADER_DX10_RAW {
    std::uint32_t dxgiFormat;
    std::uint32_t resourceDimension;
    std::uint32_t miscFlag;
    std::uint32_t arraySize;
    std::uint32_t miscFlags2;
};
#pragma pack(pop)

constexpr std::uint32_t DDS_MAGIC = 0x20534444u;
constexpr std::uint32_t DDSD_CAPS = 0x1u;
constexpr std::uint32_t DDSD_HEIGHT = 0x2u;
constexpr std::uint32_t DDSD_WIDTH = 0x4u;
constexpr std::uint32_t DDSD_PIXELFORMAT = 0x1000u;
constexpr std::uint32_t DDSD_MIPMAPCOUNT = 0x20000u;
constexpr std::uint32_t DDSD_LINEARSIZE = 0x80000u;
constexpr std::uint32_t DDSCAPS_COMPLEX = 0x8u;
constexpr std::uint32_t DDSCAPS_TEXTURE = 0x1000u;
constexpr std::uint32_t DDSCAPS_MIPMAP = 0x400000u;
constexpr std::uint32_t DDPF_FOURCC = 0x4u;
constexpr std::uint32_t D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3u;
constexpr std::uint32_t DXGI_FORMAT_BC4_UNORM = 80u;
constexpr std::uint32_t DXGI_FORMAT_BC5_UNORM = 83u;
constexpr std::uint32_t DXGI_FORMAT_BC7_UNORM = 98u;
constexpr GLenum GL_COMPRESSED_RGBA_BPTC_UNORM_VALUE = 0x8E8Cu;
constexpr GLenum GL_COMPRESSED_IMAGE_SIZE_VALUE = 0x86A0u;

std::uint32_t FourCC(const char a, const char b, const char c, const char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24u);
}

void BuildLuma(const TexturePixels& source, std::vector<std::uint8_t>& luma) {
    const std::size_t count = static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height);
    luma.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float r = source.rgba[i * 4u + 0u] / 255.0f;
        const float g = source.rgba[i * 4u + 1u] / 255.0f;
        const float b = source.rgba[i * 4u + 2u] / 255.0f;
        const float value = std::clamp(0.2126f * r + 0.7152f * g + 0.0722f * b, 0.0f, 1.0f);
        luma[i] = static_cast<std::uint8_t>(std::lround(value * 255.0f));
    }
}

void BuildHeight(const TexturePixels& source, int channel, bool invert, std::vector<std::uint8_t>& height) {
    const std::size_t count = static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height);
    channel = std::clamp(channel, 0, 4);
    height.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        float value = 0.0f;
        if (channel == 0) {
            const float r = source.rgba[i * 4u + 0u] / 255.0f;
            const float g = source.rgba[i * 4u + 1u] / 255.0f;
            const float b = source.rgba[i * 4u + 2u] / 255.0f;
            value = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        } else {
            value = source.rgba[i * 4u + static_cast<std::size_t>(channel - 1)] / 255.0f;
        }
        if (invert) value = 1.0f - value;
        height[i] = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    }
}

void ApplyHeightRange(std::vector<std::uint8_t>& height, float blackPoint, float whitePoint, float gamma = 1.0f) {
    const float black = std::clamp(blackPoint, 0.0f, 0.9999f);
    const float white = std::clamp(whitePoint, black + 0.0001f, 1.0f);
    const float inverseRange = 1.0f / (white - black);
    const float safeGamma = std::clamp(gamma, 0.1f, 4.0f);
    for (std::uint8_t& value : height) {
        float adjusted = std::clamp((value / 255.0f - black) * inverseRange, 0.0f, 1.0f);
        adjusted = std::pow(adjusted, safeGamma);
        value = static_cast<std::uint8_t>(adjusted * 255.0f + 0.5f);
    }
}

void SmoothHeight(std::vector<std::uint8_t>& height, int width, int heightPixels, float amount, bool tileEdges) {
    const float blend = std::clamp(amount, 0.0f, 1.0f);
    if (blend <= 0.0f || width <= 0 || heightPixels <= 0) return;
    const std::vector<std::uint8_t> source = height;
    auto sample = [&](int x, int y) -> float {
        if (tileEdges) {
            x = (x % width + width) % width;
            y = (y % heightPixels + heightPixels) % heightPixels;
        } else {
            x = std::clamp(x, 0, width - 1);
            y = std::clamp(y, 0, heightPixels - 1);
        }
        return source[static_cast<std::size_t>(y) * width + x];
    };
    for (int y = 0; y < heightPixels; ++y) {
        for (int x = 0; x < width; ++x) {
            const float blur = (sample(x - 1, y - 1) + 2.0f * sample(x, y - 1) + sample(x + 1, y - 1) +
                                2.0f * sample(x - 1, y) + 4.0f * sample(x, y) + 2.0f * sample(x + 1, y) +
                                sample(x - 1, y + 1) + 2.0f * sample(x, y + 1) + sample(x + 1, y + 1)) / 16.0f;
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            height[index] = static_cast<std::uint8_t>(std::clamp(source[index] * (1.0f - blend) + blur * blend, 0.0f, 255.0f) + 0.5f);
        }
    }
}

void ApplyBumpHeightSettings(std::vector<std::uint8_t>& height, float contrast, float brightness, bool normalize) {
    if (normalize && !height.empty()) {
        const auto minmax = std::minmax_element(height.begin(), height.end());
        const int minValue = *minmax.first;
        const int maxValue = *minmax.second;
        if (maxValue > minValue) {
            const float scale = 255.0f / static_cast<float>(maxValue - minValue);
            for (std::uint8_t& value : height) {
                value = static_cast<std::uint8_t>(std::clamp((static_cast<int>(value) - minValue) * scale, 0.0f, 255.0f));
            }
        }
    }

    const float safeContrast = std::max(0.0f, contrast);
    if (safeContrast == 1.0f && brightness == 0.0f) return;
    for (std::uint8_t& value : height) {
        float adjusted = value / 255.0f;
        adjusted = (adjusted - 0.5f) * safeContrast + 0.5f + brightness;
        value = static_cast<std::uint8_t>(std::clamp(adjusted, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
}

void DownsampleGray(const std::vector<std::uint8_t>& src, int width, int height, std::vector<std::uint8_t>& dst, int& outWidth, int& outHeight) {
    outWidth = std::max(1, width / 2);
    outHeight = std::max(1, height / 2);
    dst.resize(static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight));
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            const int x0 = std::min(width - 1, x * 2);
            const int x1 = std::min(width - 1, x0 + 1);
            const int y0 = std::min(height - 1, y * 2);
            const int y1 = std::min(height - 1, y0 + 1);
            const unsigned value = static_cast<unsigned>(src[static_cast<std::size_t>(y0) * width + x0]) +
                                   static_cast<unsigned>(src[static_cast<std::size_t>(y0) * width + x1]) +
                                   static_cast<unsigned>(src[static_cast<std::size_t>(y1) * width + x0]) +
                                   static_cast<unsigned>(src[static_cast<std::size_t>(y1) * width + x1]);
            dst[static_cast<std::size_t>(y) * outWidth + x] = static_cast<std::uint8_t>((value + 2u) / 4u);
        }
    }
}

TexturePreviewInfo UploadCreatorPreviewRGBA(const std::vector<std::uint8_t>& rgba, int width, int height, bool mipmaps) {
    TexturePreviewInfo preview{};
    if (rgba.empty() || width <= 0 || height <= 0) return preview;

    while (glGetError() != GL_NO_ERROR) {}
    glGenTextures(1, &preview.texture);
    glBindTexture(GL_TEXTURE_2D, preview.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    RegisterTextureSampling(preview.texture, mipmaps ? GetFullMipLevel(width, height) : 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    if (mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (glGetError() != GL_NO_ERROR) {
        g_textureMaxMipLevels.erase(preview.texture);
        glDeleteTextures(1, &preview.texture);
        return {};
    }

    preview.width = width;
    preview.height = height;
    preview.valid = true;
    preview.cachedDDS = false;
    return preview;
}

void DownsampleRGBA(const std::vector<std::uint8_t>& src, int width, int height, std::vector<std::uint8_t>& dst, int& outWidth, int& outHeight) {
    outWidth = std::max(1, width / 2);
    outHeight = std::max(1, height / 2);
    dst.resize(static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight) * 4u);
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            const int x0 = std::min(width - 1, x * 2);
            const int x1 = std::min(width - 1, x0 + 1);
            const int y0 = std::min(height - 1, y * 2);
            const int y1 = std::min(height - 1, y0 + 1);
            const std::size_t out = (static_cast<std::size_t>(y) * outWidth + x) * 4u;
            for (int c = 0; c < 4; ++c) {
                const unsigned value = src[(static_cast<std::size_t>(y0) * width + x0) * 4u + c] +
                                       src[(static_cast<std::size_t>(y0) * width + x1) * 4u + c] +
                                       src[(static_cast<std::size_t>(y1) * width + x0) * 4u + c] +
                                       src[(static_cast<std::size_t>(y1) * width + x1) * 4u + c];
                dst[out + c] = static_cast<std::uint8_t>((value + 2u) / 4u);
            }
        }
    }
}

bool CompressBC7(const std::vector<std::vector<std::uint8_t>>& rgbaMips, const std::vector<std::pair<int, int>>& sizes, std::vector<std::vector<std::uint8_t>>& blocks) {
    if (rgbaMips.empty() || rgbaMips.size() != sizes.size()) return false;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture) return false;
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (std::size_t level = 0; level < rgbaMips.size(); ++level) {
        const int width = sizes[level].first;
        const int height = sizes[level].second;
        glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), GL_COMPRESSED_RGBA_BPTC_UNORM_VALUE, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaMips[level].data());
        GLint size = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, static_cast<GLint>(level), GL_COMPRESSED_IMAGE_SIZE_VALUE, &size);
        if (size <= 0) {
            glBindTexture(GL_TEXTURE_2D, 0);
            glDeleteTextures(1, &texture);
            return false;
        }
        blocks.emplace_back(static_cast<std::size_t>(size));
        glGetCompressedTexImage(GL_TEXTURE_2D, static_cast<GLint>(level), blocks.back().data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &texture);
    return glGetError() == GL_NO_ERROR;
}

void ApplyCreatorSharpness(TexturePixels& pixels, float sharpness) {
    if (sharpness <= 0.0f || pixels.width < 2 || pixels.height < 2) return;
    const float amount = std::clamp(sharpness, 0.0f, 4.0f);
    const std::vector<std::uint8_t> original = pixels.rgba;
    for (int y = 0; y < pixels.height; ++y) {
        for (int x = 0; x < pixels.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                auto sample = [&](int sx, int sy) -> float {
                    sx = std::clamp(sx, 0, pixels.width - 1);
                    sy = std::clamp(sy, 0, pixels.height - 1);
                    return original[(static_cast<std::size_t>(sy) * pixels.width + sx) * 4u + static_cast<std::size_t>(c)];
                };
                const float center = sample(x, y);
                const float blur = (sample(x - 1, y) + sample(x + 1, y) + sample(x, y - 1) + sample(x, y + 1)) * 0.25f;
                const float value = center + (center - blur) * amount;
                pixels.rgba[(static_cast<std::size_t>(y) * pixels.width + x) * 4u + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f));
            }
        }
    }
}

void BuildNormalRGBA(const std::vector<std::uint8_t>& height, int width, int heightPixels,
                     const NormalMapSettings& settings, std::vector<std::uint8_t>& rgba) {
    rgba.resize(height.size() * 4u);
    const float scale = std::max(0.0f, settings.strength);
    auto sample = [&](int x, int y) -> float {
        if (settings.tileEdges) {
            x = (x % width + width) % width;
            y = (y % heightPixels + heightPixels) % heightPixels;
        } else {
            x = std::clamp(x, 0, width - 1);
            y = std::clamp(y, 0, heightPixels - 1);
        }
        return height[static_cast<std::size_t>(y) * width + x] / 255.0f;
    };
    for (int y = 0; y < heightPixels; ++y) {
        for (int x = 0; x < width; ++x) {
            float dx = 0.0f;
            float dy = 0.0f;
            if (settings.gradientFilter == 1) {
                dx = (sample(x + 1, y) - sample(x - 1, y)) * 4.0f;
                dy = (sample(x, y + 1) - sample(x, y - 1)) * 4.0f;
            } else if (settings.gradientFilter == 2) {
                dx = 3.0f * (sample(x + 1, y - 1) - sample(x - 1, y - 1)) +
                     10.0f * (sample(x + 1, y) - sample(x - 1, y)) +
                     3.0f * (sample(x + 1, y + 1) - sample(x - 1, y + 1));
                dy = 3.0f * (sample(x - 1, y + 1) - sample(x - 1, y - 1)) +
                     10.0f * (sample(x, y + 1) - sample(x, y - 1)) +
                     3.0f * (sample(x + 1, y + 1) - sample(x + 1, y - 1));
                dx /= 16.0f;
                dy /= 16.0f;
            } else {
                dx = (sample(x + 1, y - 1) + 2.0f * sample(x + 1, y) + sample(x + 1, y + 1)) -
                     (sample(x - 1, y - 1) + 2.0f * sample(x - 1, y) + sample(x - 1, y + 1));
                dy = (sample(x - 1, y + 1) + 2.0f * sample(x, y + 1) + sample(x + 1, y + 1)) -
                     (sample(x - 1, y - 1) + 2.0f * sample(x, y - 1) + sample(x + 1, y - 1));
            }
            float nx = -dx * scale;
            float ny = -dy * scale;
            float nz = 1.0f;
            const float inverseLength = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= inverseLength;
            ny *= inverseLength;
            if (settings.flipX) nx = -nx;
            if (settings.flipY) ny = -ny;
            const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4u;
            rgba[index + 0u] = static_cast<std::uint8_t>(std::clamp(nx * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
            rgba[index + 1u] = static_cast<std::uint8_t>(std::clamp(ny * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
            const float encodedZ = settings.fullZRange ? nz : (nz * 0.5f + 0.5f);
            rgba[index + 2u] = static_cast<std::uint8_t>(std::clamp(encodedZ, 0.0f, 1.0f) * 255.0f + 0.5f);
            rgba[index + 3u] = 255;
        }
    }
}

void BuildGlossPixels(const TexturePixels& pixels, const GlossMapSettings& settings, std::vector<std::uint8_t>& output) {
    std::vector<std::uint8_t> luma;
    BuildLuma(pixels, luma);
    output.resize(luma.size());
    const float contrast = std::max(0.0f, settings.contrast);
    const float power = std::max(0.01f, settings.power);
    const float lower = std::clamp(std::min(settings.lowerThreshold, settings.upperThreshold), 0.0f, 0.9999f);
    const float upper = std::clamp(std::max(settings.lowerThreshold, settings.upperThreshold), lower + 0.0001f, 1.0f);
    const float softness = std::clamp(settings.softness, 0.0f, 1.0f);

    for (std::size_t i = 0; i < luma.size(); ++i) {
        const float red = pixels.rgba[i * 4u + 0u] / 255.0f;
        const float green = pixels.rgba[i * 4u + 1u] / 255.0f;
        const float blue = pixels.rgba[i * 4u + 2u] / 255.0f;
        const float luminance = luma[i] / 255.0f;
        float sourceValue = 0.0f;
        if (settings.sourceMode == 1) sourceValue = red;
        else if (settings.sourceMode == 2) sourceValue = green;
        else if (settings.sourceMode == 3) sourceValue = blue;
        else if (settings.sourceMode == 4) sourceValue = pixels.rgba[i * 4u + 3u] / 255.0f;
        else sourceValue = luminance;

        float value = settings.normalize
            ? std::clamp((sourceValue - lower) / (upper - lower), 0.0f, 1.0f)
            : std::clamp(sourceValue, lower, upper);
        const float smoothValue = value * value * (3.0f - 2.0f * value);
        value = value * (1.0f - softness) + smoothValue * softness;
        if (settings.invert) value = 1.0f - value;
        value = std::clamp((value - 0.5f) * contrast + 0.5f + settings.brightness, 0.0f, 1.0f);
        value = std::pow(value, power);
        output[i] = static_cast<std::uint8_t>(value * 255.0f + 0.5f);
    }
}

TexturePreviewInfo GenerateNormalMapPreviewTextureImpl(const std::string& source, const NormalMapSettings& settings) {
    TexturePixels pixels;
    if (!LoadTexturePixels(source, pixels) || pixels.width <= 0 || pixels.height <= 0) return {};
    ApplyCreatorSharpness(pixels, settings.sharpness);
    std::vector<std::uint8_t> height;
    BuildHeight(pixels, settings.heightChannel, settings.invertHeight, height);
    ApplyHeightRange(height, settings.blackPoint, settings.whitePoint);
    SmoothHeight(height, pixels.width, pixels.height, settings.smoothing, settings.tileEdges);
    std::vector<std::uint8_t> rgba;
    BuildNormalRGBA(height, pixels.width, pixels.height, settings, rgba);
    return UploadCreatorPreviewRGBA(rgba, pixels.width, pixels.height, settings.mipmaps);
}

TexturePreviewInfo GenerateBumpMapPreviewTextureImpl(const std::string& source, const BumpMapSettings& settings) {
    TexturePixels pixels;
    if (!LoadTexturePixels(source, pixels) || pixels.width <= 0 || pixels.height <= 0) return {};
    ApplyCreatorSharpness(pixels, settings.sharpness);
    std::vector<std::uint8_t> height;
    BuildHeight(pixels, settings.heightChannel, settings.invert, height);
    ApplyHeightRange(height, settings.blackPoint, settings.whitePoint, settings.gamma);
    SmoothHeight(height, pixels.width, pixels.height, settings.smoothing, settings.tileEdges);
    ApplyBumpHeightSettings(height, settings.contrast, settings.brightness, settings.normalize);

    std::vector<std::uint8_t> rgba(height.size() * 4u);
    for (std::size_t i = 0; i < height.size(); ++i) {
        const std::uint8_t value = height[i];
        rgba[i * 4u + 0u] = value;
        rgba[i * 4u + 1u] = value;
        rgba[i * 4u + 2u] = value;
        rgba[i * 4u + 3u] = 255;
    }
    return UploadCreatorPreviewRGBA(rgba, pixels.width, pixels.height, settings.mipmaps);
}

TexturePreviewInfo GenerateGlossMapPreviewTextureImpl(const std::string& source, const GlossMapSettings& settings) {
    TexturePixels pixels;
    if (!LoadTexturePixels(source, pixels) || pixels.width <= 0 || pixels.height <= 0) return {};
    ApplyCreatorSharpness(pixels, settings.sharpness);
    std::vector<std::uint8_t> gloss;
    BuildGlossPixels(pixels, settings, gloss);
    std::vector<std::uint8_t> rgba(gloss.size() * 4u);
    for (std::size_t i = 0; i < gloss.size(); ++i) {
        rgba[i * 4u + 0u] = gloss[i];
        rgba[i * 4u + 1u] = gloss[i];
        rgba[i * 4u + 2u] = gloss[i];
        rgba[i * 4u + 3u] = 255;
    }
    return UploadCreatorPreviewRGBA(rgba, pixels.width, pixels.height, settings.mipmaps);
}

void EncodeBC4Block(const std::uint8_t* values, int stride, std::uint8_t out[8]) {
    std::uint8_t maxValue = 0;
    std::uint8_t minValue = 255;
    std::uint8_t samples[16]{};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const std::uint8_t value = values[y * stride + x];
            samples[y * 4 + x] = value;
            maxValue = std::max(maxValue, value);
            minValue = std::min(minValue, value);
        }
    }

    out[0] = maxValue;
    out[1] = minValue;
    std::uint8_t palette[8]{};
    palette[0] = maxValue;
    palette[1] = minValue;
    for (int i = 2; i < 8; ++i) {
        palette[i] = static_cast<std::uint8_t>(((8 - i) * static_cast<unsigned>(maxValue) + (i - 1) * static_cast<unsigned>(minValue) + 3u) / 7u);
    }

    std::uint64_t indices = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        int bestDistance = 1 << 30;
        for (int j = 0; j < 8; ++j) {
            const int distance = std::abs(static_cast<int>(samples[i]) - static_cast<int>(palette[j]));
            if (distance < bestDistance) {
                bestDistance = distance;
                best = j;
            }
        }
        indices |= static_cast<std::uint64_t>(best) << (3 * i);
    }
    for (int i = 0; i < 6; ++i) out[2 + i] = static_cast<std::uint8_t>((indices >> (8 * i)) & 0xFFu);
}

void EncodeBC4(const std::vector<std::uint8_t>& image, int width, int height, std::vector<std::uint8_t>& blocks) {
    const int blockWidth = std::max(1, (width + 3) / 4);
    const int blockHeight = std::max(1, (height + 3) / 4);
    blocks.resize(static_cast<std::size_t>(blockWidth) * static_cast<std::size_t>(blockHeight) * 8u);
    std::uint8_t block[16]{};
    std::uint8_t encoded[8]{};
    for (int by = 0; by < blockHeight; ++by) {
        for (int bx = 0; bx < blockWidth; ++bx) {
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    const int sx = std::min(width - 1, bx * 4 + x);
                    const int sy = std::min(height - 1, by * 4 + y);
                    block[y * 4 + x] = image[static_cast<std::size_t>(sy) * width + sx];
                }
            }
            EncodeBC4Block(block, 4, encoded);
            std::memcpy(blocks.data() + (static_cast<std::size_t>(by) * blockWidth + bx) * 8u, encoded, 8u);
        }
    }
}

void EncodeBC5(const std::vector<std::uint8_t>& red, const std::vector<std::uint8_t>& green, int width, int height, std::vector<std::uint8_t>& blocks) {
    const int blockWidth = std::max(1, (width + 3) / 4);
    const int blockHeight = std::max(1, (height + 3) / 4);
    blocks.resize(static_cast<std::size_t>(blockWidth) * static_cast<std::size_t>(blockHeight) * 16u);
    std::uint8_t blockR[16]{}, blockG[16]{}, encodedR[8]{}, encodedG[8]{};
    for (int by = 0; by < blockHeight; ++by) {
        for (int bx = 0; bx < blockWidth; ++bx) {
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    const int sx = std::min(width - 1, bx * 4 + x);
                    const int sy = std::min(height - 1, by * 4 + y);
                    blockR[y * 4 + x] = red[static_cast<std::size_t>(sy) * width + sx];
                    blockG[y * 4 + x] = green[static_cast<std::size_t>(sy) * width + sx];
                }
            }
            EncodeBC4Block(blockR, 4, encodedR);
            EncodeBC4Block(blockG, 4, encodedG);
            const std::size_t offset = (static_cast<std::size_t>(by) * blockWidth + bx) * 16u;
            std::memcpy(blocks.data() + offset, encodedR, 8u);
            std::memcpy(blocks.data() + offset + 8u, encodedG, 8u);
        }
    }
}

bool WriteDDS(const std::string& outputPath, int width, int height, const std::vector<std::vector<std::uint8_t>>& mipData, std::uint32_t dxgiFormat, int blockBytes, std::uint32_t legacyFourCC = 0u) {
    (void)blockBytes;
    if (width <= 0 || height <= 0 || mipData.empty()) return false;
    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    const std::uint32_t mipCount = static_cast<std::uint32_t>(mipData.size());
    const bool useLegacyHeader = legacyFourCC != 0u;
    DDS_HEADER_RAW header{};
    header.size = 124u;
    header.flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_LINEARSIZE;
    if (mipCount > 1) header.flags |= DDSD_MIPMAPCOUNT;
    header.height = static_cast<std::uint32_t>(height);
    header.width = static_cast<std::uint32_t>(width);
    header.pitchOrLinearSize = static_cast<std::uint32_t>(mipData.front().size());
    header.mipMapCount = mipCount;
    header.ddspf.size = 32u;
    header.ddspf.flags = DDPF_FOURCC;
    header.ddspf.fourCC = useLegacyHeader ? legacyFourCC : FourCC('D', 'X', '1', '0');
    header.caps = DDSCAPS_TEXTURE;
    if (mipCount > 1) header.caps |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP;

    DDS_HEADER_DX10_RAW dx10{};
    dx10.dxgiFormat = dxgiFormat;
    dx10.resourceDimension = D3D10_RESOURCE_DIMENSION_TEXTURE2D;
    dx10.arraySize = 1u;

    file.write(reinterpret_cast<const char*>(&DDS_MAGIC), sizeof(DDS_MAGIC));
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!useLegacyHeader) file.write(reinterpret_cast<const char*>(&dx10), sizeof(dx10));
    for (const auto& mip : mipData) file.write(reinterpret_cast<const char*>(mip.data()), static_cast<std::streamsize>(mip.size()));
    return static_cast<bool>(file);
}

bool LoadWadPixels(const std::string& reference, TexturePixels& pixels) {
    std::string encoded = reference;
    if (encoded.rfind("wad://", 0) == 0) encoded = encoded.substr(6);
    else if (encoded.rfind("wad:/", 0) == 0) encoded = encoded.substr(5);
    else return false;
    const std::size_t separator = encoded.rfind('#');
    if (separator == std::string::npos) {
        const std::string name = encoded;
        const std::string resolved = ResolveWadTextureReference(name);
        if (resolved.empty()) return false;
        return LoadWadPixels(resolved, pixels);
    }
    const std::string wadPath = encoded.substr(0, separator);
    const std::string textureName = encoded.substr(separator + 1);
    const WadArchive* wad = nullptr;
    const WadTexture* texture = nullptr;
    if (!FindWadTexture(wadPath, textureName, wad, texture)) return false;
    const std::size_t count = static_cast<std::size_t>(texture->width) * static_cast<std::size_t>(texture->height);
    std::vector<std::uint8_t> indices;
    std::array<std::uint8_t, 256 * 3> palette{};
    if (!ReadWadTextureBytes(*wad, *texture, indices, palette)) return false;

    pixels.width = texture->width;
    pixels.height = texture->height;
    pixels.rgba.resize(count * 4u);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t paletteIndex = static_cast<std::size_t>(indices[i]) * 3u;
        pixels.rgba[i * 4u + 0u] = palette[paletteIndex + 0u];
        pixels.rgba[i * 4u + 1u] = palette[paletteIndex + 1u];
        pixels.rgba[i * 4u + 2u] = palette[paletteIndex + 2u];
        pixels.rgba[i * 4u + 3u] = (!texture->name.empty() && texture->name[0] == '{' && indices[i] == 255u) ? 0u : 255u;
    }
    return true;
}
}

TexturePreviewInfo GenerateNormalMapPreviewTexture(const std::string& source, const NormalMapSettings& settings) {
    return GenerateNormalMapPreviewTextureImpl(source, settings);
}

TexturePreviewInfo GenerateGlossMapPreviewTexture(const std::string& source, const GlossMapSettings& settings) {
    return GenerateGlossMapPreviewTextureImpl(source, settings);
}

TexturePreviewInfo GenerateBumpMapPreviewTexture(const std::string& source, const BumpMapSettings& settings) {
    return GenerateBumpMapPreviewTextureImpl(source, settings);
}

bool LoadTexturePixels(const std::string& reference, TexturePixels& pixels) {
    pixels = {};
    if (reference.empty()) return false;
    std::string resolved = reference;
    if (resolved.rfind("wad://", 0) == 0 || resolved.rfind("wad:/", 0) == 0) return LoadWadPixels(resolved, pixels);
    const std::string wadReference = ResolveWadTextureReference(resolved);
    if (!wadReference.empty()) return LoadWadPixels(wadReference, pixels);

    fs::path path = fs::path(resolved).is_absolute() ? fs::path(resolved) : gameRootPath / resolved;
    const std::string extension = ToLower(path.extension().string());
    if (extension == ".png" || extension == ".tga") {
        unsigned char* data = nullptr;
        int width = 0;
        int height = 0;
        int channels = 0;
        if (!LoadRasterImage(path.string(), data, width, height, channels)) return false;
        pixels.width = width;
        pixels.height = height;
        pixels.rgba.assign(data, data + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
        FreeRasterImage(data);
        return true;
    }
    if (extension != ".dds") return false;

    const GLuint texture = LoadDDSTexture(path.string());
    if (!texture) return false;
    GLint width = 0;
    GLint height = 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    if (width > 0 && height > 0) {
        pixels.width = width;
        pixels.height = height;
        pixels.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.rgba.data());
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    ReleaseDDSTexture(texture);
    return pixels.width > 0 && pixels.height > 0 && !pixels.rgba.empty() && glGetError() == GL_NO_ERROR;
}

bool GenerateNormalMapDDS(const std::string& source, const std::string& outputPath, const NormalMapSettings& settings, int format) {
    TexturePixels pixels;
    if (!LoadTexturePixels(source, pixels) || pixels.width <= 0 || pixels.height <= 0) return false;
    ApplyCreatorSharpness(pixels, settings.sharpness);
    std::vector<std::uint8_t> height;
    BuildHeight(pixels, settings.heightChannel, settings.invertHeight, height);
    ApplyHeightRange(height, settings.blackPoint, settings.whitePoint);
    SmoothHeight(height, pixels.width, pixels.height, settings.smoothing, settings.tileEdges);
    std::vector<std::vector<std::uint8_t>> mipData;
    std::vector<std::vector<std::uint8_t>> rgbaMips;
    std::vector<std::pair<int, int>> sizes;
    int width = pixels.width;
    int heightSize = pixels.height;
    std::vector<std::uint8_t> currentHeight = std::move(height);
    while (true) {
        std::vector<std::uint8_t> normalX(static_cast<std::size_t>(width) * static_cast<std::size_t>(heightSize));
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(heightSize) * 4u);
        BuildNormalRGBA(currentHeight, width, heightSize, settings, rgba);
        for (std::size_t i = 0; i < normalX.size(); ++i) normalX[i] = rgba[i * 4u];
        std::vector<std::uint8_t> normalY(normalX.size());
        for (std::size_t i = 0; i < normalY.size(); ++i) normalY[i] = rgba[i * 4u + 1u];
        if (format == 1) {
            std::vector<std::uint8_t> blocks;
            EncodeBC5(normalX, normalY, width, heightSize, blocks);
            mipData.push_back(std::move(blocks));
        } else {
            rgbaMips.push_back(std::move(rgba));
        }
        sizes.emplace_back(width, heightSize);
        if (!settings.mipmaps || (width == 1 && heightSize == 1)) break;
        std::vector<std::uint8_t> nextHeight;
        int nextWidth = 1;
        int nextHeightSize = 1;
        DownsampleGray(currentHeight, width, heightSize, nextHeight, nextWidth, nextHeightSize);
        currentHeight = std::move(nextHeight);
        width = nextWidth;
        heightSize = nextHeightSize;
    }

    if (format == 2) {
        if (!CompressBC7(rgbaMips, sizes, mipData)) return false;
    } else if (format != 1) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(fs::path(outputPath).parent_path(), ec);
    return WriteDDS(outputPath, pixels.width, pixels.height, mipData, format == 2 ? DXGI_FORMAT_BC7_UNORM : DXGI_FORMAT_BC5_UNORM, 16, format == 1 ? FourCC('A', 'T', 'I', '2') : 0u);
}

bool GenerateGlossMapDDS(const std::string& source, const std::string& outputPath, const GlossMapSettings& settings, int format) {
    TexturePixels pixels;
    if (!LoadTexturePixels(source, pixels) || pixels.width <= 0 || pixels.height <= 0) return false;
    ApplyCreatorSharpness(pixels, settings.sharpness);
    std::vector<std::uint8_t> current;
    BuildGlossPixels(pixels, settings, current);

    std::vector<std::vector<std::uint8_t>> mipData;
    std::vector<std::vector<std::uint8_t>> rgbaMips;
    std::vector<std::pair<int, int>> sizes;
    int width = pixels.width;
    int height = pixels.height;
    while (true) {
        if (format == 2) {
            std::vector<std::uint8_t> rgba(current.size() * 4u);
            for (std::size_t i = 0; i < current.size(); ++i) {
                rgba[i * 4u + 0u] = current[i];
                rgba[i * 4u + 1u] = current[i];
                rgba[i * 4u + 2u] = current[i];
                rgba[i * 4u + 3u] = 255;
            }
            rgbaMips.push_back(std::move(rgba));
        } else if (format == 0) {
            std::vector<std::uint8_t> blocks;
            EncodeBC4(current, width, height, blocks);
            mipData.push_back(std::move(blocks));
        } else {
            return false;
        }
        sizes.emplace_back(width, height);
        if (!settings.mipmaps || (width == 1 && height == 1)) break;
        if (format == 2) {
            std::vector<std::uint8_t> next;
            int nextWidth = 1;
            int nextHeight = 1;
            DownsampleRGBA(rgbaMips.back(), width, height, next, nextWidth, nextHeight);
            current.resize(static_cast<std::size_t>(nextWidth) * static_cast<std::size_t>(nextHeight));
            for (std::size_t i = 0; i < current.size(); ++i) current[i] = next[i * 4u];
            width = nextWidth;
            height = nextHeight;
        } else {
            std::vector<std::uint8_t> next;
            int nextWidth = 1;
            int nextHeight = 1;
            DownsampleGray(current, width, height, next, nextWidth, nextHeight);
            current = std::move(next);
            width = nextWidth;
            height = nextHeight;
        }
    }
    if (format == 2) {
        if (!CompressBC7(rgbaMips, sizes, mipData)) return false;
    }
    std::error_code ec;
    fs::create_directories(fs::path(outputPath).parent_path(), ec);
    return WriteDDS(outputPath, pixels.width, pixels.height, mipData, format == 2 ? DXGI_FORMAT_BC7_UNORM : DXGI_FORMAT_BC4_UNORM, format == 2 ? 16 : 8, format == 0 ? FourCC('A', 'T', 'I', '1') : 0u);
}


bool GenerateBumpMapDDS(const std::string& source, const std::string& outputPath, const BumpMapSettings& settings, int format) {
    TexturePixels pixels;
    if (!LoadTexturePixels(source, pixels) || pixels.width <= 0 || pixels.height <= 0) return false;
    ApplyCreatorSharpness(pixels, settings.sharpness);

    std::vector<std::uint8_t> current;
    BuildHeight(pixels, settings.heightChannel, settings.invert, current);
    ApplyHeightRange(current, settings.blackPoint, settings.whitePoint, settings.gamma);
    SmoothHeight(current, pixels.width, pixels.height, settings.smoothing, settings.tileEdges);
    ApplyBumpHeightSettings(current, settings.contrast, settings.brightness, settings.normalize);

    std::vector<std::vector<std::uint8_t>> mipData;
    std::vector<std::vector<std::uint8_t>> rgbaMips;
    std::vector<std::pair<int, int>> sizes;
    int width = pixels.width;
    int height = pixels.height;
    while (true) {
        if (format == 2) {
            std::vector<std::uint8_t> rgba(current.size() * 4u);
            for (std::size_t i = 0; i < current.size(); ++i) {
                rgba[i * 4u + 0u] = current[i];
                rgba[i * 4u + 1u] = current[i];
                rgba[i * 4u + 2u] = current[i];
                rgba[i * 4u + 3u] = 255;
            }
            rgbaMips.push_back(std::move(rgba));
        } else if (format == 0) {
            std::vector<std::uint8_t> blocks;
            EncodeBC4(current, width, height, blocks);
            mipData.push_back(std::move(blocks));
        } else {
            return false;
        }
        sizes.emplace_back(width, height);
        if (!settings.mipmaps || (width == 1 && height == 1)) break;
        std::vector<std::uint8_t> next;
        int nextWidth = 1;
        int nextHeight = 1;
        DownsampleGray(current, width, height, next, nextWidth, nextHeight);
        current = std::move(next);
        width = nextWidth;
        height = nextHeight;
    }
    if (format == 2 && !CompressBC7(rgbaMips, sizes, mipData)) return false;
    std::error_code ec;
    fs::create_directories(fs::path(outputPath).parent_path(), ec);
    return WriteDDS(outputPath, pixels.width, pixels.height, mipData, format == 2 ? DXGI_FORMAT_BC7_UNORM : DXGI_FORMAT_BC4_UNORM, format == 2 ? 16 : 8, format == 0 ? FourCC('A', 'T', 'I', '1') : 0u);
}
