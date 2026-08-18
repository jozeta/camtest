#include "NamedPipeClient.h"
#include "ProtocolWireFormat.h"
#include <vector>

NamedPipeClient::NamedPipeClient(std::wstring pipeName, std::function<void(const std::wstring&)> onImagePath)
    : pipeName_(std::move(pipeName)), onImagePath_(std::move(onImagePath)) {}

NamedPipeClient::~NamedPipeClient() {
    Stop();
}

void NamedPipeClient::Start() {
    thread_ = std::thread(&NamedPipeClient::ThreadProc, this);
}

void NamedPipeClient::Stop() {
    stopping_.store(true);
    if (pipe_ != INVALID_HANDLE_VALUE) {
        // Unblocks any pending overlapped ReadFile on this handle so the
        // thread can observe stopping_ and exit.
        CancelIoEx(pipe_, nullptr);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

void NamedPipeClient::ThreadProc() {
    // The GUI process's pipe server is created before this media source is
    // ever activated, but there can still be a brief race, so retry.
    while (!stopping_.load()) {
        if (WaitNamedPipeW(pipeName_.c_str(), 2000)) {
            pipe_ = CreateFileW(pipeName_.c_str(), GENERIC_READ,
                                 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe_ != INVALID_HANDLE_VALUE) {
                break;
            }
        }
        if (stopping_.load()) return;
        Sleep(200);
    }
    if (pipe_ == INVALID_HANDLE_VALUE) return;

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr);

    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!readEvent) return;

    std::vector<char> buf(WireFormat::kMaxMessageBytes);

    while (!stopping_.load()) {
        OVERLAPPED ov{};
        ov.hEvent = readEvent;
        ResetEvent(readEvent);

        BOOL ok = ReadFile(pipe_, buf.data(), (DWORD)buf.size(), nullptr, &ov);
        DWORD bytesRead = 0;
        bool success = false;
        if (ok) {
            success = GetOverlappedResult(pipe_, &ov, &bytesRead, FALSE) != 0;
        } else if (GetLastError() == ERROR_IO_PENDING) {
            success = GetOverlappedResult(pipe_, &ov, &bytesRead, TRUE) != 0;
        }

        if (stopping_.load()) break;
        if (!success) break; // broken pipe / GUI process gone -- nothing more to receive

        std::string message(buf.data(), bytesRead);
        std::wstring path;
        if (WireFormat::DecodeImageMessage(message, path) && onImagePath_) {
            onImagePath_(path);
        }
    }

    CloseHandle(readEvent);
}
