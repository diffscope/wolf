#include <wolf/Language/LanguageProvider.h>

#include <utility>

#include <synthrt/Core/ContribImportBinding.h>

namespace wolf {

    namespace {

        class LanguageImportBinding : public srt::ContribImportBinding {
        public:
            LanguageImportBinding(srt::ContribSpec &importer, const srt::ContribImport &declaration,
                                  srt::ContribSpec &target,
                                  std::unique_ptr<srt::ContribImportOptions> options)
                : ContribImportBinding(importer, declaration, target, std::move(options)) {
            }

        protected:
            void activate() noexcept override {
            }

            void close() noexcept override {
            }

            srt::Expected<void> wait() override {
                return {};
            }
        };

    }

    srt::Expected<std::unique_ptr<srt::ContribImportBinding>> LanguageProvider::createImportBinding(
        srt::ContribSpec &importer, const srt::ContribImport &declaration, srt::ContribSpec &target,
        std::unique_ptr<srt::ContribImportOptions> options) const {
        return std::unique_ptr<srt::ContribImportBinding>(
            new LanguageImportBinding(importer, declaration, target, std::move(options)));
    }

}
