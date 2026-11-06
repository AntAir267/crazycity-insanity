// .pkg model packages (spec/formats/pkg.md): meshes, material sets, attached props.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bytes.h"
#include "core/math.h"

namespace cc {

struct Vertex {
    vec3 pos;
    vec3 normal;
    vec2 uv;
    uint8_t color[4] = {255, 255, 255, 255};  // R, G, B, A
};

struct Batch {
    int material = 0;  // index into the selected material set (low byte of the stored word)
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;  // triangle list
};

// Vertex format bits (Direct3D 7 FVF values).
enum : uint32_t {
    kFvfPositionMask = 0x00F,
    kFvfXYZ = 0x002,
    kFvfNormal = 0x010,
    kFvfReserved = 0x020,
    kFvfDiffuse = 0x040,
    kFvfSpecular = 0x080,
    kFvfTex1 = 0x100,
    kFvfTex2 = 0x200,
};

struct Mesh {
    std::string name;
    uint32_t fvf = 0;
    std::vector<Batch> batches;
    vec3 boundsMin, boundsMax;
    float radius = 0;  // largest distance from the origin to a vertex

    bool hasNormals() const { return (fvf & kFvfNormal) != 0; }
    bool hasColors() const { return (fvf & kFvfDiffuse) != 0; }
    size_t vertexCount() const;
    size_t triangleCount() const;
};

struct Material {
    std::string texture;  // "" = untextured
    vec4 diffuse;
    vec4 ambient;  // = diffuse (the file's value is discarded)
    vec4 specular;
    vec4 emissive;
    float power = 0;  // strength of the environment reflection pass
};

struct Xref {
    float matrix[12];
    std::string name;
};

struct PkgEntry {
    std::string name;
    int mesh = -1;  // index into Pkg::meshes when the entry is a mesh that parsed
};

struct PkgLoadOptions {
    // Multiplier for untextured materials' diffuse colour (0.5 for night cities).
    float untexturedFactor = 1.0f;
    // Keep only the first k material sets (0 = all).
    int maxSets = 0;
};

struct Pkg {
    int version = 3;  // 2 or 3
    std::vector<PkgEntry> entries;  // every entry in file order (meshes, shaders, xrefs, offset)
    std::vector<Mesh> meshes;
    int setCount = 0;
    int materialsPerSet = 0;
    bool compactMaterials = false;
    std::vector<Material> materials;  // set-major
    std::vector<Xref> xrefs;
    bool hasShaders = false;
    bool hasOffset = false;
    vec3 offset;
    std::vector<std::string> warnings;  // problems that did not stop the load

    const Mesh* findMesh(const std::string& name) const;  // case-insensitive
    const Material* material(int set, int index) const;
    int effectiveSet(int set) const { return setCount > 0 ? ((set % setCount) + setCount) % setCount : 0; }
    size_t byteEstimate() const;
};

// Parses a package. False + err when it cannot be used at all.
bool parsePkg(const uint8_t* data, size_t size, Pkg& out, std::string& err,
              const PkgLoadOptions& opt = {});

// A `shaders` entry on its own (a pedestrian's `anim/<name>.shaders`, ped-anim.md "Paint
// variants"): the material sets with the package colour adjustments; nothing else of `out` is set.
bool parseShadersFile(const uint8_t* data, size_t size, Pkg& out, std::string& err, const PkgLoadOptions& opt = {});

}  // namespace cc
