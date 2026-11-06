#include "render/mesh_cache.h"

#include <cstddef>
#include <cstring>

#include "formats/pkg.h"

namespace cc {

namespace {
struct GpuVertex {
    float pos[3];
    float normal[3];
    float uv[2];
    uint8_t color[4];
};
static_assert(sizeof(GpuVertex) == 36, "vertex layout");
}  // namespace

namespace {
void fillBuffers(GpuMesh* g, const Mesh& mesh, GLenum usage);
}

GpuMesh* MeshCache::upload(const Mesh& mesh) {
    auto* g = new GpuMesh();
    fillBuffers(g, mesh, GL_STATIC_DRAW);
    return g;
}

void MeshCache::stream(GpuMesh*& g, const Mesh& mesh) {
    if (!g) g = new GpuMesh();
    fillBuffers(g, mesh, GL_STREAM_DRAW);
}

namespace {
void fillBuffers(GpuMesh* g, const Mesh& mesh, GLenum usage) {
    g->ranges.clear();
    g->hasNormals = mesh.hasNormals();
    g->hasColors = mesh.hasColors();
    g->boundsMin = mesh.boundsMin;
    g->boundsMax = mesh.boundsMax;
    g->radius = mesh.radius;
    std::vector<GpuVertex> verts;
    std::vector<uint32_t> idx;
    verts.reserve(mesh.vertexCount());
    for (const Batch& b : mesh.batches) {
        const uint32_t base = uint32_t(verts.size());
        GpuMesh::Range r;
        r.material = b.material;
        r.firstIndex = GLsizei(idx.size());
        r.count = GLsizei(b.indices.size());
        for (const Vertex& v : b.vertices) {
            GpuVertex gv;
            gv.pos[0] = v.pos.x; gv.pos[1] = v.pos.y; gv.pos[2] = v.pos.z;
            gv.normal[0] = v.normal.x; gv.normal[1] = v.normal.y; gv.normal[2] = v.normal.z;
            gv.uv[0] = v.uv.x; gv.uv[1] = v.uv.y;
            std::memcpy(gv.color, v.color, 4);
            verts.push_back(gv);
        }
        for (uint16_t i : b.indices) idx.push_back(base + i);
        g->ranges.push_back(r);
    }
    const bool fresh = g->vao == 0;
    if (fresh) {
        glGenVertexArrays(1, &g->vao);
        glGenBuffers(1, &g->vbo);
        glGenBuffers(1, &g->ibo);
    }
    glBindVertexArray(g->vao);
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(GpuVertex)), verts.data(), usage);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * 4), idx.data(), usage);
    if (!fresh) {
        glBindVertexArray(0);
        return;
    }
    const GLsizei stride = sizeof(GpuVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(GpuVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(GpuVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(GpuVertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(GpuVertex, color)));
    glBindVertexArray(0);
}
}  // namespace

void MeshCache::destroy(GpuMesh* g) {
    if (!g) return;
    glDeleteVertexArrays(1, &g->vao);
    glDeleteBuffers(1, &g->vbo);
    glDeleteBuffers(1, &g->ibo);
    delete g;
}

const GpuMesh* MeshCache::get(const Mesh& mesh) {
    auto it = cache_.find(&mesh);
    if (it != cache_.end()) return it->second;
    GpuMesh* g = upload(mesh);
    cache_.emplace(&mesh, g);
    return g;
}

void MeshCache::release(const Mesh& mesh) {
    auto it = cache_.find(&mesh);
    if (it == cache_.end()) return;
    destroy(it->second);
    cache_.erase(it);
}

void MeshCache::clear() {
    for (auto& kv : cache_) destroy(kv.second);
    cache_.clear();
}

}  // namespace cc
