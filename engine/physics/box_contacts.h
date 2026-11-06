// Box against box (spec/physics/car-collision-boxes.md): the contacts between two moving box
// bounds (computer car or traffic car against another).
#pragma once

#include <vector>

#include "physics/contacts.h"

namespace cc {
namespace phys {

struct BoxShape {
    vec3 size;    // S
    vec3 offset;  // c: the box centre in the body's coordinates
};

struct BoxImpact {
    Impact im;          // normal out of the second side into the first
    bool aFirst = true; // false: filed with B as the first side (pass 2)
};

// A = the earlier mover, B = the later one; MA0/MB0 the previous matrices (already shifted by a
// last push where car-collision.md 3.3 says so). Materials are filled by the caller.
void boxBoxContacts(const BoxShape& A, const Mtx& MA, const Mtx& MA0, const BoxShape& B, const Mtx& MB, const Mtx& MB0,
                    float dt, std::vector<BoxImpact>& out);

}  // namespace phys
}  // namespace cc
