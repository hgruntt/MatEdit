#pragma once
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <cstdint>
#include <glad/glad.h>

namespace fs = std::filesystem;

extern fs::path gameRootPath;
extern std::vector<std::string> physicalMaterialTypes;

struct Material {
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;
    std::map<std::string, GLuint> textures;

    char diffusePath[256] = "";
    char normalPath[256] = "";
    char glossPath[256] = "";
    char lumaPath[256] = "";
    char bumpPath[256] = "";     
    char detailPath[256] = "";
    char detailScale[64] = "1 1";
    float smoothness = 0.0f;
    float reflectScale = 0.0f;
    float refractScale = 0.0f;
    float aberrationScale = 0.00f;
    float reliefScale = 0.00f;
    float textureScaleX = 1.0f;
    float textureScaleY = 1.0f;
    int swayHeight = 0;
    int matTypeIndex = 0;
    bool diffuseVisible = true;
    bool diffuseWadTransparency = false;
    bool normalVisible = true;
    bool glossVisible = true;
    bool lumaVisible = true;
    bool bumpVisible = true;
    bool detailVisible = true;

    void updateBuffers();
    void syncParams();
    void loadTextures();
    void releaseTextures();
};

struct PhysicalMaterialEntry {
    std::string name;
    std::map<std::string, std::vector<std::string>> multiParams;

    char impactDecal[128] = "";
    char impactPartsBuf[256] = "";
    char impactSoundBuf[512] = "";
    char stepSoundBuf[1024] = "";

    void updateBuffers();
    void syncParams();
};

std::vector<std::string> LoadPhysicalMaterialTypes();
GLuint LoadDDSTexture(const std::string& path);
void ReleaseDDSTexture(GLuint texture);
GLuint LoadTextureReference(const std::string& reference, bool* goldSrcWadTransparency = nullptr);
GLuint LoadDDS_Cubemap(const std::string& path);
void SetTextureFilteringEnabled(bool enabled);

void RefreshMaterialTextureIndex();
bool AutoAssignMaterialTexturesByName(Material& material);
bool LoadAllMaterials(const std::string& path, std::vector<Material>& materials, bool autoAssignTextures = false);
bool SaveAllMaterials(const std::string& path, const std::vector<Material>& materials);
void LoadAllPhysicalMaterials(const std::string& path, std::vector<PhysicalMaterialEntry>& physMats);
void SaveAllPhysicalMaterials(const std::string& path, const std::vector<PhysicalMaterialEntry>& physMats);

struct TexturePreviewInfo {
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
    bool cachedDDS = false;
};

struct WadTexture {
    std::string name;
    int width = 0;
    int height = 0;
    std::uint32_t pixelOffset = 0;
    std::uint32_t paletteOffset = 0;
};

struct WadArchive {
    std::string relativePath;
    std::string displayName;
    std::vector<WadTexture> textures;
    std::uint64_t sourceSize = 0;
    std::uint64_t contentHashFirst = 0;
    std::uint64_t contentHashSecond = 0;
};


struct TextureFormatInfo {
    bool valid = false;
    bool compressed = false;
    bool srgb = false;
    bool bc5 = false;
    int channels = 4;
    bool bc4 = false;
};

TextureFormatInfo GetTextureFormatInfo(GLuint texture);

TexturePreviewInfo LoadTexturePreview(const std::string& reference);
void ReleaseTexturePreview(TexturePreviewInfo& preview);
const std::vector<WadArchive>& GetWadArchives();
bool AddWadArchive(const std::string& relativePath);
void LoadWadArchives(const std::vector<std::string>& paths);
void ScanAndLoadAllWads();
void ClearWadArchives();
std::vector<std::string> GetLoadedWadPaths();
std::string MakeWadTextureReference(const WadArchive& wad, const WadTexture& texture);
std::string ResolveWadTextureReference(const std::string& reference);

struct TexturePixels {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

bool LoadTexturePixels(const std::string& reference, TexturePixels& pixels);

struct NormalMapSettings {
    float strength = 2.0f;
    bool flipX = false;
    bool flipY = true;
    bool fullZRange = false;
    int heightChannel = 0;
    bool invertHeight = false;
    float sharpness = 0.0f;
    bool mipmaps = true;
    float blackPoint = 0.0f;
    float whitePoint = 1.0f;
    float smoothing = 0.0f;
    int gradientFilter = 0;
    bool tileEdges = false;
};

struct GlossMapSettings {
    float contrast = 1.0f;
    float brightness = 0.0f;
    float power = 1.0f;
    bool invert = false;
    float lowerThreshold = 0.0f;
    float upperThreshold = 1.0f;
    bool normalize = true;
    float sharpness = 0.0f;
    bool mipmaps = true;
    int sourceMode = 0;
    float softness = 0.0f;
};

struct BumpMapSettings {
    int heightChannel = 1;
    bool invert = false;
    float contrast = 1.0f;
    float brightness = 0.0f;
    bool normalize = false;
    float sharpness = 0.0f;
    bool mipmaps = true;
    float blackPoint = 0.0f;
    float whitePoint = 1.0f;
    float gamma = 1.0f;
    float smoothing = 0.0f;
    bool tileEdges = false;
};

TexturePreviewInfo GenerateNormalMapPreviewTexture(const std::string& source, const NormalMapSettings& settings);
TexturePreviewInfo GenerateGlossMapPreviewTexture(const std::string& source, const GlossMapSettings& settings);
TexturePreviewInfo GenerateBumpMapPreviewTexture(const std::string& source, const BumpMapSettings& settings);
bool GenerateNormalMapDDS(const std::string& source, const std::string& outputPath, const NormalMapSettings& settings, int format);
bool GenerateGlossMapDDS(const std::string& source, const std::string& outputPath, const GlossMapSettings& settings, int format);
bool GenerateBumpMapDDS(const std::string& source, const std::string& outputPath, const BumpMapSettings& settings, int format);
