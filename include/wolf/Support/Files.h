#ifndef WOLF_FILES_H
#define WOLF_FILES_H

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

#include <synthrt/Support/Expected.h>


namespace wolf {

    /// Classifies a file that could not be read.
    ///
    /// A missing file and an existing unreadable file require different handling. The first
    /// indicates an incorrectly installed package, and the second indicates a problem on the host.
    /// Callers switch on the code, and the code is therefore determined by examining the path
    /// instead of being inferred from a failed open call, which reports both faults identically.
    ///
    /// \return srt::Error::FileNotFound if nothing exists at \a path, and srt::Error::FileNotOpen
    /// otherwise.
    srt::Error::ErrorCode fileErrorCode(const std::filesystem::path &path);

    /// Returns the error for a file that could not be read, with the code that fileErrorCode()
    /// determines and a message that names the file.
    ///
    /// \a what names the kind of file, such as "S2P dictionary". The message reads
    /// "<what> not found: <path>" or "failed to open the <what>: <path>".
    srt::Error fileError(const std::filesystem::path &path, std::string_view what);

    /// Removes the line decorations of the text resources that this family reads: a UTF-8 byte
    /// order mark at the start of the first line, and a carriage return at the end of any line.
    ///
    /// The upstream dictionaries contain both. If either remained, the first word, or every word,
    /// would differ from the string that a caller looks up. \a first indicates whether \a line is
    /// the first line of its file.
    void stripLineDecorations(std::string &line, bool first);

    /// Opens \a path for reading into \a file, or returns the error that fileError() constructs.
    ///
    /// A directory is rejected with FileNotOpen before the open call, because on some platforms
    /// opening a directory as a stream succeeds and every subsequent read fails, which a caller
    /// would interpret as an empty file.
    srt::Expected<void> openForReading(std::ifstream &file, const std::filesystem::path &path,
                                       std::string_view what,
                                       std::ios::openmode mode = std::ios::in);

}

#endif // WOLF_FILES_H
