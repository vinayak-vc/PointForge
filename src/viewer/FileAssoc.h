#pragma once
// Windows file association for .vxpc packages (double-click in Explorer opens
// the viewer). Registered per-user under HKCU\Software\Classes — no admin
// rights, no installer. Because every build renames the exe
// (ViitorXPCViewer_v<N>.exe), the association is refreshed at startup to point
// at the exe that is actually running; launching a newer build re-targets it.
// Non-Windows builds compile these as no-op stubs.

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>   // SHChangeNotify

namespace pf {

namespace assoc_detail {

inline std::wstring currentExePath() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : std::wstring();
}

inline bool setKeyDefault(const std::wstring& subKey, const std::wstring& value) {
    return RegSetKeyValueW(HKEY_CURRENT_USER, subKey.c_str(), nullptr, REG_SZ,
                           value.c_str(),
                           (DWORD)((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

inline std::wstring getKeyDefault(const std::wstring& subKey) {
    wchar_t buf[1024];
    DWORD len = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, subKey.c_str(), nullptr, RRF_RT_REG_SZ,
                     nullptr, buf, &len) != ERROR_SUCCESS)
        return L"";
    return buf;
}

inline constexpr const wchar_t* kProgId = L"ViitorXPC.Package";

} // namespace assoc_detail

// The open-command currently registered for .vxpc ("" if none). Used to decide
// whether a refresh is needed and to show state in Preferences.
inline std::wstring vxpcAssociationCommand() {
    using namespace assoc_detail;
    if (getKeyDefault(L"Software\\Classes\\.vxpc") != kProgId) return L"";
    return getKeyDefault(std::wstring(L"Software\\Classes\\") + kProgId + L"\\shell\\open\\command");
}

// Point the .vxpc association at THIS exe. Idempotent; call at startup so the
// association follows the newest versioned build the user actually runs.
// Returns false if any registry write fails.
inline bool registerVxpcAssociation() {
    using namespace assoc_detail;
    const std::wstring exe = currentExePath();
    if (exe.empty()) return false;
    const std::wstring cmd = L"\"" + exe + L"\" \"%1\"";
    const std::wstring base = std::wstring(L"Software\\Classes\\") + kProgId;

    // Skip the writes (and the Explorer refresh broadcast) when up to date.
    if (vxpcAssociationCommand() == cmd) return true;

    bool ok = setKeyDefault(L"Software\\Classes\\.vxpc", kProgId) &&
              setKeyDefault(base, L"ViitorXPC Point Cloud Package") &&
              setKeyDefault(base + L"\\DefaultIcon", L"\"" + exe + L"\",0") &&
              setKeyDefault(base + L"\\shell\\open\\command", cmd);
    if (ok) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

// Remove the per-user association (Preferences checkbox off).
inline void unregisterVxpcAssociation() {
    using namespace assoc_detail;
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.vxpc");
    RegDeleteTreeW(HKEY_CURRENT_USER,
                   (std::wstring(L"Software\\Classes\\") + kProgId).c_str());
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

} // namespace pf

#else // !_WIN32

namespace pf {
inline std::wstring vxpcAssociationCommand() { return L""; }
inline bool registerVxpcAssociation() { return false; }
inline void unregisterVxpcAssociation() {}
} // namespace pf

#endif
