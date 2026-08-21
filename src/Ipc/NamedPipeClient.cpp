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
    {
        std::lock_guard<std::mutex> lock(pipeMutex_);
        if (pipe_ != INVALID_HANDLE_VALUE) {
            // Unblocks any pending overlapped ReadFile on this handle so the
            // thread can observe stopping_ and exit.
            CancelIoEx(pipe_, nullptr);
        }
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    // ThreadProc closes pipe_ itself (on every disconnect/exit path) before
    // returning, so there's nothing left to close here.
}

void NamedPipeClient::ThreadProc() {
    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!readEvent) return;

    std::vector<char> buf(WireFormat::kMaxMessageBytes);

    // Outer loop: (re)connect and pump messages until Stop() is called. The
    // GUI's pipe server can be stopped and restarted at any time (user hits
    // Stop, changes an image, hits Start again) while this consumer process
    // keeps the filter -- and this thread -- alive the whole time, so a
    // dropped connection must lead back to reconnecting, not to exiting.
    while (!stopping_.load()) {
        // --- Connect phase: retry until connected or told to stop. The GUI
        // process's pipe server may not exist yet (or may not exist right
        // now), so this polls rather than failing outright. ---
        HANDLE connectedPipe = INVALID_HANDLE_VALUE;
        while (!stopping_.load()) {
            if (WaitNamedPipeW(pipeName_.c_str(), 2000)) {
                HANDLE h = CreateFileW(pipeName_.c_str(), GENERIC_READ,
                                        0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    connectedPipe = h;
                    break;
                }
            }
            if (stopping_.load()) break;
            Sleep(200);
        }
        if (connectedPipe == INVALID_HANDLE_VALUE) break; // stopping

        {
            std::lock_guard<std::mutex> lock(pipeMutex_);
            pipe_ = connectedPipe;
        }

        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(connectedPipe, &mode, nullptr, nullptr);

        // --- Read phase: pump messages until disconnect/error or stop. ---
        while (!stopping_.load()) {
            OVERLAPPED ov{};
            ov.hEvent = readEvent;
            ResetEvent(readEvent);

            BOOL ok = ReadFile(connectedPipe, buf.data(), (DWORD)buf.size(), nullptr, &ov);
            DWORD bytesRead = 0;
            bool success = false;
            if (ok) {
                success = GetOverlappedResult(connectedPipe, &ov, &bytesRead, FALSE) != 0;
            } else if (GetLastError() == ERROR_IO_PENDING) {
                success = GetOverlappedResult(connectedPipe, &ov, &bytesRead, TRUE) != 0;
            }

            if (stopping_.load()) break;
            if (!success) break; // broken pipe / GUI stopped this slot -- go reconnect

            std::string message(buf.data(), bytesRead);
            std::wstring path;
            if (WireFormat::DecodeImageMessage(message, path) && onImagePath_) {
                onImagePath_(path);
            }
        }

        {
            std::lock_guard<std::mutex> lock(pipeMutex_);
            CloseHandle(connectedPipe);
            pipe_ = INVALID_HANDLE_VALUE;
        }
        // Loop back: if not stopping, this re-enters the connect phase and
        // waits for the GUI's pipe server to come back.
    }

    CloseHandle(readEvent);
}
