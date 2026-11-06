// GPU copies of meshes: one vertex buffer and one index buffer per mesh, one draw range per
// batch. Keyed by the CPU mesh (which lives as long as its package in the asset cache).
#pragma once

#include <unordered_map>
#include <vector>

#include "core/math.h"
#include "render/gl_util.h"

namespace cc {

struct Mesh;

struct GpuMesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    struct Range {
        int material = 0;
        GLsizei count = 0;      // indices
        GLsizei firstIndex = 0;
    };
    std::vector<Range> ranges;  // one per batch, same order
    bool hasNormals = false;
    bool hasColors = false;
    vec3 boundsMin, boundsMax;
    float radius = 0;
};

class MeshCache {
public:
    ~MeshCache() { clear(); }
    const GpuMesh* get(const Mesh& mesh);
    void release(const Mesh& mesh);
    void clear();
    size_t count() const { return cache_.size(); }

    static GpuMesh* upload(const Mesh& mesh);
    static void destroy(GpuMesh* m);
    // A mesh whose vertices change every frame (posed pedestrians): `g` is created on first use and
    // its buffers are respecified (stream draw) on every call.
    static void stream(GpuMesh*& g, const Mesh& mesh);

private:
    std::unordered_map<const Mesh*, GpuMesh*> cache_;
};

}  // namespace cc
