#include "WolfPipelineExecutive.h"

#include <algorithm>
#include <utility>

namespace wolf {

    WolfPipelineExecutive::WolfPipelineExecutive(srt::SingerSpec &spec, SingerLanguages languages)
        : Api::Linguist::L1::WolfPipelineExecutive(spec), m_languages(std::move(languages)) {
        m_handles.reserve(m_languages.entries.size());
        for (const auto &entry : m_languages.entries) {
            m_handles.push_back(entry.language);
        }
    }

    WolfPipelineExecutive::~WolfPipelineExecutive() = default;

    const std::vector<std::string> &WolfPipelineExecutive::languages() const {
        return m_handles;
    }

    srt::Expected<Api::Linguist::L1::LinguistExecutive *> WolfPipelineExecutive::createLinguist(
        std::string_view language,
        const Api::Linguist::L1::LinguistRuntimeOptions &runtimeOptions) {
        const auto entry = std::find_if(m_languages.entries.begin(), m_languages.entries.end(),
                                        [&](const SingerLanguage &candidate) {
                                            return candidate.language == language;
                                        });
        if (entry == m_languages.entries.end()) {
            return srt::Error(srt::Error::InvalidArgument,
                              "the singer does not declare the language " + std::string(language));
        }
        // The child comes from the import the language map points at, so the executive tree is
        // built entirely out of declared imports.
        auto child = createChild(entry->role, runtimeOptions);
        if (!child) {
            return child.takeError();
        }
        return static_cast<Api::Linguist::L1::LinguistExecutive *>(child.take());
    }

}
