#include "AppConfig.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <codecvt>

namespace {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len, nullptr, nullptr);
    return out;
}

} // namespace

std::wstring GetExecutablePath() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, len);
}

std::wstring GetFilterDllPath() {
    std::wstring exePath = GetExecutablePath();
    size_t slash = exePath.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? L"." : exePath.substr(0, slash);
    return dir + L"\\camtest_filter.dll";
}

std::wstring GetConfigFilePath() {
    std::wstring exePath = GetExecutablePath();
    size_t slash = exePath.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? L"." : exePath.substr(0, slash);
    return dir + L"\\camtest.cfg";
}

AppConfig AppConfig::Load() {
    AppConfig cfg;
    std::wstring path = GetConfigFilePath();

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return cfg; // defaults
    }

    std::string line;
    while (std::getline(file, line)) {
        // Strip trailing CR if present (file may have been written with \r\n).
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        if (key == "count") {
            int c = 1;
            try { c = std::stoi(value); } catch (...) {}
            if (c < 1) c = 1;
            if (c > AppConfig::kMaxCameras) c = AppConfig::kMaxCameras;
            cfg.cameraCount = c;
        } else if (key.rfind("cam", 0) == 0 && key.size() > 3) {
            // cam0.image, cam1.image, ...
            size_t dot = key.find('.');
            if (dot != std::string::npos) {
                std::string idxStr = key.substr(3, dot - 3);
                std::string field = key.substr(dot + 1);
                int idx = -1;
                try { idx = std::stoi(idxStr); } catch (...) {}
                if (idx >= 0 && idx < AppConfig::kMaxCameras && field == "image") {
                    cfg.imagePaths[idx] = Utf8ToWide(value);
                }
            }
        } else if (key == "registered") {
            cfg.registered = (value == "1");
        } else if (key == "registeredExePath") {
            cfg.registeredExePath = Utf8ToWide(value);
        }
    }

    return cfg;
}

bool AppConfig::Save() const {
    std::wstring path = GetConfigFilePath();
    std::wstring tmpPath = path + L".tmp";

    {
        std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;

        file << "count=" << cameraCount << "\n";
        for (int i = 0; i < kMaxCameras; ++i) {
            file << "cam" << i << ".image=" << WideToUtf8(imagePaths[i]) << "\n";
        }
        file << "registered=" << (registered ? "1" : "0") << "\n";
        file << "registeredExePath=" << WideToUtf8(registeredExePath) << "\n";
    }

    // Atomic-ish swap: replace the real file with the fully-written temp file.
    return MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
