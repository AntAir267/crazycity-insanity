// MM2's force feedback (spec/engine/controls.md, "Force feedback"): four effects on a joystick or
// wheel that reports force feedback (friction from the front-left tyre's surface, a centring spring
// that rises with speed while that tyre grips, a rumble from the surface's bumps or the damage, and
// short jolts from hard wheel compression and counted impacts), only while the driver's
// force-feedback option is on and the joystick or wheel device is in use (never the gamepad).
// The jolt directions come from the game-wide random generator, as in MM2: a draw happens only
// while force feedback is active, so check runs and games without it never draw.
#pragma once

namespace cc {

namespace phys {
class Car;
}

// The four effects as the controller plays them (natively SDL's haptic interface: platform/).
// Values in MM2's units: coefficients and forces 0 … 10000, the rumble period in microseconds,
// directions in hundredths of a degree (0 = from straight ahead, 9000 = from the right).
class FfDevice {
public:
    enum Effect { Friction = 0, Spring = 1, Rumble = 2, Jolt = 3 };
    virtual ~FfDevice() = default;
    virtual bool playing(Effect e) const = 0;
    virtual void start(Effect e) = 0;  // starts, or starts over
    virtual void stop(Effect e) = 0;
    virtual void setFriction(int coefficient) = 0;
    virtual void setSpring(int coefficient) = 0;
    virtual void setRumble(int periodUs, int magnitude) = 0;  // the magnitude may pass 10000 (R > 1)
    virtual void jolt(int strength, int direction) = 0;       // set and start
};

class ForceFeedback {
public:
    // This frame's device and whether force feedback is active (the option on, the joystick or
    // wheel device, a controller reporting force feedback); C and R, the two strengths.
    void configure(FfDevice* device, bool active, float collision, float road);
    bool active() const { return active_ && dev_ != nullptr; }
    // The real-time clock in seconds (runs on while paused).
    void setClock(double now) { now_ = now; }
    // "Each frame", steps 2 – 6: at the end of the player's frame update, before the physics.
    void frame(const phys::Car& car);
    // "Impact jolts": a counted impact of the player's car (the slot's total after it).
    void impact(const phys::Car& car, float total);
    // The force-feedback reset (every reset of the player's car, every paused frame).
    void reset();
    // An in-game pop-up opened: rumble, spring and friction stop.
    void popup();
    // Engine choice: everything stops when no game runs and when the window loses the focus.
    void stopAll();
    // Tests: the jolts sent so far.
    int jolts() const { return jolts_; }

private:
    void sendFriction(float v);
    void sendSpring(float k);
    void rumbleRequest(float f, float m);
    void stopRumble();
    void sendJolt(float m, float angle);
    FfDevice* dev_ = nullptr;
    bool active_ = false;
    float C_ = 1.0f, R_ = 1.0f;
    double now_ = 0.0, J_ = 0.0;
    float Lf_ = 0.0f, Lh_ = 0.0f, Lw_ = 0.0f, Lk_ = 0.0f;
    float sentP_ = -1.0f, sentM_ = -1.0f;  // the rumble's period and magnitude last sent
    unsigned lastReset_ = ~0u;
    int jolts_ = 0;
};

}  // namespace cc
