#pragma once
#include <SDL.h>
#include <string>

namespace pf {

// Logical buttons (Xbox naming). Raw joysticks map to these via the jBtn* indices.
enum PadBtn {
    PAD_A, PAD_B, PAD_X, PAD_Y,
    PAD_LB, PAD_RB, PAD_BACK, PAD_START,
    PAD_DU, PAD_DD, PAD_DL, PAD_DR,
    PAD_COUNT
};

struct PadState {
    float lx = 0, ly = 0, rx = 0, ry = 0; // sticks, deadzoned, [-1,1]
    float lt = 0, rt = 0;                 // triggers, [0,1]
    bool  btn[PAD_COUNT] = {};
};

// Unified game-input wrapper over SDL. Prefers SDL_GameController (Xbox-style,
// auto-mapped); falls back to a raw SDL_Joystick (e.g. a custom HID) whose axes
// and buttons are read by configurable index. No GL/ImGui dependency.
class GameInput {
public:
    ~GameInput() { shutdown(); }

    void openFirst();                       // open the first available device
    void shutdown();
    void onDeviceEvent(const SDL_Event& e); // SDL_CONTROLLER*/SDL_JOY* add/remove
    void poll();                            // refresh cur(); keeps previous for edges

    bool        connected() const { return gc_ != nullptr || joy_ != nullptr; }
    bool        isGameController() const { return gc_ != nullptr; }
    const char* name() const { return name_.c_str(); }

    const PadState& cur() const { return cur_; }
    bool held(PadBtn b)    const { return cur_.btn[b]; }
    bool pressed(PadBtn b) const { return cur_.btn[b] && !prev_.btn[b]; } // rising edge

    // ---- tunables / bindings (persisted by the viewer) --------------------
    float deadzone = 0.18f;
    // Raw-joystick axis/button indices (ignored for SDL_GameController devices).
    int jAxisX = 0, jAxisY = 1, jAxisRX = 2, jAxisRY = 3;
    int jBtnA = 0, jBtnB = 1, jBtnLB = 4, jBtnRB = 5, jBtnBack = 6, jBtnStart = 7;

    // ---- raw introspection for a rebinding UI ----------------------------
    int   rawAxisCount() const;
    int   rawButtonCount() const;
    float rawAxis(int i) const;   // [-1,1]
    bool  rawButton(int i) const;

private:
    float applyDeadzone(float v) const;

    SDL_GameController* gc_  = nullptr;
    SDL_Joystick*      joy_  = nullptr;
    SDL_JoystickID     id_   = -1;
    std::string        name_;
    PadState           cur_, prev_;
};

} // namespace pf
