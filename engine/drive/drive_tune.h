// Tune files used by driving outside the car physics: tune/<car>.asNode (speed-sensitive
// steering, spec/engine/controls.md) and tune/camera/* (spec/engine/cameras.md).
#pragma once

#include <string>

#include "core/math.h"

namespace cc {

class Vfs;

struct SteeringTune {
    int speedSensitive = 2;
    float speedBaseLow = 5.0f, speedBaseHi = 100.0f;
    float deltaInLo = 2.5f, deltaInHi = 1.5f;
    float deltaOutLo = 3.5f, deltaOutHi = 2.5f;
    float filterLo = 2.0f, filterHi = 1.0f;
    float joySensitivityLow = 0.5f, joySensitivityHi = 1.1f;
    float joySteerFilterLow = 1.0f, joySteerFilterHi = 3.0f;
    // The joystick approach mode (controls.md, "Joystick and steering wheel").
    int joyApp = 0;
    float joyApproachOutLo = 10.0f, joyApproachInLo = 10.0f, joyApproachOutHi = 2.0f, joyApproachInHi = 4.0f;
    float joyAppApp = 0.0f;
    // The steering-wheel device's own values (controls.md: defaults as the Joy keys).
    float wheelSensitivityLow = 0.5f, wheelSensitivityHi = 1.1f;
    float wheelSteerFilterLow = 1.0f, wheelSteerFilterHi = 3.0f;
    int wheelApp = 0;
    float wheelApproachOutLo = 10.0f, wheelApproachInLo = 10.0f, wheelApproachOutHi = 2.0f, wheelApproachInHi = 4.0f;
    float wheelAppApp = 0.0f;
    // The mouse device's values (the mouse device itself is not specified, controls.md).
    float mouseSensitivityLow = 0.6f, mouseSensitivityHi = 1.8f;
    float mouseSteerFilterLow = 1.5f, mouseSteerFilterHi = 4.0f;
    bool fileRead = false;
};

SteeringTune loadSteeringTune(const Vfs& vfs, const std::string& baseName);

struct ChaseTune {
    vec3 offset{0.0f, 1.9f, 7.7f};
    vec3 trackTo{0.0f, 0.8f, 0.0f};
    int collideType = 0;
    int minMaxOn = 1;
    int trackBreak = 0;
    float minAppXZPos = 1.8f, maxAppXZPos = 12.0f;
    float minSpeed = 5.0f, maxSpeed = 35.0f;
    float appInc = 15.0f, appDec = 10.0f;
    float appYPos = 5.0f;
    float appXZPos = 0.0f;
    float appRot = 30.0f, appXRot = 10.0f;
    float appApp = 0.7f;
    int approachOn = 1, appAppOn = 1;
    float appPosMin = 0.25f, appRotMin = 0.01f;
    float maxDist = 11.0f, minDist = 7.93f;
    float lookAt = 1.0f;
    float vertOffset = 0.6f;
    float hillMin = -0.56f, hillMax = 0.56f, hillLerp = 0.05f;
    int reverseOn = 1;
    float revOnApp = 2.0f, revOffApp = 4.0f;
    float cameraFOV = 60.0f;
    float cameraNear = 1.0f, cameraFar = 600.0f;
    float blendGoal = 1.0f;
    bool fileRead = false;
};

struct PovTune {
    vec3 offset{0.0f, 1.6f, 0.7f};
    float pitch = 0.0f;
    vec3 reverseOffset{0.0f, 1.7f, 0.75f};
    int approachOn = 1, appAppOn = 1;
    float appXZPos = 28.0f, appYPos = 28.0f, appRot = 28.0f, appXRot = 0.5f, appApp = 0.7f;
    float appPosMin = 0.0f, appRotMin = 0.0f;
    vec3 trackTo{0.0f, 0.8f, 0.0f};
    float maxDist = 1.8f, minDist = 1.74f;
    float lookAt = 0.0f;
    float lookAbove = 0.0f;
    float cameraFOV = 60.0f;
    float cameraNear = 3.0f, cameraFar = 600.0f;
    float blendGoal = 1.0f;
    bool fileRead = false;
};

struct CameraFiles {
    ChaseTune nearChase, farChase, indoor;
    PovTune bumper, dash;
    float farPlane = 600.0f;  // CameraFar of the last file read
};

// Reads near, far, ind, bumper, dash in that order. `aspect` = width/height (the dash offset is
// scaled when it is below 1.3).
CameraFiles loadCameraFiles(const Vfs& vfs, const std::string& baseName, float aspect);

}  // namespace cc
