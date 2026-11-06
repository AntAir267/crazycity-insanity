#include "net/car_state.h"

#include <cmath>

#include "net/wire.h"
#include "physics/car.h"

namespace cc {
namespace net {

namespace {
int8_t truncByte127(float x) {
    const float t = std::trunc(x * 127.0f);
    if (!(t == t)) return 0;  // NaN
    if (t > 127.0f) return 127;
    if (t < -128.0f) return -128;
    return int8_t(t);
}
uint8_t truncByte255(float x) {
    const float t = std::trunc(x * 255.0f);
    if (!(t > 0.0f)) return 0;
    if (t > 255.0f) return 255;
    return uint8_t(t);
}
}  // namespace

void encodeDirection(const vec3& v, float& len, int8_t dir[3]) {
    const float l2 = (v.x * v.x + v.y * v.y) + v.z * v.z;
    len = std::sqrt(l2);
    if (!(len > 0.0f)) {
        // Zero: MM2 sends whatever 0/0 gives; we send 0 (3.1).
        len = 0.0f;
        dir[0] = dir[1] = dir[2] = 0;
        return;
    }
    dir[0] = truncByte127(v.x / len);
    dir[1] = truncByte127(v.y / len);
    dir[2] = truncByte127(v.z / len);
}

vec3 decodeDirection(float len, const int8_t dir[3]) {
    return vec3((len * float(dir[0])) / 127.0f, (len * float(dir[1])) / 127.0f, (len * float(dir[2])) / 127.0f);
}

void anglesOf(const phys::M3& R, float& a, float& b, float& c) {
    const vec3& X = R.r[0];
    const vec3& Y = R.r[1];
    const vec3& Z = R.r[2];
    a = float(std::atan2(double(Y.z), double(Z.z)));
    double s = -double(X.z);
    if (s > 1.0) s = 1.0;
    if (s < -1.0) s = -1.0;
    b = float(std::asin(s));
    c = float(std::atan2(double(X.y), double(X.x)));
}

phys::M3 matrixFromAngles(float a, float b, float c) {
    const float sa = phys::fsin(a), ca = phys::fcos(a);
    const float sb = phys::fsin(b), cb = phys::fcos(b);
    const float sc = phys::fsin(c), cc_ = phys::fcos(c);
    phys::M3 M;
    M.r[0] = vec3(cc_ * cb, sc * cb, -sb);
    M.r[1] = vec3(cc_ * sb * sa - sc * ca, cc_ * ca + sc * sb * sa, cb * sa);
    M.r[2] = vec3(sc * sa + cc_ * sb * ca, sc * sb * ca - cc_ * sa, cb * ca);
    return M;
}

CarState captureCarState(const phys::Car& car) {
    CarState s;
    const phys::CarInputs& in = car.inputs();
    s.steer = truncByte127(in.steer);
    s.throttle = truncByte255(in.throttle);
    s.brake = truncByte255(in.brake);
    s.gear = int8_t(car.gearIndex());
    anglesOf(car.rotation(), s.a, s.b, s.c);
    s.pos = car.body().p;
    encodeDirection(car.body().v, s.accelLen, s.accelDir);  // MM2: the velocity in the acceleration slot
    encodeDirection(car.body().v, s.velLen, s.velDir);
    encodeDirection(car.body().w, s.angLen, s.angDir);
    s.damage = car.damage().damage;
    uint16_t f = 0;
    if (car.gearIndex() == 0) f |= FlagReverse;
    if (in.handbrake > 0.8f) f |= FlagHandbrake;
    if (!car.automatic()) f |= FlagManual;
    if (!car.drivable()) f |= FlagNotDrivable;
    s.flags = f;
    return s;
}

void writeCarState(Writer& w, const CarState& s) {
    w.f32(s.hostClock);
    w.i8(s.steer);
    w.u8(s.throttle);
    w.u8(s.brake);
    w.i8(s.gear);
    w.f32(s.a);
    w.f32(s.b);
    w.f32(s.c);
    w.v3(s.pos);
    w.f32(s.accelLen);
    for (int i = 0; i < 3; ++i) w.i8(s.accelDir[i]);
    w.f32(s.velLen);
    for (int i = 0; i < 3; ++i) w.i8(s.velDir[i]);
    w.f32(s.angLen);
    for (int i = 0; i < 3; ++i) w.i8(s.angDir[i]);
    w.f32(s.damage);
    w.u16(s.score);
    w.u16(s.flags);
    w.u32(s.seq);
}

bool readCarState(Reader& r, CarState& s) {
    s.hostClock = r.f32();
    s.steer = r.i8();
    s.throttle = r.u8();
    s.brake = r.u8();
    s.gear = r.i8();
    s.a = r.f32();
    s.b = r.f32();
    s.c = r.f32();
    s.pos = r.v3();
    s.accelLen = r.f32();
    for (int i = 0; i < 3; ++i) s.accelDir[i] = r.i8();
    s.velLen = r.f32();
    for (int i = 0; i < 3; ++i) s.velDir[i] = r.i8();
    s.angLen = r.f32();
    for (int i = 0; i < 3; ++i) s.angDir[i] = r.i8();
    s.damage = r.f32();
    s.score = r.u16();
    s.flags = r.u16();
    s.seq = r.u32();
    return r.ok();
}

}  // namespace net
}  // namespace cc
