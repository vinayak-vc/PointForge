#include "viewer/Controller.h"
#include "common/Log.h"
#include <cmath>

namespace pf {

void GameInput::openFirst() {
    if (connected()) return;
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; ++i) {
        if (SDL_IsGameController(i)) {
            gc_ = SDL_GameControllerOpen(i);
            if (gc_) {
                SDL_Joystick* j = SDL_GameControllerGetJoystick(gc_);
                id_ = SDL_JoystickInstanceID(j);
                const char* nm = SDL_GameControllerName(gc_);
                name_ = nm ? nm : "GameController";
                logInfo("Controller: opened (gamepad) " + name_);
                return;
            }
        }
    }
    // No recognised gamepad — fall back to the first raw joystick (custom HID).
    for (int i = 0; i < n; ++i) {
        joy_ = SDL_JoystickOpen(i);
        if (joy_) {
            id_ = SDL_JoystickInstanceID(joy_);
            const char* nm = SDL_JoystickName(joy_);
            name_ = nm ? nm : "Joystick";
            logInfo("Controller: opened (raw joystick) " + name_ + " axes=" +
                    std::to_string(SDL_JoystickNumAxes(joy_)) + " buttons=" +
                    std::to_string(SDL_JoystickNumButtons(joy_)));
            return;
        }
    }
}

void GameInput::shutdown() {
    if (gc_)  { SDL_GameControllerClose(gc_); gc_ = nullptr; }
    if (joy_) { SDL_JoystickClose(joy_);     joy_ = nullptr; }
    id_ = -1;
    name_.clear();
    cur_ = prev_ = PadState{};
}

void GameInput::onDeviceEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_JOYDEVICEADDED:
            if (!connected()) openFirst();
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (gc_ && e.cdevice.which == id_) { shutdown(); openFirst(); }
            break;
        case SDL_JOYDEVICEREMOVED:
            if (joy_ && e.jdevice.which == id_) { shutdown(); openFirst(); }
            break;
        default: break;
    }
}

float GameInput::applyDeadzone(float v) const {
    float a = std::fabs(v);
    if (a < deadzone) return 0.0f;
    // Rescale [deadzone,1] -> [0,1] so motion starts smoothly past the dead band.
    float s = (a - deadzone) / (1.0f - deadzone);
    return (v < 0.0f ? -s : s);
}

void GameInput::poll() {
    prev_ = cur_;
    cur_ = PadState{};
    if (gc_) {
        auto ax = [&](SDL_GameControllerAxis a) {
            return SDL_GameControllerGetAxis(gc_, a) / 32767.0f;
        };
        cur_.lx = applyDeadzone(ax(SDL_CONTROLLER_AXIS_LEFTX));
        cur_.ly = applyDeadzone(ax(SDL_CONTROLLER_AXIS_LEFTY));
        cur_.rx = applyDeadzone(ax(SDL_CONTROLLER_AXIS_RIGHTX));
        cur_.ry = applyDeadzone(ax(SDL_CONTROLLER_AXIS_RIGHTY));
        cur_.lt = std::fmax(0.0f, ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT));
        cur_.rt = std::fmax(0.0f, ax(SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
        auto bt = [&](SDL_GameControllerButton b) {
            return SDL_GameControllerGetButton(gc_, b) != 0;
        };
        cur_.btn[PAD_A]     = bt(SDL_CONTROLLER_BUTTON_A);
        cur_.btn[PAD_B]     = bt(SDL_CONTROLLER_BUTTON_B);
        cur_.btn[PAD_X]     = bt(SDL_CONTROLLER_BUTTON_X);
        cur_.btn[PAD_Y]     = bt(SDL_CONTROLLER_BUTTON_Y);
        cur_.btn[PAD_LB]    = bt(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        cur_.btn[PAD_RB]    = bt(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        cur_.btn[PAD_BACK]  = bt(SDL_CONTROLLER_BUTTON_BACK);
        cur_.btn[PAD_START] = bt(SDL_CONTROLLER_BUTTON_START);
        cur_.btn[PAD_DU]    = bt(SDL_CONTROLLER_BUTTON_DPAD_UP);
        cur_.btn[PAD_DD]    = bt(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        cur_.btn[PAD_DL]    = bt(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        cur_.btn[PAD_DR]    = bt(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    } else if (joy_) {
        int na = SDL_JoystickNumAxes(joy_);
        int nb = SDL_JoystickNumButtons(joy_);
        auto ax = [&](int i) {
            return (i >= 0 && i < na) ? SDL_JoystickGetAxis(joy_, i) / 32767.0f : 0.0f;
        };
        auto bt = [&](int i) {
            return (i >= 0 && i < nb) ? SDL_JoystickGetButton(joy_, i) != 0 : false;
        };
        cur_.lx = applyDeadzone(ax(jAxisX));
        cur_.ly = applyDeadzone(ax(jAxisY));
        cur_.rx = applyDeadzone(ax(jAxisRX));
        cur_.ry = applyDeadzone(ax(jAxisRY));
        cur_.btn[PAD_A]     = bt(jBtnA);
        cur_.btn[PAD_B]     = bt(jBtnB);
        cur_.btn[PAD_LB]    = bt(jBtnLB);
        cur_.btn[PAD_RB]    = bt(jBtnRB);
        cur_.btn[PAD_BACK]  = bt(jBtnBack);
        cur_.btn[PAD_START] = bt(jBtnStart);
    }
}

int GameInput::rawAxisCount() const {
    if (gc_) return SDL_CONTROLLER_AXIS_MAX;
    if (joy_) return SDL_JoystickNumAxes(joy_);
    return 0;
}
int GameInput::rawButtonCount() const {
    if (gc_) return SDL_CONTROLLER_BUTTON_MAX;
    if (joy_) return SDL_JoystickNumButtons(joy_);
    return 0;
}
float GameInput::rawAxis(int i) const {
    if (gc_) return SDL_GameControllerGetAxis(gc_, (SDL_GameControllerAxis)i) / 32767.0f;
    if (joy_ && i >= 0 && i < SDL_JoystickNumAxes(joy_)) return SDL_JoystickGetAxis(joy_, i) / 32767.0f;
    return 0.0f;
}
bool GameInput::rawButton(int i) const {
    if (gc_) return SDL_GameControllerGetButton(gc_, (SDL_GameControllerButton)i) != 0;
    if (joy_ && i >= 0 && i < SDL_JoystickNumButtons(joy_)) return SDL_JoystickGetButton(joy_, i) != 0;
    return false;
}

} // namespace pf
