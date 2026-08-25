#include <wolf/Language/LanguageInterpreter.h>

#include <utility>

#include <synthrt/Core/ContribImportBinding.h>

#include <wolf/Api/Languages/Language/1/LanguageApiL1.h>

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

    srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
        LanguageInterpreter::createImportOptions(const srt::ContribSpec &target,
                                                 const srt::JsonValue &manifestOptions) const {
        if (!manifestOptions.isObject()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "language import options must be an object");
        }
        if (target.interface() != Api::Language::L1::API_INTERFACE ||
            target.variant() != Api::Language::L1::API_VARIANT ||
            target.level() != Api::Language::L1::API_LEVEL) {
            return srt::Error(srt::Error::InvalidArgument,
                              "language import target has an unsupported contract");
        }
        return std::unique_ptr<srt::ContribImportOptions>(
            new Api::Language::L1::LanguageImportOptions());
    }

    srt::Expected<std::unique_ptr<srt::ContribImportBinding>>
        LanguageInterpreter::createImportBinding(
            srt::ContribSpec &importer, const srt::ContribImport &declaration,
            srt::ContribSpec &target, std::unique_ptr<srt::ContribImportOptions> options) const {
        return std::unique_ptr<srt::ContribImportBinding>(
            new LanguageImportBinding(importer, declaration, target, std::move(options)));
    }

}
