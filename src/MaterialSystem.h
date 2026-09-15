#pragma once
#include <string>
#include <vector>
#include <map>
#include <filesystem>
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
    int swayHeight = 0;
    int matTypeIndex = 0;

    void updateBuffers();
    void syncParams();
    void loadTextures();
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
GLuint LoadDDS_Cubemap(const std::string& path);
GLuint LoadSkyboxAs2D(const std::string& path);

void LoadAllMaterials(const std::string& path, std::vector<Material>& materials);
void SaveAllMaterials(const std::string& path, const std::vector<Material>& materials);
void LoadAllPhysicalMaterials(const std::string& path, std::vector<PhysicalMaterialEntry>& physMats);
void SaveAllPhysicalMaterials(const std::string& path, const std::vector<PhysicalMaterialEntry>& physMats);
