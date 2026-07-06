#include "common/FileDialog.h"

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h> 
#include <vector>

namespace pf {

static std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

static std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::string openFileDialog(const char* filters) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool coinit = SUCCEEDED(hr);

    std::string result = "";
    IFileOpenDialog *pFileOpen;
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        if (filters && filters[0]) {
            std::vector<COMDLG_FILTERSPEC> spec;
            const char* p = filters;
            while (*p) {
                const char* name = p;
                p += strlen(p) + 1;
                const char* ext = p;
                p += strlen(p) + 1;
                
                std::wstring wname = utf8_to_wstring(name);
                std::wstring wext = utf8_to_wstring(ext);
                
                // Allocate copies because COMDLG_FILTERSPEC takes LPCWSTR
                wchar_t* copyName = new wchar_t[wname.size() + 1];
                wcscpy(copyName, wname.c_str());
                wchar_t* copyExt = new wchar_t[wext.size() + 1];
                wcscpy(copyExt, wext.c_str());
                
                spec.push_back({ copyName, copyExt });
            }
            if (!spec.empty()) {
                pFileOpen->SetFileTypes((UINT)spec.size(), spec.data());
            }
            
            hr = pFileOpen->Show(NULL);
            if (SUCCEEDED(hr)) {
                IShellItem *pItem;
                hr = pFileOpen->GetResult(&pItem);
                if (SUCCEEDED(hr)) {
                    PWSTR pszFilePath;
                    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                    if (SUCCEEDED(hr)) {
                        result = wstring_to_utf8(pszFilePath);
                        CoTaskMemFree(pszFilePath);
                    }
                    pItem->Release();
                }
            }
            
            for (auto& f : spec) {
                delete[] f.pszName;
                delete[] f.pszSpec;
            }
        } else {
            hr = pFileOpen->Show(NULL);
            if (SUCCEEDED(hr)) {
                IShellItem *pItem;
                hr = pFileOpen->GetResult(&pItem);
                if (SUCCEEDED(hr)) {
                    PWSTR pszFilePath;
                    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                    if (SUCCEEDED(hr)) {
                        result = wstring_to_utf8(pszFilePath);
                        CoTaskMemFree(pszFilePath);
                    }
                    pItem->Release();
                }
            }
        }
        pFileOpen->Release();
    }
    
    if (coinit) {
        CoUninitialize();
    }
    return result;
}

std::vector<std::string> openFileDialogMulti(const char* filters) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool coinit = SUCCEEDED(hr);

    std::vector<std::string> result;
    IFileOpenDialog* pFileOpen = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions = 0;
        if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions)))
            pFileOpen->SetOptions(dwOptions | FOS_ALLOWMULTISELECT);

        std::vector<COMDLG_FILTERSPEC> spec;
        if (filters && filters[0]) {
            const char* p = filters;
            while (*p) {
                const char* name = p;
                p += strlen(p) + 1;
                const char* ext = p;
                p += strlen(p) + 1;

                std::wstring wname = utf8_to_wstring(name);
                std::wstring wext = utf8_to_wstring(ext);

                // Allocate copies because COMDLG_FILTERSPEC takes LPCWSTR
                wchar_t* copyName = new wchar_t[wname.size() + 1];
                wcscpy(copyName, wname.c_str());
                wchar_t* copyExt = new wchar_t[wext.size() + 1];
                wcscpy(copyExt, wext.c_str());

                spec.push_back({ copyName, copyExt });
            }
            if (!spec.empty())
                pFileOpen->SetFileTypes((UINT)spec.size(), spec.data());
        }

        hr = pFileOpen->Show(NULL);
        if (SUCCEEDED(hr)) {
            IShellItemArray* pItems = nullptr;
            hr = pFileOpen->GetResults(&pItems);
            if (SUCCEEDED(hr) && pItems) {
                DWORD count = 0;
                pItems->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* pItem = nullptr;
                    if (SUCCEEDED(pItems->GetItemAt(i, &pItem)) && pItem) {
                        PWSTR pszFilePath = nullptr;
                        if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                            result.push_back(wstring_to_utf8(pszFilePath));
                            CoTaskMemFree(pszFilePath);
                        }
                        pItem->Release();
                    }
                }
                pItems->Release();
            }
        }

        for (auto& f : spec) {
            delete[] f.pszName;
            delete[] f.pszSpec;
        }
        pFileOpen->Release();
    }

    if (coinit) {
        CoUninitialize();
    }
    return result;
}

std::string openFolderDialog() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool coinit = SUCCEEDED(hr);

    std::string result = "";
    IFileOpenDialog *pFileOpen;
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
            pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);
        }
        
        hr = pFileOpen->Show(NULL);
        if (SUCCEEDED(hr)) {
            IShellItem *pItem;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr)) {
                PWSTR pszFilePath;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr)) {
                    result = wstring_to_utf8(pszFilePath);
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    
    if (coinit) {
        CoUninitialize();
    }
    return result;
}

} // namespace pf

#else // !_WIN32
namespace pf {
std::string openFileDialog(const char*) { return ""; }
std::vector<std::string> openFileDialogMulti(const char*) { return {}; }
std::string openFolderDialog() { return ""; }
}
#endif
