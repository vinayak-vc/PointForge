#pragma once
#include <atomic>
#include <string>
#include <thread>

namespace pf {

// Reads the ESP32 joystick stream over a Bluetooth SPP virtual COM port and
// exposes it to the viewer. Mirrors the protocol of the user's Unity
// JoystickReceiverBluetooth.cs:
//   "x,y,b"  -> raw X/Y (0..4095) + joystick button (b==0 means pressed)
//   "PAUSE"  -> GPIO22 one-shot click
//   "PLAY"   -> GPIO23 one-shot click
// The COM port is auto-detected from the device MAC via the Windows registry
// (same lookup the C# script uses), with a manual port fallback. Reading runs on
// a background thread because serial reads block. Windows-only; a no-op stub
// elsewhere. No GL/SDL/ImGui dependency.
class SerialController {
public:
    ~SerialController() { stop(); }

    // (Re)start the reader. autoDetect uses mac to find the SPP port; otherwise
    // manualPort (e.g. "COM4") is used. Safe to call repeatedly (rescan).
    void start(const std::string& mac, const std::string& manualPort, bool autoDetect);
    void stop();

    bool        connected() const { return connected_.load(); }
    std::string portName()  const;

    // Normalised axes in 0..1 (centre ~0.5); joystick button held; one-shots.
    float normX() const { return nx_.load(); }
    float normY() const { return ny_.load(); }
    bool  triggerHeld() const { return trig_.load(); }
    bool  consumePause() { return pause_.exchange(false); }
    bool  consumePlay()  { return play_.exchange(false); }

private:
    void        readLoop();
    std::string findPortByMac(const std::string& mac) const;

    std::atomic<bool>  running_{false};
    std::atomic<bool>  connected_{false};
    std::atomic<float> nx_{0.5f}, ny_{0.5f};
    std::atomic<bool>  trig_{false};
    std::atomic<bool>  pause_{false}, play_{false};

    std::thread  thread_;
    std::string  mac_, manualPort_, resolvedPort_;
    bool         autoDetect_ = true;

    // ADC calibration (ESP32 12-bit). Fixed range -> centre 0.5.
    int xMin_ = 0, xMax_ = 4095, yMin_ = 0, yMax_ = 4095;
};

} // namespace pf
