#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

struct Sphere {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    Sphere(int rings = 32, int sectors = 32) {
        float const R = 1.0f / (float)(rings - 1);
        float const S = 1.0f / (float)(sectors - 1);

        for (int r = 0; r < rings; ++r) {
            for (int s = 0; s < sectors; ++s) {
                float y = sin(-glm::pi<float>() / 2.0f + glm::pi<float>() * r * R);
                float x = cos(2.0f * glm::pi<float>() * s * S) * sin(glm::pi<float>() * r * R);
                float z = sin(2.0f * glm::pi<float>() * s * S) * sin(glm::pi<float>() * r * R);

                glm::vec3 pos(x, y, z);
                glm::vec3 norm = glm::normalize(pos);
                vertices.push_back(pos.x); vertices.push_back(pos.y); vertices.push_back(pos.z); // pos
                vertices.push_back(norm.x); vertices.push_back(norm.y); vertices.push_back(norm.z); // normal
                vertices.push_back(s * S); vertices.push_back(r * R);               // uv
            }
        }

        for (int r = 0; r < rings - 1; ++r) {
            for (int s = 0; s < sectors - 1; ++s) {
                indices.push_back(r * sectors + s);
                indices.push_back(r * sectors + (s + 1));
                indices.push_back((r + 1) * sectors + (s + 1));
                indices.push_back((r + 1) * sectors + (s + 1));
                indices.push_back((r + 1) * sectors + s);
                indices.push_back(r * sectors + s);
            }
        }
    }
};

