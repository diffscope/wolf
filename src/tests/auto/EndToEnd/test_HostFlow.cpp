#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <synthrt/Core/ContribSpecExtension.h>
#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistContrib.h>
#include <wolf/Session/LinguistSession.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;
namespace LinguistApi = wolf::Api::Linguist::L1;

namespace {

    /// The voicebank shaped fixture, built by scripts/make-voicebank-fixture.py out of the real
    /// converted language packages. Both have to be there: the voicebank supplies the syllable
    /// dictionary and the onset rules, the language packages supply the G2P.
    using wolf::test::convertedRoot;
    using wolf::test::voicebankRoot;

    struct DataOrSkip {
        DataOrSkip() {
            if (!fs::is_directory(voicebankRoot() / "wolf-voicebank-zh") ||
                !fs::is_directory(convertedRoot() / "wolf-g2p-pinyin")) {
                wolf::test::skip("no converted packages or voicebank fixture; set "
                                 "WOLF_LANG_PACKAGES_SOURCE and WOLF_VOICEBANK_FIXTURE_SOURCE");
            }
        }
    };

    /// One note as a host holds it, before and after each stage.
    struct Note {
        std::string language;
        std::string lyric;

        /// Written by the pronunciation stage, and editable by the user before the phoneme stage.
        std::string pronunciation;
        std::vector<std::string> candidates;

        std::vector<std::string> phonemes;
        std::vector<bool> onsets;
        bool converted = false;
    };

    /// What a host does with the domain, in the order a host does it.
    ///
    /// Modelled on the shape lite has: two passes over the same notes, with the user free to edit
    /// the pronunciation in between, and everything the domain does not answer written here.
    ///
    /// This is not part of wolf and is not meant to become part of it. It is written out so the
    /// domain can be judged by what it leaves a host to do: everything below that is not a project
    /// decision is work every host would have to repeat, which is what the session layer is for.
    class Host {
    public:
        explicit Host(srt::SynthUnit &unit) : m_unit(unit) {
        }

        srt::Expected<void> open(const fs::path &package) {
            auto handle = m_unit.openPackage(package, srt::SynthUnit::Load);
            if (!handle) {
                return handle.takeError();
            }
            m_package = handle.take();
            auto *singer = m_package->contribution("singer", "zh");
            if (singer == nullptr) {
                return srt::Error(srt::Error::InvalidArgument, "no singer contribution");
            }
            auto *base =
                srt::ContribSpecExtension::findFromSpec<LinguistApi::WolfPipelineExecutive>(
                    *singer->as<srt::SingerSpec>());
            m_extension = base ? base->as<LinguistApi::WolfPipelineExtension>() : nullptr;
            if (m_extension == nullptr) {
                return srt::Error(srt::Error::InvalidArgument, "no pipeline extension");
            }
            LinguistApi::WolfPipelineRuntimeOptions options;
            auto pipeline = m_extension->createPipeline(options);
            if (!pipeline) {
                return pipeline.takeError();
            }
            m_pipeline = pipeline.take();
            return {};
        }

        const std::vector<std::string> &languages() const {
            return m_extension->languages();
        }

        std::string defaultLanguage() const {
            return m_extension->defaultLanguage();
        }

        /// The readiness question a host asks before it lets the user type in a language.
        ///
        /// Two caches, because a host that asks the domain twice pays twice: one for what came
        /// back, one for what did not. Today the only way to answer the question at all is to
        /// build the executive, so readiness and the pool are one call.
        bool ensureLanguageReady(const std::string &language) {
            const auto ready = m_ready.find(language);
            if (ready != m_ready.end()) {
                return true;
            }
            if (m_failed.count(language) != 0) {
                return false;
            }
            LinguistApi::LinguistRuntimeOptions options;
            auto created = m_pipeline->as<LinguistApi::WolfPipelineExecutive>()->createLinguist(
                language, options);
            if (!created) {
                m_failed.insert(language);
                ++m_creations;
                return false;
            }
            m_ready.emplace(language, *created);
            ++m_creations;
            return true;
        }

        /// How many times the host went to the domain for an executive. A host that reasks is a
        /// host paying to load the same dictionaries again, which is why it is counted.
        int creations() const {
            return m_creations;
        }

        /// Stage A. Fills in the pronunciation of every note that needs one.
        void convertPronunciation(std::vector<Note> &notes) {
            run(notes, LinguistApi::Depth::Pronunciation, false);
        }

        /// Stage B. Turns the pronunciation each note now carries into phonemes and onsets.
        ///
        /// The pronunciation is sent back in rather than recomputed, because by now the user may
        /// have edited it and their edit is the input.
        void convertPhonemes(std::vector<Note> &notes) {
            run(notes, LinguistApi::Depth::Onsets, true);
        }

    private:
        /// True when the host answers this note itself and never sends it to a language.
        ///
        /// The first two are the ecosystem's reserved markers; the rest are project notation —
        /// a slur, a syllable continuation — that means nothing to a language at all.
        static bool reserved(const Note &note) {
            return note.lyric == "SP" || note.lyric == "AP";
        }

        static bool projectMarker(const Note &note) {
            if (note.lyric == "-") {
                return true;
            }
            return !note.lyric.empty() && note.lyric.find_first_not_of('+') == std::string::npos;
        }

        void run(std::vector<Note> &notes, LinguistApi::Depth depth, bool sendPronunciation) {
            std::map<std::string, std::vector<std::size_t>> byLanguage;

            for (std::size_t index = 0; index < notes.size(); ++index) {
                auto &note = notes[index];
                if (reserved(note)) {
                    note.pronunciation = note.lyric;
                    note.candidates = {note.lyric};
                    // A reserved marker is one phoneme that starts its own syllable. The domain
                    // does not say so; every host decides this again.
                    note.phonemes = {note.lyric};
                    note.onsets = {true};
                    note.converted = true;
                    continue;
                }
                if (projectMarker(note)) {
                    note.pronunciation = note.lyric;
                    note.candidates = {note.lyric};
                    note.phonemes.clear();
                    note.onsets.clear();
                    note.converted = true;
                    continue;
                }
                byLanguage[note.language].push_back(index);
            }

            for (const auto &[language, positions] : byLanguage) {
                if (!ensureLanguageReady(language)) {
                    for (const auto position : positions) {
                        notes[position].converted = false;
                    }
                    continue;
                }

                LinguistApi::LinguistConvertInput input;
                input.depth = depth;
                for (const auto position : positions) {
                    LinguistApi::LinguistWordInput word;
                    // Trailing syllable markers are project notation on an ordinary word, so they
                    // come off before the word is sent and the word itself still converts.
                    word.lyric = notes[position].lyric;
                    while (!word.lyric.empty() && word.lyric.back() == '+') {
                        word.lyric.pop_back();
                    }
                    if (sendPronunciation && !notes[position].pronunciation.empty()) {
                        word.pronunciation = notes[position].pronunciation;
                    }
                    input.words.push_back(std::move(word));
                }

                auto converted = m_ready.at(language)->start(input);
                if (!converted) {
                    for (const auto position : positions) {
                        notes[position].converted = false;
                    }
                    continue;
                }
                // A batch that comes back the wrong length would scatter every later note onto the
                // wrong note, so it is refused rather than trusted.
                if ((*converted)->words.size() != positions.size()) {
                    for (const auto position : positions) {
                        notes[position].converted = false;
                    }
                    continue;
                }
                for (std::size_t slot = 0; slot < positions.size(); ++slot) {
                    const auto &word = (*converted)->words[slot];
                    auto &note = notes[positions[slot]];
                    note.pronunciation = word.pronunciation;
                    note.candidates = word.candidates;
                    note.phonemes = word.phonemes;
                    note.onsets = word.onsets;
                    note.converted = word.error == wolf::Api::G2P::L1::Error::None;
                }
            }
        }

        srt::SynthUnit &m_unit;
        std::optional<srt::PackageHandle> m_package;
        LinguistApi::WolfPipelineExtension *m_extension = nullptr;
        std::unique_ptr<srt::SingerPipelineExecutive> m_pipeline;
        std::map<std::string, LinguistApi::LinguistExecutive *> m_ready;
        std::set<std::string> m_failed;
        int m_creations = 0;
    };

    void configure(srt::SynthUnit &unit) {
        wolf::test::configure(unit, {voicebankRoot(), convertedRoot()});
    }

    /// Opens the fixture, or fails the case. Returns nothing useful; the host is the point.
    void openFixture(Host &host) {
        auto opened = host.open(voicebankRoot() / "wolf-voicebank-zh");
        if (!opened) {
            BOOST_FAIL("the voicebank should have loaded: " + opened.error().toString());
        }
    }

}

BOOST_TEST_GLOBAL_FIXTURE(DataOrSkip);

BOOST_AUTO_TEST_SUITE(test_HostFlow)

/// What the voicebank declares, before anything is converted. A host reads this to know which
/// languages it may offer, and it has to be answerable without loading a model.
BOOST_AUTO_TEST_CASE(test_HostFlow_DeclaresItsLanguages) {
    srt::SynthUnit unit;
    configure(unit);

    Host host(unit);
    openFixture(host);

    BOOST_REQUIRE_EQUAL(host.languages().size(), 2u);
    BOOST_CHECK_EQUAL(host.defaultLanguage(), "cmn");

    BOOST_CHECK(host.ensureLanguageReady("cmn"));
    BOOST_CHECK(host.ensureLanguageReady("yue"));
    // A language this voicebank does not declare is refused rather than half built.
    BOOST_CHECK(!host.ensureLanguageReady("eng"));

    // Asked twice, built once — including the one that failed, which is the cache a host has to
    // keep for itself today.
    BOOST_CHECK(host.ensureLanguageReady("cmn"));
    BOOST_CHECK(!host.ensureLanguageReady("eng"));
    BOOST_CHECK_EQUAL(host.creations(), 3);
}

/// The whole path against the real resources, in the order a host walks it: pronunciation first,
/// phonemes second, with a mixed language line and the markers a project carries.
BOOST_AUTO_TEST_CASE(test_HostFlow_ConvertsALineInTwoStages) {
    srt::SynthUnit unit;
    configure(unit);

    Host host(unit);
    openFixture(host);

    // 我 唱 SP 歌(yue) -, a line with both languages, a reserved marker and a slur.
    std::vector<Note> notes = {
        {"cmn", "\xE6\x88\x91"},
        {"cmn", "\xE5\x94\xB1"},
        {"cmn", "SP"          },
        {"yue", "\xE6\xAD\x8C"},
        {"cmn", "-"           },
    };

    host.convertPronunciation(notes);
    BOOST_CHECK_EQUAL(notes[0].pronunciation, "wo");
    BOOST_CHECK_EQUAL(notes[1].pronunciation, "chang");
    BOOST_CHECK_EQUAL(notes[2].pronunciation, "SP");
    BOOST_CHECK(!notes[3].pronunciation.empty());
    BOOST_CHECK_EQUAL(notes[4].pronunciation, "-");
    BOOST_TEST_MESSAGE("yue 歌 -> " + notes[3].pronunciation);

    // Stage A stops at the pronunciation, so nothing below it exists yet.
    BOOST_CHECK(notes[0].phonemes.empty());
    BOOST_CHECK(notes[1].phonemes.empty());

    host.convertPhonemes(notes);

    // The voicebank's own dictionary split the syllables and its own rules marked the onsets.
    // Neither could have come from a language package: cmn and yue carry a G2P and nothing else.
    BOOST_REQUIRE_EQUAL(notes[0].phonemes.size(), 2u);
    BOOST_CHECK_EQUAL(notes[0].phonemes[0], "w");
    BOOST_CHECK_EQUAL(notes[0].phonemes[1], "o");
    BOOST_REQUIRE_EQUAL(notes[1].phonemes.size(), 2u);
    BOOST_CHECK_EQUAL(notes[1].phonemes[0], "ch");
    BOOST_CHECK_EQUAL(notes[1].phonemes[1], "ang");
    BOOST_REQUIRE_EQUAL(notes[1].onsets.size(), 2u);
    BOOST_CHECK(notes[1].onsets[0]);
    BOOST_CHECK(!notes[1].onsets[1]);

    // The reserved marker became one phoneme of its own; the slur became none.
    BOOST_REQUIRE_EQUAL(notes[2].phonemes.size(), 1u);
    BOOST_CHECK_EQUAL(notes[2].phonemes[0], "SP");
    BOOST_CHECK(notes[4].phonemes.empty());

    // Cantonese, out of the other package, in the same line and the same process.
    BOOST_CHECK(!notes[3].phonemes.empty());
    BOOST_CHECK_EQUAL(notes[3].phonemes.size(), notes[3].onsets.size());
}

/// The reason the two stages are separate at all: between them the user may rewrite a reading, and
/// the phoneme layer has to follow what they wrote rather than what the dictionary said.
BOOST_AUTO_TEST_CASE(test_HostFlow_FollowsAnEditedPronunciation) {
    srt::SynthUnit unit;
    configure(unit);

    Host host(unit);
    openFixture(host);

    // 行 reads xing on its own and hang in a bank's name; only the user knows which this is.
    std::vector<Note> notes = {
        {"cmn", "\xE8\xA1\x8C"}
    };
    host.convertPronunciation(notes);
    BOOST_CHECK_EQUAL(notes[0].pronunciation, "xing");

    notes[0].pronunciation = "hang";
    host.convertPhonemes(notes);

    BOOST_CHECK_EQUAL(notes[0].pronunciation, "hang");
    BOOST_REQUIRE_EQUAL(notes[0].phonemes.size(), 2u);
    BOOST_CHECK_EQUAL(notes[0].phonemes[0], "h");
    BOOST_CHECK_EQUAL(notes[0].phonemes[1], "ang");
}

/// Every syllable the language's G2P can produce has to be in the voicebank's dictionary, or an
/// ordinary word converts to a reading and then to nothing at all. A spread across the retroflexes,
/// the compound finals and the ü finals is where a split rule goes wrong first.
BOOST_AUTO_TEST_CASE(test_HostFlow_CoversTheSyllablesTheG2PProduces) {
    srt::SynthUnit unit;
    configure(unit);

    Host host(unit);
    openFixture(host);

    std::vector<Note> notes;
    for (const auto *word : {
             "\xE4\xB8\xAD", // zhong
             "\xE5\x9B\xBD", // guo
             "\xE9\x9F\xB3", // yin
             "\xE4\xB9\x90", // yue
             "\xE5\xAD\xA6", // xue
             "\xE9\x99\xA2", // yuan
             "\xE5\xA5\xB3", // nv
             "\xE5\x84\xBF", // er
         }) {
        notes.push_back({"cmn", word});
    }

    host.convertPronunciation(notes);
    host.convertPhonemes(notes);

    for (const auto &note : notes) {
        BOOST_REQUIRE_MESSAGE(!note.pronunciation.empty(), "a word converted to nothing");
        // This is the assertion a missing dictionary entry breaks: a reading is there and the
        // layer under it is empty.
        BOOST_CHECK_MESSAGE(!note.phonemes.empty(), "no phonemes for " + note.pronunciation);
        BOOST_CHECK_EQUAL(note.phonemes.size(), note.onsets.size());
        BOOST_CHECK(note.converted);
    }
}

/// A word carrying trailing syllable markers is an ordinary word with project notation on it. The
/// markers come off before it is sent, so the word still converts, while a note that is nothing
/// but markers never goes near a language.
BOOST_AUTO_TEST_CASE(test_HostFlow_StripsProjectNotationFromOrdinaryWords) {
    srt::SynthUnit unit;
    configure(unit);

    Host host(unit);
    openFixture(host);

    std::vector<Note> notes = {
        {"cmn", "\xE5\x94\xB1+"},
        {"cmn", "++"           }
    };
    host.convertPronunciation(notes);
    host.convertPhonemes(notes);

    BOOST_CHECK_EQUAL(notes[0].pronunciation, "chang");
    BOOST_CHECK_EQUAL(notes[0].phonemes.size(), 2u);
    BOOST_CHECK_EQUAL(notes[1].pronunciation, "++");
    BOOST_CHECK(notes[1].phonemes.empty());
}

/// The same line, the same resources, through the session layer instead of by hand.
///
/// This is the case for that layer, stated as a test rather than as an argument: everything the
/// Host class above does that is not a project decision is gone. No readiness cache, no failure
/// cache, no pool, no answer for SP written out again — and the results are the same ones.
///
/// What is left here is what genuinely belongs to a host: the slur and the syllable markers,
/// which are lite's engineering model and mean nothing to a language.
BOOST_AUTO_TEST_CASE(test_HostFlow_TheSameLineThroughTheSession) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = unit.openPackage(voicebankRoot() / "wolf-voicebank-zh", srt::SynthUnit::Load);
    if (!package) {
        BOOST_FAIL("the voicebank should have loaded: " + package.error().toString());
    }

    wolf::LinguistSession session(unit);
    const wolf::SingerRef singer{srt::ContribLocator(package->id(), "singer", "zh"),
                                 package->version()};

    // The catalog answers what the hand written host had to reach into the extension for.
    const auto *entry = session.catalog()->find(singer);
    BOOST_REQUIRE(entry != nullptr);
    BOOST_CHECK_EQUAL(entry->defaultLanguage, "cmn");
    BOOST_REQUIRE_EQUAL(entry->languages.size(), 2u);
    BOOST_CHECK(session.probe(singer, "cmn").readiness == wolf::Readiness::Cold);
    BOOST_CHECK(session.probe(singer, "eng").readiness == wolf::Readiness::Unavailable);

    // Stage A. SP goes in with the rest: the session answers it, the language never sees it.
    LinguistApi::LinguistConvertInput first;
    first.depth = LinguistApi::Depth::Pronunciation;
    for (const auto *lyric : {"\xE6\x88\x91", "\xE5\x94\xB1", "SP"}) {
        first.words.push_back({lyric, std::nullopt, std::nullopt});
    }
    auto pronunciations = session.convert(singer, "cmn", first);
    if (!pronunciations) {
        BOOST_FAIL("stage A should have run: " + pronunciations.error().toString());
    }
    BOOST_REQUIRE_EQUAL((*pronunciations)->words.size(), 3u);
    BOOST_CHECK_EQUAL((*pronunciations)->words[0].pronunciation, "wo");
    BOOST_CHECK_EQUAL((*pronunciations)->words[1].pronunciation, "chang");
    BOOST_CHECK_EQUAL((*pronunciations)->words[2].pronunciation, "SP");
    BOOST_CHECK((*pronunciations)->words[2].mode == wolf::Api::G2P::L1::Mode::Copy);

    // Converting warmed it, so stage B loads nothing.
    BOOST_CHECK(session.probe(singer, "cmn").readiness == wolf::Readiness::Ready);

    // Stage B, with the user's edit carried in as a pinned pronunciation.
    LinguistApi::LinguistConvertInput second;
    second.depth = LinguistApi::Depth::Onsets;
    second.words.push_back({"\xE6\x88\x91", std::string("wo"), std::nullopt});
    second.words.push_back({"\xE5\x94\xB1", std::string("chang"), std::nullopt});
    second.words.push_back({"SP", std::nullopt, std::nullopt});
    auto phonemes = session.convert(singer, "cmn", second);
    BOOST_REQUIRE(phonemes);
    const auto &words = (*phonemes)->words;
    BOOST_REQUIRE_EQUAL(words.size(), 3u);

    BOOST_REQUIRE_EQUAL(words[0].phonemes.size(), 2u);
    BOOST_CHECK_EQUAL(words[0].phonemes[0], "w");
    BOOST_CHECK_EQUAL(words[1].phonemes[0], "ch");
    BOOST_CHECK_EQUAL(words[1].phonemes[1], "ang");
    BOOST_CHECK(words[1].onsets[0]);
    BOOST_CHECK(!words[1].onsets[1]);
    // The marker came back with the shape the depth asks for, without going near the dictionary.
    BOOST_REQUIRE_EQUAL(words[2].phonemes.size(), 1u);
    BOOST_CHECK_EQUAL(words[2].phonemes[0], "SP");
    BOOST_CHECK(words[2].onsets[0]);

    // The other language out of the other package, on the same session.
    LinguistApi::LinguistConvertInput cantonese;
    cantonese.depth = LinguistApi::Depth::Onsets;
    cantonese.words.push_back({"\xE6\xAD\x8C", std::nullopt, std::nullopt});
    auto yue = session.convert(singer, "yue", cantonese);
    BOOST_REQUIRE(yue);
    BOOST_CHECK(!(*yue)->words[0].pronunciation.empty());
    BOOST_CHECK(!(*yue)->words[0].phonemes.empty());
}

BOOST_AUTO_TEST_SUITE_END()
