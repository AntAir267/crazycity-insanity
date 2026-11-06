#include "model/light_bar.h"

#include <algorithm>
#include <cmath>

#include "core/game_random.h"
#include "core/strutil.h"
#include "formats/pkg.h"
#include "model/vehicle.h"

namespace cc {

namespace {
// x' = s·z + c·x, z' = c·z − s·x (y unchanged).
vec3 turnY(const vec3& d, float a) {
    const float s = float(std::sin(double(a))), c = float(std::cos(double(a)));
    return vec3(s * d.z + c * d.x, d.y, c * d.z - s * d.x);
}
}  // namespace

void LightBar::setup(const VehicleModel& m, int materialSet) {
    lights.clear();
    on = false;
    int k = 0;
    for (int i = 0; i < 4; ++i) {
        const std::string name = strf("SRN%d", i);
        const PartLods* part = m.part(name);
        const Pivot* pv = m.pivotOf(name);
        if (!part || !part->any()) continue;
        SirenLight l;
        l.pos = pv && pv->found ? pv->origin : vec3();
        l.color = vec3(1, 1, 1);
        const Mesh* h = part->at(LodH);
        if (h && !h->batches.empty() && m.pkg)
            if (const Material* mat = m.pkg->material(materialSet, h->batches[0].material))
                l.color = vec3(mat->diffuse.x, mat->diffuse.y, mat->diffuse.z);
        l.beam = turnY(vec3(1, 0, 0), float(k) * 1.5707964f);
        lights.push_back(l);
        ++k;
    }
    // Lens flare elements: 6 draws each from a generator started at x = 1 (engine choice).
    Lcg g(1);
    for (FlareElement& e : flare) {
        const float r1 = g.unit(), r2 = g.unit(), r3 = g.unit(), r4 = g.unit(), r5 = g.unit(), r6 = g.unit();
        e.c = vec3((r2 + 1.0f) * 0.5f, (r1 + 1.0f) * 0.5f, (r3 + 1.0f) * 0.5f);
        e.p = (r4 * 1.5f) * (r4 * 1.5f);
        e.b = std::min(0.25f / e.p, 1.0f);
        e.z = std::sqrt(e.p) * 0.1f;
        e.m = r5 * 0.5f + 1.5f;
        if (r6 < 0.5f) e.p = -e.p;
    }
    flare[0].p = 1.0f;
    flare[0].z = 0.3f;
    flare[1].p = -1.0f;
    flare[1].z = 0.25f;
}

void LightBar::update(float F, const vec3& carForward) {
    if (!on) {
        head[0] = head[1] = carForward;
        return;
    }
    const float a = (F * 2.5f) * 3.1415927f;
    for (SirenLight& l : lights) l.beam = turnY(l.beam, a);
    head[0] = turnY(head[0], F * 42.411503f);
    head[1] = turnY(head[1], -(F * 42.411503f));
}

}  // namespace cc
