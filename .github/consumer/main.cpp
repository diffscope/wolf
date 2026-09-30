// Includes the headers offered to a host and references one symbol from the library, so that the
// program links against the installed library.
//
// The package installs the Api, Linguist and Session headers. The wolf/Support headers are
// deliberately not installed, so an include of a wolf/Support header here causes the check to fail.

#include <synthrt/Core/SynthUnit.h>

#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistContrib.h>
#include <wolf/Session/LinguistSession.h>

int main() {
    srt::SynthUnit unit;
    wolf::linkLinguistCategory();
    const wolf::LinguistSession session(unit);
    // No package is loaded, so the return value is irrelevant. The call verifies that the program
    // links and that the headers are at the locations the package exports.
    (void) session.catalog();
    return 0;
}
