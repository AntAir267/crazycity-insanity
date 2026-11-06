// GPU side of the visual effects: a car's texture damage working copies (damage-visuals.md), and
// the skid marks, particles, sparks and chips of engine/effects.md ("Drawing order").
#pragma once

#include <vector>

#include "core/math.h"

namespace cc {

struct GpuTexture;
class Renderer;
class TextureCache;

namespace fx {
class DamageTextures;
class EffectsWorld;
}

// The car's working copies as GPU textures, indexed by material index (null = the material is
// drawn with its own texture), for ModelDraw::slotTextures. Creates them on first use (top level
// only, no mip chain) and sends only the changed rectangle of each copy after that. Null when the
// car has no texture damage.
const std::vector<const GpuTexture*>* damageSlotTextures(fx::DamageTextures& t);

// The decal pass part: every car's skid marks, in car order, wheel by wheel (depth test and
// writes off).
void drawSkidMarks(Renderer& r, TextureCache& textures, const fx::EffectsWorld& world);
// The effects pass: per car its wheel particles, smoke, sparks and chips, then the active props'
// particles (at most 63 items), then the rain if it is shown this frame. camX/camY = the camera's
// right and up axes (world).
void drawEffectsPass(Renderer& r, TextureCache& textures, const fx::EffectsWorld& world, const vec3& camX,
                     const vec3& camY);

}  // namespace cc
