#ifndef WOLF_API_LINGUISTS_LINGUIST_1_LINGUISTWOLFAPIL1_H
#define WOLF_API_LINGUISTS_LINGUIST_1_LINGUISTWOLFAPIL1_H

#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>

namespace wolf::Api::Linguist::L1::Wolf {
    /// Identifies the wolf implementation of the linguist contract.
    inline const srt::ContribContractTriplet Triplet{Contract, "wolf"};

    /// The wolf variant configuration for a linguist composition.
    class Configuration : public srt::ContribConfiguration {
    public:
        Configuration() : ContribConfiguration(Triplet) {
        }
    };
}

#endif // WOLF_API_LINGUISTS_LINGUIST_1_LINGUISTWOLFAPIL1_H
