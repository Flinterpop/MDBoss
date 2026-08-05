#include "SingleInstance.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace mdboss {
namespace {

const wchar_t* const kInstanceProp = L"MDBossCpp.Instance";
constexpr ULONG_PTR kCopyDataId = 0x4D44'4243;   // 'MDBC'

struct FindState {
    HWND found = nullptr;
    DWORD self = 0;
};

BOOL CALLBACK find_instance(HWND hwnd, LPARAM param)
{
    auto* state = reinterpret_cast<FindState*>(param);
    // Skip our own process, or a second launch would talk to itself.
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == state->self) {
        return TRUE;
    }
    if (GetPropW(hwnd, kInstanceProp) != nullptr) {
        state->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

}  // namespace

void mark_as_instance(void* hwnd)
{
    if (hwnd != nullptr) {
        SetPropW(static_cast<HWND>(hwnd), kInstanceProp,
                 reinterpret_cast<HANDLE>(1));
    }
}

unsigned long instance_message_id()
{
    return static_cast<unsigned long>(kCopyDataId);
}

bool forward_to_running(const std::string& path)
{
    FindState state;
    state.self = GetCurrentProcessId();
    EnumWindows(&find_instance, reinterpret_cast<LPARAM>(&state));
    if (state.found == nullptr) {
        return false;
    }

    // Bring it forward first: the user double-clicked a document and expects
    // to see it, not to have it open behind whatever they were doing.
    //
    // Hand over our foreground rights before anything else.  THIS process
    // holds them (Explorer, the foreground process, launched it); the
    // long-running instance does not, and without the grant both our
    // SetForegroundWindow on its behalf and its own Raise() inside
    // WM_COPYDATA can be quietly reduced to a taskbar flash.  ASFW_ANY
    // because the grant lapses at the next user input anyway.  A FALSE
    // return means we had no rights to give (launched from a background
    // process), in which case the flash is all Windows allows.
    AllowSetForegroundWindow(ASFW_ANY);
    if (IsIconic(state.found)) {
        ShowWindow(state.found, SW_RESTORE);
    }
    SetForegroundWindow(state.found);

    if (!path.empty()) {
        COPYDATASTRUCT data{};
        data.dwData = kCopyDataId;
        // The +1 carries the terminator; the receiver treats it as UTF-8.
        data.cbData = static_cast<DWORD>(path.size() + 1);
        data.lpData = const_cast<char*>(path.c_str());
        // SendMessage, not Post: lpData must stay valid until it is consumed,
        // and this process is about to exit.
        SendMessageW(state.found, WM_COPYDATA, 0,
                     reinterpret_cast<LPARAM>(&data));
    }
    return true;
}

}  // namespace mdboss
