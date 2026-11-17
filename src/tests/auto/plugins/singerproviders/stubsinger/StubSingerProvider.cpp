#include "StubSingerProvider.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <synthrt/SVS/SingerProvider.h>

namespace wolf::stub {

    StubSingerProvider::StubSingerProvider(srt::ContribTriplet triplet)
        : m_triplet(std::move(triplet)) {
    }

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        StubSingerProvider::createConfiguration(const srt::ContribSpec &) const {
        return std::unique_ptr<srt::ContribConfiguration>(new Configuration(m_triplet));
    }

}
