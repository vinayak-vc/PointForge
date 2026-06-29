#include "viewer/SerialController.h"
#include "common/Log.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pf {

static float normalize(int raw, int lo, int hi) {
    if (hi == lo) return 0.5f;
    float t = (float)(raw - lo) / (float)(hi - lo);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

std::string SerialController::portName() const { return resolvedPort_; }

#ifdef _WIN32

// Find the Bluetooth SPP COM port for a MAC by walking HKLM..\Enum\BTHENUM
// (same registry path the user's C# JoystickReceiverBluetooth uses).
std::string SerialController::findPortByMac(const std::string& macIn) const {
    if (macIn.empty()) return "";
    std::string mac;
    for (char c : macIn) if (c != ':' && c != '-' && c != ' ') mac += (char)std::toupper((unsigned char)c);

    HKEY bth;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SYSTEM\\CurrentControlSet\\Enum\\BTHENUM",
                      0, KEY_READ, &bth) != ERROR_SUCCESS)
        return "";

    std::string result;
    char svcName[256];
    DWORD si = 0, svcLen;
    while (result.empty()) {
        svcLen = sizeof(svcName);
        if (RegEnumKeyExA(bth, si++, svcName, &svcLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        HKEY svc;
        if (RegOpenKeyExA(bth, svcName, 0, KEY_READ, &svc) != ERROR_SUCCESS) continue;

        char inst[256];
        DWORD ii = 0, instLen;
        while (true) {
            instLen = sizeof(inst);
            if (RegEnumKeyExA(svc, ii++, inst, &instLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            std::string instUpper(inst);
            std::transform(instUpper.begin(), instUpper.end(), instUpper.begin(),
                           [](unsigned char c){ return (char)std::toupper(c); });
            if (instUpper.find(mac) == std::string::npos) continue;

            std::string dpPath = std::string(inst) + "\\Device Parameters";
            HKEY dp;
            if (RegOpenKeyExA(svc, dpPath.c_str(), 0, KEY_READ, &dp) == ERROR_SUCCESS) {
                char port[64]; DWORD type = 0, len = sizeof(port);
                if (RegQueryValueExA(dp, "PortName", nullptr, &type,
                                     (LPBYTE)port, &len) == ERROR_SUCCESS && type == REG_SZ) {
                    result = port;
                }
                RegCloseKey(dp);
            }
            if (!result.empty()) break;
        }
        RegCloseKey(svc);
    }
    RegCloseKey(bth);
    return result;
}

void SerialController::readLoop() {
    while (running_.load()) {
        // Resolve the port (auto-detect by MAC, else manual).
        std::string port = autoDetect_ ? findPortByMac(mac_) : std::string();
        if (port.empty()) port = manualPort_;
        resolvedPort_ = port;
        if (port.empty()) { Sleep(1000); continue; }

        // \\.\COMx form handles COM10+.
        std::string dev = "\\\\.\\" + port;
        HANDLE h = CreateFileA(dev.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) { Sleep(1000); continue; }

        DCB dcb = {}; dcb.DCBlength = sizeof(dcb);
        if (GetCommState(h, &dcb)) {
            dcb.BaudRate = CBR_9600; dcb.ByteSize = 8;
            dcb.Parity = NOPARITY;  dcb.StopBits = ONESTOPBIT;
            dcb.fDtrControl = DTR_CONTROL_ENABLE; dcb.fRtsControl = RTS_CONTROL_ENABLE;
            SetCommState(h, &dcb);
        }
        COMMTIMEOUTS to = {};
        to.ReadIntervalTimeout = 50;
        to.ReadTotalTimeoutConstant = 200;
        to.ReadTotalTimeoutMultiplier = 0;
        SetCommTimeouts(h, &to);

        connected_.store(true);
        logInfo("SerialController: connected on " + port);

        std::string acc;
        char buf[256];
        while (running_.load()) {
            DWORD got = 0;
            if (!ReadFile(h, buf, sizeof(buf), &got, nullptr)) break; // device gone
            if (got == 0) continue;
            acc.append(buf, buf + got);

            size_t nl;
            while ((nl = acc.find('\n')) != std::string::npos) {
                std::string line = acc.substr(0, nl);
                acc.erase(0, nl + 1);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (line.empty()) continue;

                if (line == "PAUSE") { pause_.store(true); continue; }
                if (line == "PLAY")  { play_.store(true);  continue; }

                // "x,y,b"
                int px, py, pb;
                if (std::sscanf(line.c_str(), "%d,%d,%d", &px, &py, &pb) == 3) {
                    nx_.store(normalize(px, xMin_, xMax_));
                    ny_.store(normalize(py, yMin_, yMax_));
                    trig_.store(pb == 0); // button active-low, like the C# script
                }
            }
        }

        CloseHandle(h);
        connected_.store(false);
        if (running_.load()) Sleep(1000); // device dropped — retry
    }
}

#else // non-Windows stub

std::string SerialController::findPortByMac(const std::string&) const { return ""; }
void SerialController::readLoop() {}

#endif

void SerialController::start(const std::string& mac, const std::string& manualPort, bool autoDetect) {
    stop();
    mac_ = mac; manualPort_ = manualPort; autoDetect_ = autoDetect;
    running_.store(true);
    thread_ = std::thread(&SerialController::readLoop, this);
}

void SerialController::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    connected_.store(false);
}

} // namespace pf
