#ifndef WOLF_LUASANDBOX_H
#define WOLF_LUASANDBOX_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <synthrt/Support/Expected.h>

namespace wolf::lua {

    /// One Lua interpreter that runs the script of one package.
    ///
    /// The interpreter state cannot be shared, so every executive owns a separate sandbox. For the
    /// same reason, script bodies are not stored in the resource cache: the shared object would be
    /// an execution context rather than a read-only parse result.
    ///
    /// The sandbox loads the standard library and then removes every facility that reaches outside
    /// the process: files, the operating system, the debug interface, and every mechanism for
    /// loading additional code. A language package is data, and data must not open files.
    class Sandbox {
    public:
        /// Compiles \a source and runs it, so that the functions it defines become callable.
        ///
        /// \a chunkName names the script in error messages.
        ///
        /// \return the sandbox; a FeatureNotSupported error if the interpreter cannot be created;
        /// an InvalidFormat error if \a source does not compile or fails to run.
        static srt::Expected<std::unique_ptr<Sandbox>> create(const std::string &source,
                                                              const std::string &chunkName);

        ~Sandbox();

        /// Requests that the running script stop.
        ///
        /// A check at word boundaries is insufficient because a script can loop indefinitely, so
        /// the interpreter also checks the request between instruction batches. The interrupted
        /// call fails, and the caller reports the conversion as cancelled rather than as an
        /// incorrect result.
        void interrupt() noexcept;

        /// Clears a previous interrupt so the sandbox can be used again.
        void resume() noexcept;

        /// Returns whether the script defines a global function named \a name.
        bool hasFunction(const std::string &name) const;

        /// Calls the global function \a name with one string argument.
        ///
        /// \return the elements of the returned table; an InvalidFormat error if the call fails,
        /// the function does not return a table, or the table holds an element that is not a
        /// string.
        srt::Expected<std::vector<std::string>> callForStrings(const std::string &name,
                                                               const std::string &argument) const;

        /// Calls the global function \a name with a table of strings.
        ///
        /// \return the elements of the returned table; an InvalidFormat error if the call fails,
        /// the function does not return a table, the table length differs from the size of
        /// \a argument, or the table holds an element that is not a boolean.
        srt::Expected<std::vector<bool>>
            callForFlags(const std::string &name, const std::vector<std::string> &argument) const;

    private:
        Sandbox();

        class Impl;
        std::unique_ptr<Impl> _impl;
    };

    /// Reads a script from disk.
    ///
    /// \return the script source; an error if the file cannot be opened; an InvalidFormat error
    /// if the file is empty.
    srt::Expected<std::string> readScript(const std::filesystem::path &path);

}

#endif // WOLF_LUASANDBOX_H
