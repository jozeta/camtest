#pragma once
#include <windows.h>
#include <string>
#include <vector>

// Wire protocol between the GUI process (pipe server) and the per-camera
// media source running inside the Frame Server process (pipe client).
//
// The pipe is opened in PIPE_TYPE_MESSAGE / PIPE_READMODE_MESSAGE mode, so
// each WriteFile call is delivered as exactly one message to the matching
// ReadFile call on the other end -- no manual length-prefix framing needed
// on top of what the pipe itself already provides.
//
// Messages are UTF-8 text, one of:
//   "IMG:<absolute file path>"   -- GUI telling the source to (re)load this image
//
// Kept deliberately tiny: this is the only thing that ever needs to cross
// the process boundary at runtime (the initial image and every live-swap).
namespace WireFormat {

constexpr DWORD kMaxMessageBytes = 32 * 1024; // generous headroom for any real file path

inline std::string EncodeImageMessage(const std::wstring& path) {
    int len = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), (int)path.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), (int)path.size(), utf8.data(), len, nullptr, nullptr);
    return "IMG:" + utf8;
}

// Returns true and fills outPath if `message` is a well-formed IMG message.
inline bool DecodeImageMessage(const std::string& message, std::wstring& outPath) {
    constexpr char kPrefix[] = "IMG:";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    if (message.size() < kPrefixLen || message.compare(0, kPrefixLen, kPrefix) != 0) {
        return false;
    }
    std::string utf8 = message.substr(kPrefixLen);
    if (utf8.empty()) {
        outPath.clear();
        return true;
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), wide.data(), len);
    outPath = std::move(wide);
    return true;
}

// Writes one message to a message-mode pipe handle. Returns false on any
// I/O error (broken pipe, etc.) -- callers treat that as "the other side is
// gone" and stop trying to use the handle.
inline bool WriteMessage(HANDLE pipe, const std::string& message) {
    if (message.size() > kMaxMessageBytes) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(pipe, message.data(), (DWORD)message.size(), &written, nullptr);
    return ok && written == message.size();
}

// Reads one message from a message-mode pipe handle (blocking). Returns
// false on broken pipe / error / message too large for kMaxMessageBytes.
inline bool ReadMessage(HANDLE pipe, std::string& outMessage) {
    std::vector<char> buf(kMaxMessageBytes);
    DWORD read = 0;
    BOOL ok = ReadFile(pipe, buf.data(), (DWORD)buf.size(), &read, nullptr);
    if (!ok) {
        // ERROR_MORE_DATA would mean a message exceeded our buffer; treat
        // as a protocol violation rather than trying to reassemble it.
        return false;
    }
    outMessage.assign(buf.data(), read);
    return true;
}

} // namespace WireFormat
