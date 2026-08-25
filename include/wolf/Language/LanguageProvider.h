#ifndef WOLF_LANGUAGEPROVIDER_H
#define WOLF_LANGUAGEPROVIDER_H

#include <memory>

#include <synthrt/Core/ContribInterpreter.h>

#include <wolf/wolf_global.h>

namespace wolf {

    /// Interprets and executes language contributions.
    class WOLF_EXPORT LanguageProvider : public srt::ContribInterpreter {
    public:
        ~LanguageProvider() = default;

        srt::Expected<std::unique_ptr<srt::ContribImportBinding>>
            createImportBinding(srt::ContribSpec &importer, const srt::ContribImport &declaration,
                                srt::ContribSpec &target,
                                std::unique_ptr<srt::ContribImportOptions> options) const override;

        /// Creates the execution factory used by an import of \a target.
        virtual srt::Expected<std::unique_ptr<srt::ContribExecFactory>>
            createExecFactory(srt::ContribImportBinding &binding) const = 0;

    protected:
        LanguageProvider() = default;
    };

}

#endif // WOLF_LANGUAGEPROVIDER_H
