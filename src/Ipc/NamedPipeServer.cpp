#include "NamedPipeServer.h"
#include "ProtocolWireFormat.h"
#include <sddl.h>

namespace {
constexpr DWORD kHeartbeatIntervalMs = 2000;

// Grants read/write to Everyone and to app-container callers (regular and
// restricted), and lowers the pipe's own mandatory integrity label to Low
// so low-integrity callers aren't blocked by the default write-up policy.
// Consumers of this pipe run in all sorts of security contexts we don't
// control -- a sandboxed browser/Teams capture process, or the OS Frame
// Server service hosting this filter for Media Foundation callers -- and
// none of those are covered by the default (nullptr) pipe DACL.
constexpr wchar_t kPipeSecuritySddl[] =
    L"D:(A;;GRGW;;;WD)(A;;GRGW;;;AC)(A;;GRGW;;;S-1-15-2-2)S:(ML;;NW;;;LW)";
}

NamedPipeServer::NamedPipeServer(int slotIndex, int maxConcurrentClients)
    : maxConcurrentClients_(maxConcurrentClients) {
    pipeName_ = L"\\\\.\\pipe\\camtest_cam" + std::to_wstring(slotIndex);
}

NamedPipeServer::~NamedPipeServer() {
    Stop();
}

bool NamedPipeServer::StartListening() {
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) return false;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kPipeSecuritySddl, SDDL_REVISION_1, &pipeSecurityDescriptor_, nullptr)) {
        pipeSecurityDescriptor_ = nullptr; // fall back to CreateNamedPipeW's default DACL
    }
    pipeSecurityAttributes_.nLength = sizeof(SECURITY_ATTRIBUTES);
    pipeSecurityAttributes_.lpSecurityDescriptor = pipeSecurityDescriptor_;
    pipeSecurityAttributes_.bInheritHandle = FALSE;

    for (int i = 0; i < maxConcurrentClients_; ++i) {
        auto slot = std::make_unique<ClientSlot>();
        ClientSlot* raw = slot.get();
        slot->thread = std::thread(&NamedPipeServer::AcceptorThreadProc, this, raw);
        clients_.push_back(std::move(slot));
    }
    return true;
}

void NamedPipeServer::SetImagePath(const std::wstring& path) {
    {
        std::lock_guard<std::mutex> lock(pathMutex_);
        currentPath_ = path;
    }
    for (auto& slot : clients_) {
        if (slot->connected.load()) {
            if (!SendCurrentPath(slot->pipe)) {
                slot->connected.store(false); // acceptor thread will notice and recycle
            }
        }
    }
}

bool NamedPipeServer::SendCurrentPath(HANDLE pipe) {
    std::wstring path;
    {
        std::lock_guard<std::mutex> lock(pathMutex_);
        path = currentPath_;
    }
    std::string msg = WireFormat::EncodeImageMessage(path);

    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;

    BOOL ok = WriteFile(pipe, msg.data(), (DWORD)msg.size(), nullptr, &ov);
    bool success = false;
    if (ok) {
        success = true;
    } else if (GetLastError() == ERROR_IO_PENDING) {
        DWORD written = 0;
        success = GetOverlappedResult(pipe, &ov, &written, TRUE) && written == msg.size();
    }
    CloseHandle(ov.hEvent);
    return success;
}

void NamedPipeServer::AcceptorThreadProc(ClientSlot* slot) {
    while (!stopping_.load()) {
        HANDLE pipe = CreateNamedPipeW(
            pipeName_.c_str(),
            PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            WireFormat::kMaxMessageBytes, WireFormat::kMaxMessageBytes,
            0, &pipeSecurityAttributes_);
        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(500);
            continue;
        }

        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) { CloseHandle(pipe); continue; }

        BOOL immediate = ConnectNamedPipe(pipe, &ov);
        bool connected = false;
        if (immediate) {
            connected = true;
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_PIPE_CONNECTED) {
                connected = true;
            } else if (err == ERROR_IO_PENDING) {
                HANDLE waitHandles[2] = { ov.hEvent, stopEvent_ };
                DWORD w = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (w == WAIT_OBJECT_0) {
                    DWORD bytes = 0;
                    connected = GetOverlappedResult(pipe, &ov, &bytes, FALSE) != 0;
                }
            }
        }
        CloseHandle(ov.hEvent);

        if (stopping_.load()) {
            CloseHandle(pipe);
            return;
        }
        if (!connected) {
            CloseHandle(pipe);
            continue;
        }

        slot->pipe = pipe;
        slot->connected.store(true);
        bool ok = SendCurrentPath(pipe);

        // Heartbeat loop: doubles as disconnect detection (a consumer
        // process closing/crashing shows up here as a failed write).
        while (ok && !stopping_.load()) {
            DWORD w = WaitForSingleObject(stopEvent_, kHeartbeatIntervalMs);
            if (w == WAIT_OBJECT_0) break; // stopping
            ok = SendCurrentPath(pipe);
        }

        slot->connected.store(false);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        slot->pipe = INVALID_HANDLE_VALUE;
        // Loop back and accept the next client on this slot.
    }
}

void NamedPipeServer::Stop() {
    stopping_.store(true);
    if (stopEvent_) SetEvent(stopEvent_);
    for (auto& slot : clients_) {
        if (slot->thread.joinable()) slot->thread.join();
    }
    clients_.clear();
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    if (pipeSecurityDescriptor_) {
        LocalFree(pipeSecurityDescriptor_);
        pipeSecurityDescriptor_ = nullptr;
    }
}
