#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

// GUI-process side of a camera slot's named pipe. Unlike a typical
// single-client pipe, this must support:
//   - Zero or more consumer processes (Zoom, browser, Windows Camera app,
//     OBS, ...) connecting at unpredictable times -- including before the
//     GUI has even started serving that slot, or while it's already
//     running -- since DirectShow can load our filter into a consumer's
//     process independently of our own lifecycle.
//   - Multiple consumers holding the same camera open at once, all of
//     which must receive the same image and the same live-swap updates.
//
// The pipe name is therefore FIXED per slot (no PID suffix -- a consumer
// process has no way to know our PID in advance), and the server maintains
// a small pool of pipe instances so several clients can be connected
// concurrently. Each connected client is periodically re-sent the current
// image path as a heartbeat, which doubles as disconnect detection (a
// failed write means that client is gone); NamedPipeClient-side consumers
// are expected to skip re-decoding when the path hasn't actually changed.
class NamedPipeServer {
public:
    explicit NamedPipeServer(int slotIndex, int maxConcurrentClients = 8);
    ~NamedPipeServer();

    NamedPipeServer(const NamedPipeServer&) = delete;
    NamedPipeServer& operator=(const NamedPipeServer&) = delete;

    // Starts the pool of acceptor threads. Safe to call once.
    bool StartListening();

    // Updates the current image path and broadcasts it to every currently
    // connected client. New connections automatically receive whatever
    // path was most recently set here, even if they connect long after
    // this call.
    void SetImagePath(const std::wstring& path);

    // Stops all acceptor threads and closes all client connections.
    void Stop();

    const std::wstring& GetPipeName() const { return pipeName_; }

    // Number of clients currently connected (i.e. consumer processes
    // actively holding this camera open). Cheap snapshot -- for UI display,
    // not synchronized with any particular SetImagePath call.
    int GetConnectedClientCount() const;

private:
    struct ClientSlot {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        std::atomic<bool> connected{false};
        std::thread thread;
    };

    void AcceptorThreadProc(ClientSlot* slot);
    bool SendCurrentPath(HANDLE pipe); // returns false on write failure

    std::wstring pipeName_;
    int maxConcurrentClients_;
    std::vector<std::unique_ptr<ClientSlot>> clients_;
    HANDLE stopEvent_ = nullptr;
    std::atomic<bool> stopping_{false};

    // Consumers can be sandboxed/low-integrity processes (a browser's or
    // Teams' capture process, or the OS's own Frame Server service hosting
    // this filter for Media Foundation-based callers) running under a
    // different security context than this GUI process. The default
    // (nullptr) pipe DACL only grants access within roughly the creating
    // token's own context, so those callers connect but silently never
    // get in -- reproducing as "camera enumerates, image never appears".
    // Built once in StartListening(), freed in the destructor.
    PSECURITY_DESCRIPTOR pipeSecurityDescriptor_ = nullptr;
    SECURITY_ATTRIBUTES pipeSecurityAttributes_{};

    std::mutex pathMutex_;
    std::wstring currentPath_;
};
