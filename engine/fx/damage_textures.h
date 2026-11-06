// Texture damage of a car body (spec/formats/damage-visuals.md, "Texture damage"): the damageable
// material slots of the car's paint set, one working copy per slot (top level only), the BODY_H
// triangles, and the marks a counted impact uncovers. CPU side only; the renderer uploads the
// working copies (render/fx_draw.h).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/math.h"
#include "formats/texture_lookup.h"

namespace cc {

struct VehicleModel;

namespace fx {

class ImageSource;

struct DamageSlot {
    int material = 0;              // material index in the paint set
    std::string cleanName, damageName;
    std::shared_ptr<const TextureAsset> clean, damage;
    int w = 0, h = 0;              // the working copy's size (the clean image's top level)
    int dw = 0, dh = 0;            // the damage image's top level
    bool hasAlpha = false, clampU = false, clampV = false;
    uint32_t texFlags = 0;
    // Allocated on first use: RGBA texels, and 1 where the damage image is uncovered.
    std::vector<uint8_t> work, mask;
    // Changed texels since the renderer last took them (x0 > x1: none).
    int x0 = 1, y0 = 1, x1 = 0, y1 = 0;
};

class DamageTextures {
public:
    // Set-up when the car loads: BODY_H (after the fill rule) of `model` with paint set `set`.
    // False when no slot is damageable (the car then has no texture damage).
    bool setup(const VehicleModel& model, int set, ImageSource& images);
    bool active() const { return !slots_.empty(); }

    // One round of marks at car-space point p with radius r; draws from the effects generator.
    // Returns the number of triangles marked.
    int markRound(const vec3& p, float r);
    // Clear damage: every working copy back to its clean image.
    void repair();
    // Follows the image source's condition (night/rain textures): reloads the images and rebuilds
    // the working copies, keeping the marks.
    void sync();

    std::vector<DamageSlot>& slots() { return slots_; }
    const std::vector<DamageSlot>& slots() const { return slots_; }
    // Slot index of a material index, -1 if that material is not damageable.
    int slotOf(int material) const {
        return material >= 0 && size_t(material) < slotOfMaterial_.size() ? slotOfMaterial_[size_t(material)] : -1;
    }
    // Makes sure the slot's working copy exists (filled from the clean image and the marks).
    void ensureWork(DamageSlot& s);
    bool anyMarks() const { return marked_; }
    unsigned version() const { return version_; }

    // Survey numbers (damage-visuals.md, "Checked against").
    size_t triangleCount() const { return tris_.size(); }
    size_t damageableTriangles() const;

    // The renderer's GPU copies (type known only to the renderer).
    std::shared_ptr<void> gpu;

private:
    struct Tri {
        uint32_t v[3];
        int slot;  // index into slots_, -1 = not damageable
    };
    void loadImages(DamageSlot& s);
    void fillFromMask(DamageSlot& s);
    void stamp(DamageSlot& s, int x, int y, bool large);

    ImageSource* images_ = nullptr;
    unsigned generation_ = 0;
    std::vector<vec3> pos_;
    std::vector<vec2> uv_;
    std::vector<Tri> tris_;
    std::vector<DamageSlot> slots_;
    std::vector<int> slotOfMaterial_;
    bool marked_ = false;
    unsigned version_ = 0;
};

// The damageable-slot rule for one material texture name (damage-visuals.md, "The data"): the
// clean and damage image names, or false.
bool damageSlotNames(const std::string& texture, ImageSource& images, std::string& clean, std::string& damage);

}  // namespace fx
}  // namespace cc
