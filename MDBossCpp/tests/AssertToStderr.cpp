// Make a failing assertion FAIL the test run instead of hanging it.
//
// The C runtime's default for a failed assert in a Debug build is a modal
// dialog.  Under ctest nothing is there to dismiss it, so the suite stops dead
// with no output and no exit code -- one assertion took the whole Debug run
// past five minutes before it was killed by hand, and the only clue was a
// window titled "Microsoft Visual C++ Runtime Library".  A hanging suite is
// worse than a failing one: a failure names the test.
//
// Routing the report to stderr turns that into an abort with a message, which
// ctest records as a normal failure against the test that caused it.
//
// This is what makes assertions worth adding at all.  They are compiled out in
// Release (NDEBUG), so Debug is the ONLY configuration in which one is ever
// checked -- and a configuration that cannot be run is a configuration in
// which they are decoration.

#ifdef _DEBUG

#include <crtdbg.h>
#include <cstdlib>

namespace {

struct RouteAssertsToStderr {
    RouteAssertsToStderr()
    {
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        // No "abort() has been called" popup either.
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    }
};

// Constructed before main(), so it is in force for every test.
const RouteAssertsToStderr g_routed;

}  // namespace

#endif  // _DEBUG
