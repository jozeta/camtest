#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <functional>

// Media-source-process side of the per-camera named pipe (runs inside the
// Frame Server's svchost.exe once our ImageMediaSource is activated there).
// Connects back to the GUI process's pipe server and delivers each
// received image path via a callback, on a dedicated background thread,
// for as long as the source is alive.
class NamedPipeClient {
public:
    // onImagePath is invoked (from the listener thread) for the initial
    // image and every subsequent live-swap message.
    NamedPipeClient(std::wstring pipeName, std::function<void(const std::wstring&)> onImagePath);
    ~NamedPipeClient();

    NamedPipeClient(const NamedPipeClient&) = delete;
    NamedPipeClient& operator=(const NamedPipeClient&) = delete;

    void Start();
    void Stop();

private:
    void ThreadProc();

    std::wstring pipeName_;
    std::function<void(const std::wstring&)> onImagePath_;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
};
