#include <wolf/Support/ErrorCode.h>

#include <string>

namespace wolf {

    namespace {

        class WolfErrorCategory final : public std::error_category {
        public:
            const char *name() const noexcept override {
                return "wolf";
            }

            std::string message(int condition) const override {
                switch (static_cast<ErrorCode>(condition)) {
                    case ErrorCode::NoWorker:
                        return "no worker thread could be started";
                    case ErrorCode::ResourceInvalid:
                        return "the resource parser failed";
                }
                return "unknown wolf error";
            }
        };

    }

    const std::error_category &errorCategory() noexcept {
        static const WolfErrorCategory category;
        return category;
    }

}
