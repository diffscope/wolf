#include "Files.h"

#include <string>

#include <stdcorelib/path.h>

namespace fs = std::filesystem;

namespace wolf {

    srt::Error::ErrorCode fileErrorCode(const fs::path &path) {
        std::error_code status;
        return fs::exists(path, status) ? srt::Error::FileNotOpen : srt::Error::FileNotFound;
    }

    srt::Error fileError(const fs::path &path, std::string_view what) {
        const auto code = fileErrorCode(path);
        if (code == srt::Error::FileNotFound) {
            return srt::Error(code, std::string(what) + " not found: " + stdc::path::to_utf8(path));
        }
        return srt::Error(code, "failed to open the " + std::string(what) + ": " +
                                    stdc::path::to_utf8(path));
    }

    void stripLineDecorations(std::string &line, bool first) {
        if (first && line.rfind("\xEF\xBB\xBF", 0) == 0) {
            line.erase(0, 3);
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
    }

    srt::Expected<void> openForReading(std::ifstream &file, const fs::path &path,
                                       std::string_view what, std::ios::openmode mode) {
        std::error_code status;
        if (fs::is_directory(path, status)) {
            return fileError(path, what);
        }
        file.open(path, mode);
        if (!file.is_open()) {
            return fileError(path, what);
        }
        return {};
    }

}
