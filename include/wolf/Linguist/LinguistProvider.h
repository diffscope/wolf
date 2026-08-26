#ifndef WOLF_LINGUISTPROVIDER_H
#define WOLF_LINGUISTPROVIDER_H

#include <memory>

#include <synthrt/Core/ContribInterpreter.h>

#include <wolf/wolf_global.h>

namespace wolf {

    /// Interprets and executes linguist contributions.
    class WOLF_EXPORT LinguistProvider : public srt::ContribInterpreter {
    public:
        ~LinguistProvider() = default;

        srt::Expected<std::unique_ptr<srt::ContribImportBinding>>
            createImportBinding(srt::ContribSpec &importer, const srt::ContribImport &declaration,
                                srt::ContribSpec &target,
                                std::unique_ptr<srt::ContribImportOptions> options) const override;

        /// Creates the execution factory used by an import of \a target.
        virtual srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
            createExecutiveFactory(srt::ContribImportBinding &binding) const = 0;

    protected:
        LinguistProvider() = default;
    };

}

#endif // WOLF_LINGUISTPROVIDER_H
