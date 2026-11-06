#include "drive/drive_tune.h"

#include "core/bytes.h"
#include "formats/dat_text.h"
#include "vfs/vfs.h"

namespace cc {

using dat::FieldType;
using dat::Schema;

namespace {
std::string textOf(const BytesPtr& b) { return b ? std::string(b->begin(), b->end()) : std::string(); }
}  // namespace

SteeringTune loadSteeringTune(const Vfs& vfs, const std::string& baseName) {
    SteeringTune t;
    BytesPtr b = vfs.open("tune/" + baseName + ".asNode");
    if (!b) return t;
    Schema s{{{"SpeedSensitive", FieldType::Int, &t.speedSensitive, nullptr},
              {"SpeedBaseLow", FieldType::Float, &t.speedBaseLow, nullptr},
              {"SpeedBaseHi", FieldType::Float, &t.speedBaseHi, nullptr},
              {"DiscreteSteeringDeltaInLo", FieldType::Float, &t.deltaInLo, nullptr},
              {"DiscreteSteeringDeltaInHi", FieldType::Float, &t.deltaInHi, nullptr},
              {"DiscreteSteeringDeltaOutLo", FieldType::Float, &t.deltaOutLo, nullptr},
              {"DiscreteSteeringDeltaOutHi", FieldType::Float, &t.deltaOutHi, nullptr},
              {"DiscreteSteeringFilterLo", FieldType::Float, &t.filterLo, nullptr},
              {"DiscreteSteeringFilterHi", FieldType::Float, &t.filterHi, nullptr},
              {"JoySensitivityLow", FieldType::Float, &t.joySensitivityLow, nullptr},
              {"JoySensitivityHi", FieldType::Float, &t.joySensitivityHi, nullptr},
              {"JoySteerFilterLow", FieldType::Float, &t.joySteerFilterLow, nullptr},
              {"JoySteerFilterHi", FieldType::Float, &t.joySteerFilterHi, nullptr},
              {"JoyApp", FieldType::Int, &t.joyApp, nullptr},
              {"JoySteerApproachOutLo", FieldType::Float, &t.joyApproachOutLo, nullptr},
              {"JoySteerApproachInLo", FieldType::Float, &t.joyApproachInLo, nullptr},
              {"JoySteerApproachOutHi", FieldType::Float, &t.joyApproachOutHi, nullptr},
              {"JoySteerApproachInHi", FieldType::Float, &t.joyApproachInHi, nullptr},
              {"JoySteerAppApp", FieldType::Float, &t.joyAppApp, nullptr},
              {"WheelApp", FieldType::Int, &t.wheelApp, nullptr},
              {"WheelSteerApproachOutLo", FieldType::Float, &t.wheelApproachOutLo, nullptr},
              {"WheelSteerApproachInLo", FieldType::Float, &t.wheelApproachInLo, nullptr},
              {"WheelSteerApproachOutHi", FieldType::Float, &t.wheelApproachOutHi, nullptr},
              {"WheelSteerApproachInHi", FieldType::Float, &t.wheelApproachInHi, nullptr},
              {"WheelSteerAppApp", FieldType::Float, &t.wheelAppApp, nullptr},
              {"WheelSensitivityLow", FieldType::Float, &t.wheelSensitivityLow, nullptr},
              {"WheelSensitivityHi", FieldType::Float, &t.wheelSensitivityHi, nullptr},
              {"WheelSteerFilterLow", FieldType::Float, &t.wheelSteerFilterLow, nullptr},
              {"WheelSteerFilterHi", FieldType::Float, &t.wheelSteerFilterHi, nullptr},
              {"MouseSensitivityLow", FieldType::Float, &t.mouseSensitivityLow, nullptr},
              {"MouseSensitivityHi", FieldType::Float, &t.mouseSensitivityHi, nullptr},
              {"MouseSteerFilterLow", FieldType::Float, &t.mouseSteerFilterLow, nullptr},
              {"MouseSteerFilterHi", FieldType::Float, &t.mouseSteerFilterHi, nullptr}}};
    t.fileRead = dat::parseText(textOf(b), s);
    return t;
}

namespace {

bool readChase(const Vfs& vfs, const std::string& path, ChaseTune& t) {
    BytesPtr b = vfs.open(path);
    if (!b) return false;
    Schema s{{{"Offset", FieldType::Vector, &t.offset, nullptr},
              {"TrackTo", FieldType::Vector, &t.trackTo, nullptr},
              {"CollideType", FieldType::Int, &t.collideType, nullptr},
              {"MinMaxOn", FieldType::Int, &t.minMaxOn, nullptr},
              {"TrackBreak", FieldType::Int, &t.trackBreak, nullptr},
              {"MinAppXZPos", FieldType::Float, &t.minAppXZPos, nullptr},
              {"MaxAppXZPos", FieldType::Float, &t.maxAppXZPos, nullptr},
              {"MinSpeed", FieldType::Float, &t.minSpeed, nullptr},
              {"MaxSpeed", FieldType::Float, &t.maxSpeed, nullptr},
              {"AppInc", FieldType::Float, &t.appInc, nullptr},
              {"AppDec", FieldType::Float, &t.appDec, nullptr},
              {"AppYPos", FieldType::Float, &t.appYPos, nullptr},
              {"AppXZPos", FieldType::Float, &t.appXZPos, nullptr},
              {"AppRot", FieldType::Float, &t.appRot, nullptr},
              {"AppXRot", FieldType::Float, &t.appXRot, nullptr},
              {"AppApp", FieldType::Float, &t.appApp, nullptr},
              {"ApproachOn", FieldType::Int, &t.approachOn, nullptr},
              {"AppAppOn", FieldType::Int, &t.appAppOn, nullptr},
              {"AppPosMin", FieldType::Float, &t.appPosMin, nullptr},
              {"AppRotMin", FieldType::Float, &t.appRotMin, nullptr},
              {"MaxDist", FieldType::Float, &t.maxDist, nullptr},
              {"MinDist", FieldType::Float, &t.minDist, nullptr},
              {"LookAt", FieldType::Float, &t.lookAt, nullptr},
              {"VertOffset", FieldType::Float, &t.vertOffset, nullptr},
              {"HillMin", FieldType::Float, &t.hillMin, nullptr},
              {"HillMax", FieldType::Float, &t.hillMax, nullptr},
              {"HillLerp", FieldType::Float, &t.hillLerp, nullptr},
              {"ReverseOn", FieldType::Int, &t.reverseOn, nullptr},
              {"RevOnApp", FieldType::Float, &t.revOnApp, nullptr},
              {"RevOffApp", FieldType::Float, &t.revOffApp, nullptr},
              {"CameraFOV", FieldType::Float, &t.cameraFOV, nullptr},
              {"CameraNear", FieldType::Float, &t.cameraNear, nullptr},
              {"CameraFar", FieldType::Float, &t.cameraFar, nullptr},
              {"BlendGoal", FieldType::Float, &t.blendGoal, nullptr}}};
    t.fileRead = dat::parseText(textOf(b), s);
    return t.fileRead;
}

bool readPov(const Vfs& vfs, const std::string& path, PovTune& t) {
    BytesPtr b = vfs.open(path);
    if (!b) return false;
    Schema s{{{"Offset", FieldType::Vector, &t.offset, nullptr},
              {"Pitch", FieldType::Float, &t.pitch, nullptr},
              {"ReverseOffset", FieldType::Vector, &t.reverseOffset, nullptr},
              {"ApproachOn", FieldType::Int, &t.approachOn, nullptr},
              {"AppAppOn", FieldType::Int, &t.appAppOn, nullptr},
              {"AppXZPos", FieldType::Float, &t.appXZPos, nullptr},
              {"AppYPos", FieldType::Float, &t.appYPos, nullptr},
              {"AppRot", FieldType::Float, &t.appRot, nullptr},
              {"AppXRot", FieldType::Float, &t.appXRot, nullptr},
              {"AppApp", FieldType::Float, &t.appApp, nullptr},
              {"AppPosMin", FieldType::Float, &t.appPosMin, nullptr},
              {"AppRotMin", FieldType::Float, &t.appRotMin, nullptr},
              {"TrackTo", FieldType::Vector, &t.trackTo, nullptr},
              {"MaxDist", FieldType::Float, &t.maxDist, nullptr},
              {"MinDist", FieldType::Float, &t.minDist, nullptr},
              {"LookAt", FieldType::Float, &t.lookAt, nullptr},
              {"LookAbove", FieldType::Float, &t.lookAbove, nullptr},
              {"CameraFOV", FieldType::Float, &t.cameraFOV, nullptr},
              {"CameraNear", FieldType::Float, &t.cameraNear, nullptr},
              {"CameraFar", FieldType::Float, &t.cameraFar, nullptr},
              {"BlendGoal", FieldType::Float, &t.blendGoal, nullptr}}};
    t.fileRead = dat::parseText(textOf(b), s);
    return t.fileRead;
}

}  // namespace

CameraFiles loadCameraFiles(const Vfs& vfs, const std::string& car, float aspect) {
    CameraFiles c;
    const std::string dir = "tune/camera/" + car;
    float lastFar = -1.0f;
    if (readChase(vfs, dir + "_near.camTrackCS", c.nearChase)) lastFar = c.nearChase.cameraFar;
    if (readChase(vfs, dir + "_far.camTrackCS", c.farChase)) lastFar = c.farChase.cameraFar;
    if (readChase(vfs, dir + "_ind.camTrackCS", c.indoor)) lastFar = c.indoor.cameraFar;
    if (readPov(vfs, dir + ".camPovCS", c.bumper)) lastFar = c.bumper.cameraFar;
    if (readPov(vfs, dir + "_dash.camPovCS", c.dash)) {
        lastFar = c.dash.cameraFar;
        if (aspect < 1.3f) c.dash.offset.z = c.dash.offset.z * 0.7352941f;
    }
    if (lastFar > 0.0f) c.farPlane = lastFar;
    // Near planes (cameras.md, "Projection"): 0.5 / 0.1 when the file was read (the file's value
    // is ignored), else the built-in 1.0 / 3.0.
    for (ChaseTune* t : {&c.nearChase, &c.farChase, &c.indoor}) t->cameraNear = t->fileRead ? 0.5f : 1.0f;
    for (PovTune* t : {&c.bumper, &c.dash}) t->cameraNear = t->fileRead ? 0.1f : 3.0f;
    return c;
}

}  // namespace cc
