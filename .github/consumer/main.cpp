// Includes the headers offered to a host, references symbols from the library so that the program
// links against the installed library, and checks the installed plugin tree.
//
// The package installs the Api, Linguist, Session and Support headers and the root headers.

#include <cstdio>
#include <filesystem>

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

    // No link step detects a missing plugin, so the category directories that the imported plugin
    // targets of the package name are checked here. They are the directories a host adds.
    for (const char *directory :
         {WOLF_CONSUMER_LINGUIST_PLUGIN_DIR, WOLF_CONSUMER_INFERENCE_PLUGIN_DIR}) {
        if (!std::filesystem::is_directory(directory)) {
            std::fprintf(stderr, "the wolf plugin directory %s does not exist\n", directory);
            return 1;
        }
        std::printf("wolf plugins: %s\n", directory);
    }
    return 0;
}
