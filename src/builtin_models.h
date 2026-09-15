#pragma once

#include <initializer_list>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct MeshData;

struct BuiltinBezierPatch {
    glm::vec3 p[4][4];
};

static void builtinAddVertex(MeshData& mesh, const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv) {
    mesh.vertices.push_back(p.x);
    mesh.vertices.push_back(p.y);
    mesh.vertices.push_back(p.z);
    mesh.vertices.push_back(n.x);
    mesh.vertices.push_back(n.y);
    mesh.vertices.push_back(n.z);
    mesh.vertices.push_back(uv.x);
    mesh.vertices.push_back(uv.y);
}

static void builtinAddTri(MeshData& mesh, unsigned int a, unsigned int b, unsigned int c) {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
}

static glm::vec3 builtinBezier(const BuiltinBezierPatch& patch, float u, float v) {
    const float bu[4] = {
        (1.0f-u)*(1.0f-u)*(1.0f-u),
        3.0f*u*(1.0f-u)*(1.0f-u),
        3.0f*u*u*(1.0f-u),
        u*u*u
    };
    const float bv[4] = {
        (1.0f-v)*(1.0f-v)*(1.0f-v),
        3.0f*v*(1.0f-v)*(1.0f-v),
        3.0f*v*v*(1.0f-v),
        v*v*v
    };
    glm::vec3 p(0.0f);
    for (int i=0;i<4;++i)
        for (int j=0;j<4;++j)
            p += patch.p[i][j] * bu[i] * bv[j];
    return p;
}

static glm::vec3 builtinBezierDu(const BuiltinBezierPatch& patch, float u, float v) {
    const float bu[3] = {
        3.0f*(1.0f-u)*(1.0f-u),
        6.0f*u*(1.0f-u),
        3.0f*u*u
    };
    const float bv[4] = {
        (1.0f-v)*(1.0f-v)*(1.0f-v),
        3.0f*v*(1.0f-v)*(1.0f-v),
        3.0f*v*v*(1.0f-v),
        v*v*v
    };
    glm::vec3 d(0.0f);
    for (int i=0;i<3;++i)
        for (int j=0;j<4;++j)
            d += (patch.p[i+1][j]-patch.p[i][j]) * bu[i] * bv[j];
    return d;
}

static glm::vec3 builtinBezierDv(const BuiltinBezierPatch& patch, float u, float v) {
    const float bu[4] = {
        (1.0f-u)*(1.0f-u)*(1.0f-u),
        3.0f*u*(1.0f-u)*(1.0f-u),
        3.0f*u*u*(1.0f-u),
        u*u*u
    };
    const float bv[3] = {
        3.0f*(1.0f-v)*(1.0f-v),
        6.0f*v*(1.0f-v),
        3.0f*v*v
    };
    glm::vec3 d(0.0f);
    for (int i=0;i<4;++i)
        for (int j=0;j<3;++j)
            d += (patch.p[i][j+1]-patch.p[i][j]) * bu[i] * bv[j];
    return d;
}

static void builtinAppendPatch(MeshData& mesh, const BuiltinBezierPatch& patch, int resolution, float normalSign = 1.0f) {
    const unsigned int base = static_cast<unsigned int>(mesh.vertices.size() / 8);
    for (int y=0;y<=resolution;++y) {
        const float v = static_cast<float>(y) / resolution;
        for (int x=0;x<=resolution;++x) {
            const float u = static_cast<float>(x) / resolution;
            const glm::vec3 p = builtinBezier(patch,u,v);
            glm::vec3 n = glm::normalize(glm::cross(builtinBezierDu(patch,u,v), builtinBezierDv(patch,u,v))) * normalSign;
            builtinAddVertex(mesh,p,n,glm::vec2(u,v));
        }
    }
    const int row = resolution + 1;
    for (int y=0;y<resolution;++y) {
        for (int x=0;x<resolution;++x) {
            const unsigned int a = base + static_cast<unsigned int>(y*row+x);
            const unsigned int b = a + 1;
            const unsigned int c = a + static_cast<unsigned int>(row);
            const unsigned int d = c + 1;
            builtinAddTri(mesh,a,c,b);
            builtinAddTri(mesh,b,c,d);
        }
    }
}

static BuiltinBezierPatch builtinPatch(std::initializer_list<glm::vec3> values) {
    BuiltinBezierPatch p{};
    auto it = values.begin();
    for (int i=0;i<4;++i)
        for (int j=0;j<4;++j,++it)
            p.p[i][j] = *it;
    return p;
}

static BuiltinBezierPatch builtinRotatePatch(const BuiltinBezierPatch& src, int quarterTurns) {
    BuiltinBezierPatch out{};
    const float angle = glm::half_pi<float>() * static_cast<float>(quarterTurns);
    const glm::mat4 r = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0,1,0));
    for (int i=0;i<4;++i)
        for (int j=0;j<4;++j)
            out.p[i][j] = glm::vec3(r * glm::vec4(src.p[i][j],1.0f));
    return out;
}

static BuiltinBezierPatch builtinMirrorZ(const BuiltinBezierPatch& src) {
    BuiltinBezierPatch out = src;
    for (int i=0;i<4;++i)
        for (int j=0;j<4;++j)
            out.p[i][j].z = -out.p[i][j].z;
    return out;
}

static MeshData makeNewellTeapot() {
    const BuiltinBezierPatch bodyTop = builtinPatch({
        {1.4f,2.25f,0.0f},{1.3375f,2.38125f,0.0f},{1.4375f,2.38125f,0.0f},{1.5f,2.25f,0.0f},
        {1.4f,2.25f,0.784f},{1.3375f,2.38125f,0.749f},{1.4375f,2.38125f,0.805f},{1.5f,2.25f,0.84f},
        {0.784f,2.25f,1.4f},{0.749f,2.38125f,1.3375f},{0.805f,2.38125f,1.4375f},{0.84f,2.25f,1.5f},
        {0.0f,2.25f,1.4f},{0.0f,2.38125f,1.3375f},{0.0f,2.38125f,1.4375f},{0.0f,2.25f,1.5f}
    });

    const BuiltinBezierPatch bodyMid = builtinPatch({
        {1.5f,2.25f,0.0f},{1.75f,1.725f,0.0f},{2.0f,1.2f,0.0f},{2.0f,0.75f,0.0f},
        {1.5f,2.25f,0.84f},{1.75f,1.725f,0.98f},{2.0f,1.2f,1.12f},{2.0f,0.75f,1.12f},
        {0.84f,2.25f,1.5f},{0.98f,1.725f,1.75f},{1.12f,1.2f,2.0f},{1.12f,0.75f,2.0f},
        {0.0f,2.25f,1.5f},{0.0f,1.725f,1.75f},{0.0f,1.2f,2.0f},{0.0f,0.75f,2.0f}
    });

    const BuiltinBezierPatch bodyBottom = builtinPatch({
        {2.0f,0.75f,0.0f},{2.0f,0.3f,0.0f},{1.5f,0.075f,0.0f},{1.5f,0.0f,0.0f},
        {2.0f,0.75f,1.12f},{2.0f,0.3f,1.12f},{1.5f,0.075f,0.84f},{1.5f,0.0f,0.84f},
        {1.12f,0.75f,2.0f},{1.12f,0.3f,2.0f},{0.84f,0.075f,1.5f},{0.84f,0.0f,1.5f},
        {0.0f,0.75f,2.0f},{0.0f,0.3f,2.0f},{0.0f,0.075f,1.5f},{0.0f,0.0f,1.5f}
    });

    const BuiltinBezierPatch handleUpper = builtinPatch({
        {-1.6f,1.875f,0.0f},{-2.3f,1.875f,0.0f},{-2.7f,1.875f,0.0f},{-2.7f,1.65f,0.0f},
        {-1.6f,1.875f,0.3f},{-2.3f,1.875f,0.3f},{-2.7f,1.875f,0.3f},{-2.7f,1.65f,0.3f},
        {-1.5f,2.1f,0.3f},{-2.5f,2.1f,0.3f},{-3.0f,2.1f,0.3f},{-3.0f,1.65f,0.3f},
        {-1.5f,2.1f,0.0f},{-2.5f,2.1f,0.0f},{-3.0f,2.1f,0.0f},{-3.0f,1.65f,0.0f}
    });

    const BuiltinBezierPatch handleLower = builtinPatch({
        {-2.7f,1.65f,0.0f},{-2.7f,1.425f,0.0f},{-2.5f,0.975f,0.0f},{-2.0f,0.75f,0.0f},
        {-2.7f,1.65f,0.3f},{-2.7f,1.425f,0.3f},{-2.5f,0.975f,0.3f},{-2.0f,0.75f,0.3f},
        {-3.0f,1.65f,0.3f},{-3.0f,1.2f,0.3f},{-2.65f,0.7875f,0.3f},{-1.9f,0.45f,0.3f},
        {-3.0f,1.65f,0.0f},{-3.0f,1.2f,0.0f},{-2.65f,0.7875f,0.0f},{-1.9f,0.45f,0.0f}
    });

    const BuiltinBezierPatch spoutUpper = builtinPatch({
        {1.7f,1.275f,0.0f},{2.6f,1.275f,0.0f},{2.3f,1.95f,0.0f},{2.7f,2.25f,0.0f},
        {1.7f,1.275f,0.66f},{2.6f,1.275f,0.66f},{2.3f,1.95f,0.25f},{2.7f,2.25f,0.25f},
        {1.7f,0.45f,0.66f},{3.1f,0.675f,0.66f},{2.4f,1.875f,0.25f},{3.3f,2.25f,0.25f},
        {1.7f,0.45f,0.0f},{3.1f,0.675f,0.0f},{2.4f,1.875f,0.0f},{3.3f,2.25f,0.0f}
    });

    const BuiltinBezierPatch spoutTip = builtinPatch({
        {2.7f,2.25f,0.0f},{2.8f,2.325f,0.0f},{2.9f,2.325f,0.0f},{2.8f,2.25f,0.0f},
        {2.7f,2.25f,0.25f},{2.8f,2.325f,0.25f},{2.9f,2.325f,0.15f},{2.8f,2.25f,0.15f},
        {3.3f,2.25f,0.25f},{3.525f,2.34375f,0.25f},{3.45f,2.3625f,0.15f},{3.2f,2.25f,0.15f},
        {3.3f,2.25f,0.0f},{3.525f,2.34375f,0.0f},{3.45f,2.3625f,0.0f},{3.2f,2.25f,0.0f}
    });

    const BuiltinBezierPatch lidTop = builtinPatch({
        {0.01f,3.0f,0.0f},{0.8f,3.0f,0.0f},{0.0f,2.7f,0.0f},{0.2f,2.55f,0.0f},
        {0.0f,3.0f,0.01f},{0.8f,3.0f,0.45f},{0.0f,2.7f,0.0f},{0.2f,2.55f,0.112f},
        {0.01f,3.0f,0.0f},{0.45f,3.0f,0.8f},{0.0f,2.7f,0.0f},{0.112f,2.55f,0.2f},
        {0.0f,3.0f,0.01f},{0.0f,3.0f,0.8f},{0.0f,2.7f,0.0f},{0.0f,2.55f,0.2f}
    });

    const BuiltinBezierPatch lidLower = builtinPatch({
        {0.2f,2.55f,0.0f},{0.4f,2.4f,0.0f},{1.3f,2.4f,0.0f},{1.3f,2.25f,0.0f},
        {0.2f,2.55f,0.112f},{0.4f,2.4f,0.224f},{1.3f,2.4f,0.728f},{1.3f,2.25f,0.728f},
        {0.112f,2.55f,0.2f},{0.224f,2.4f,0.4f},{0.728f,2.4f,1.3f},{0.728f,2.25f,1.3f},
        {0.0f,2.55f,0.2f},{0.0f,2.4f,0.4f},{0.0f,2.4f,1.3f},{0.0f,2.25f,1.3f}
    });

    MeshData mesh;
    constexpr int resolution = 18;

    for (int q=0;q<4;++q) {
        builtinAppendPatch(mesh,builtinRotatePatch(bodyTop,q),resolution,1.0f);
        builtinAppendPatch(mesh,builtinRotatePatch(bodyMid,q),resolution,1.0f);
        builtinAppendPatch(mesh,builtinRotatePatch(bodyBottom,q),resolution,1.0f);
        builtinAppendPatch(mesh,builtinRotatePatch(lidTop,q),resolution,1.0f);
        builtinAppendPatch(mesh,builtinRotatePatch(lidLower,q),resolution,1.0f);
    }

    builtinAppendPatch(mesh,handleUpper,resolution,1.0f);
    builtinAppendPatch(mesh,builtinMirrorZ(handleUpper),resolution,-1.0f);
    builtinAppendPatch(mesh,handleLower,resolution,1.0f);
    builtinAppendPatch(mesh,builtinMirrorZ(handleLower),resolution,-1.0f);
    builtinAppendPatch(mesh,spoutUpper,resolution,1.0f);
    builtinAppendPatch(mesh,builtinMirrorZ(spoutUpper),resolution,-1.0f);
    builtinAppendPatch(mesh,spoutTip,resolution,1.0f);
    builtinAppendPatch(mesh,builtinMirrorZ(spoutTip),resolution,-1.0f);

    const float scale = 0.48f;
    for (std::size_t i=0;i<mesh.vertices.size();i+=8) {
        mesh.vertices[i] *= scale;
        mesh.vertices[i+1] = (mesh.vertices[i+1] - 1.5f) * scale;
        mesh.vertices[i+2] *= scale;
    }
    return mesh;
}
