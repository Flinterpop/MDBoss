#include "SingleInstance.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace mdboss {
namespace {

const wchar_t* const kInstanceProp = L"MDBossCpp.Instance";
constexpr ULONG_PTR kCopyDataId = 0x4D44'4243;   // 'MDBC'

// "Local\" scopes the name to the logon session, which is what "one window per
// user session" means -- two users on the same machine get one window each,
// not one between them.  The name is deliberately not the Python app's, for
// the reason given in the header.
const wchar_t* const kInstanceMutex = L"Local\\MDBossCpp.SingleInstance";

// How long a later launch waits for the first one's window.  Bounded (Rule of
// 10): the first instance may be slow, may be stuck, or may have died between
// claiming the slot and showing anything, and none of those may cost the user
// the document they just double-clicked.
constexpr int kWaitAttempts = 100;
constexpr DWORD kWaitStepMs = 100;   // 10 s in total

// Held open for the life of the process: the named object exists only while
// someone has a handle to it, which is exactly the lifetime wanted, and it
// makes the slot self-healing after a crash -- a dead process's handle is
// closed by the kernel, the name disappears, and the next launch claims it.
HANDLE g_slot = nullptr;

// True when THIS process owns the session's slot.
bool claim_slot()
{
    if (g_slot != nullptr) {
        return true;
    }
    SetLastError(ERROR_SUCCESS);
    // bInitialOwner FALSE: only the existence of the name is being tested, and
    // taking ownership would drag in abandoned-mutex handling for no gain.
    g_slot = CreateMutexW(nullptr, FALSE, kInstanceMutex);
    if (g_slot == nullptr) {
        // Cannot claim it at all.  Degrade to the old behaviour and open a
        // window: refusing to start would be far worse than a second one.
        return true;
    }
    return GetLastError() != ERROR_ALREADY_EXISTS;
}

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

namespace {

// Hand the path over if an instance window exists RIGHT NOW.  Nothing waits
// here; the caller decides how long to keep looking.
bool try_forward(const std::string& path)
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

}  // namespace

bool forward_to_running(const std::string& path)
{
    // Claim first, look second.  The other order is the bug: two launches in
    // the same instant both looked, both found nothing, and both opened.
    if (claim_slot()) {
        return false;   // this process owns the session: open a window
    }

    // Someone else owns it.  Their window may not exist yet -- that gap is
    // precisely what the mutex covers and the property could not -- so wait
    // for it rather than concluding there is nobody there.
    for (int attempt = 0; attempt < kWaitAttempts; ++attempt) {
        if (try_forward(path)) {
            return true;
        }
        Sleep(kWaitStepMs);
    }

    // The slot is held but no window ever appeared: the owner is stuck, or
    // died between claiming and showing.  Open our own rather than exit and
    // take the user's document with us.
    return false;
}

}  // namespace mdboss
