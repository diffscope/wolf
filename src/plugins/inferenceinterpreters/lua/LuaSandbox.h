#ifndef WOLF_LUASANDBOX_H
#define WOLF_LUASANDBOX_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <synthrt/Support/Expected.h>

namespace wolf::lua {

    /// One Lua interpreter running one package's script.
    ///
    /// The interpreter state is not shareable, so every executive owns a sandbox of its own. That
    /// is also why script bodies stay out of the resource cache: what would be shared is an
    /// execution context, not a read-only parse product.
    ///
    /// The sandbox loads the standard library and then removes everything that reaches outside the
    /// process: files, the operating system, the debug interface, and every way of loading more
    /// code. A language package is data, and data does not get to open files.
    class Sandbox {
    public:
        /// Compiles \a source and runs it, so that the functions it defines become callable.
        ///
        /// \a chunkName names the script in error messages.
        static srt::Expected<std::unique_ptr<Sandbox>> create(const std::string &source,
                                                              const std::string &chunkName);

        ~Sandbox();

        /// Asks the running script to stop.
        ///
        /// A word boundary is not enough here: a script can loop on its own, so the interpreter
        /// is also told to give up between instruction batches. The call it was in fails, and the
        /// caller reports the conversion as cancelled rather than as a wrong answer.
        void interrupt() noexcept;

        /// Clears a previous interrupt so the sandbox can be used again.
        void resume() noexcept;

        /// Whether the script defined a global function called \a name.
        bool hasFunction(const std::string &name) const;

        /// Calls a global function with one string and expects a list of strings back.
        srt::Expected<std::vector<std::string>> callForStrings(const std::string &name,
                                                               const std::string &argument) const;

        /// Calls a global function with a list of strings and expects a list of booleans of the
        /// same length back.
        srt::Expected<std::vector<bool>>
            callForFlags(const std::string &name, const std::vector<std::string> &argument) const;

    private:
        Sandbox();

        class Impl;
        std::unique_ptr<Impl> _impl;
    };

    /// Reads a script from disk.
    srt::Expected<std::string> readScript(const std::filesystem::path &path);

}

#endif // WOLF_LUASANDBOX_H
