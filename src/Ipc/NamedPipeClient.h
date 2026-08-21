#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

// Media-source-process side of the per-camera named pipe (runs inside the
// Frame Server's svchost.exe once our ImageMediaSource is activated there).
// Connects back to the GUI process's pipe server and delivers each
// received image path via a callback, on a dedicated background thread,
// for as long as the source is alive.
//
// The GUI's pipe server can go away and come back at any time (user hits
// Stop, then Start again) while this client -- and the consumer process
// hosting it -- keeps running the whole time. So the listener thread never
// just gives up after one connection: it reconnects for as long as the
// source is alive, only stopping for good once Stop() is called.
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

    // Guards pipe_ against the connect/reconnect churn in ThreadProc racing
    // Stop()'s CancelIoEx/close from another thread.
    std::mutex pipeMutex_;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
};
