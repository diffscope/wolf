#include "MandarinProvider.h"

#include <string>
#include <vector>

#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

#include <wolf/Api/Languages/Mandarin/1/MandarinApiL1.h>

namespace wolf {

    namespace Cmn = Api::Mandarin::L1;

    // Collects every complaint about a manifest before reporting, so a package with four mistakes
    // in it takes one round to fix rather than four.
    namespace {

        class ErrorCollector {
        public:
            void add(std::string message) {
                _messages.push_back(std::move(message));
            }

            bool hasErrors() const {
                return !_messages.empty();
            }

            std::string message(std::string_view prefix) const {
                std::string res = stdc::formatN("%1 (%2 errors found):\n", prefix,
                                                std::to_string(_messages.size()));
                for (size_t i = 0; i < _messages.size(); ++i) {
                    res += std::to_string(i + 1);
                    res += ". ";
                    res += _messages[i];
                    if (i + 1 != _messages.size()) {
                        res += ";\n";
                    }
                }
                return res;
            }

        private:
            std::vector<std::string> _messages;
        };

        /// Reads a path field, resolved against the directory the manifest lives in.
        void readPath(std::filesystem::path *out, const srt::JsonObject &obj, std::string_view key,
                      const std::filesystem::path &basePath, bool required, ErrorCollector *ec) {
            auto it = obj.find(std::string(key));
            if (it == obj.end()) {
                if (required) {
                    ec->add(stdc::formatN(R"(string field "%1" is missing)", key));
                }
                return;
            }
            if (!it->second.isString()) {
                ec->add(stdc::formatN(R"(string field "%1" type mismatch)", key));
                return;
            }
            *out =
                stdc::path::clean_path(basePath / stdc::path::from_utf8(it->second.toStringView()));
        }

    }

    MandarinProvider::MandarinProvider() = default;

    MandarinProvider::~MandarinProvider() = default;

    int MandarinProvider::apiLevel() const {
        return Cmn::API_LEVEL;
    }

    srt::Expected<srt::UNO<LanguageSchema>>
        MandarinProvider::createSchema(const LanguageSpec *spec) const {
        if (!spec) {
            // fatal error: null pointer, return immediately
            return srt::Error{
                srt::Error::InvalidArgument,
                "fatal in createSchema: LanguageSpec is nullptr",
            };
        }

        const auto &schema = spec->manifestSchema();
        auto result = srt::UNO<Cmn::MandarinSchema>::create();

        ErrorCollector ec;

        // [REQUIRED] phonemes, string[]
        {
            auto it = schema.find("phonemes");
            if (it == schema.end()) {
                ec.add(R"(array field "phonemes" is missing)");
            } else if (!it->second.isArray()) {
                ec.add(R"(array field "phonemes" type mismatch)");
            } else {
                const auto &arr = it->second.toArray();
                result->phonemes.reserve(arr.size());
                for (size_t i = 0; i < arr.size(); ++i) {
                    if (!arr[i].isString()) {
                        ec.add(stdc::formatN(R"(entry %1 of "phonemes" is not a string)",
                                             std::to_string(i + 1)));
                        continue;
                    }
                    result->phonemes.emplace_back(arr[i].toString());
                }
                if (result->phonemes.empty() && !ec.hasErrors()) {
                    ec.add(R"(array field "phonemes" is empty)");
                }
            }
        } // phonemes

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.message("error parsing mandarin schema"),
            };
        }
        return result;
    }

    srt::Expected<srt::UNO<LanguageConfiguration>>
        MandarinProvider::createConfiguration(const LanguageSpec *spec) const {
        if (!spec) {
            // fatal error: null pointer, return immediately
            return srt::Error{
                srt::Error::InvalidArgument,
                "fatal in createConfiguration: LanguageSpec is nullptr",
            };
        }

        const auto &config = spec->manifestConfiguration();
        auto result = srt::UNO<Cmn::MandarinConfiguration>::create();

        ErrorCollector ec;

        // [REQUIRED] dict, path
        readPath(&result->dict, config, "dict", spec->path(), true, &ec);

        // [OPTIONAL] extraDict, path
        readPath(&result->extraDict, config, "extraDict", spec->path(), false, &ec);

        // [OPTIONAL] useTone, bool
        {
            auto it = config.find("useTone");
            if (it != config.end()) {
                if (!it->second.isBool()) {
                    ec.add(R"(bool field "useTone" type mismatch)");
                } else {
                    result->useTone = it->second.toBool();
                }
            }
        } // useTone

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.message("error parsing mandarin configuration"),
            };
        }
        return result;
    }

}
