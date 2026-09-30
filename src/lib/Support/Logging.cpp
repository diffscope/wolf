#include "Logging.h"

namespace wolf {

    srt::LogCategory &logCategory() {
        static srt::LogCategory category("wolf");
        return category;
    }

}
