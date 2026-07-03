#include "graph/person_extractor.h"
#include "test_harness.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

void WriteLines(
    const std::filesystem::path& path,
    const std::vector<std::string>& lines
) {
    std::ofstream out(path);
    for (const auto& line : lines) {
        out << line << '\n';
    }
}

void WriteMap(
    const std::filesystem::path& path,
    const std::vector<std::pair<std::string, std::string>>& rows
) {
    std::ofstream out(path);
    for (const auto& [k, v] : rows) {
        out << k << '\t' << v << '\n';
    }
}

std::filesystem::path CreateDictionaryDir() {
    const auto seed = static_cast<long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    const auto dir = std::filesystem::temp_directory_path() /
                     ("concept-extractor-person-tests-" + std::to_string(seed));
    std::filesystem::create_directories(dir);

    WriteLines(
        dir / "person_forms.txt", {
                                      "алексей",
                                      "алексеем",
                                      "черников",
                                      "черниковым",
                                      "иван",
                                      "ивана",
                                      "иванов",
                                      "иванову",
                                      "александр",
                                      "дудка",
                                      "унковский",
                                      "унковскому",
                                      "владимир",
                                      "владимира",
                                      "путин",
                                      "путина",
                                      "евгений",
                                      "евгения",
                                  }
    );

    WriteLines(
        dir / "name_forms.txt", {
                                    "алексей",
                                    "алексеем",
                                    "иван",
                                    "ивана",
                                    "александр",
                                    "владимир",
                                    "владимира",
                                    "евгений",
                                    "евгения",
                                }
    );
    WriteLines(
        dir / "surname_forms.txt", {
                                       "черников",
                                       "черниковым",
                                       "иванов",
                                       "иванову",
                                       "дудка",
                                       "унковский",
                                       "унковскому",
                                       "путин",
                                       "путина",
                                   }
    );
    WriteLines(dir / "patron_forms.txt", {});

    WriteLines(
        dir / "geo_forms.txt",
        {"москве", "мочалово", "юхновского", "района", "владимир", "евгений"}
    );
    WriteLines(dir / "org_forms.txt", {"правительство"});
    WriteLines(dir / "trade_forms.txt", {});

    WriteLines(
        dir / "known_forms.txt",
        {
            "в",         "с",       "по",        "москве",    "мочалово",   "юхновского",
            "района",    "встреча", "проходит",  "алексеем",  "черниковым", "иван",
            "иванову",   "пришел",  "александр", "дудка",     "унковскому", "поручили",
            "селе",      "был",     "снег",      "приехал",   "встретился", "выступил",
            "президент", "сказал",  "сообщил",   "владимира", "путина",     "евгений",
            "евгения",   "товарищ",
        }
    );

    WriteLines(dir / "noun_forms.txt", {"встреча", "района", "снег", "президент"});
    WriteLines(dir / "adj_forms.txt", {"московский"});
    WriteLines(
        dir / "verb_forms.txt", {
                                    "проходит",
                                    "поручили",
                                    "приехал",
                                    "встретился",
                                    "выступил",
                                    "сказал",
                                    "сообщил",
                                }
    );

    WriteMap(
        dir / "lemma_map.txt", {
                                   {"алексеем", "алексей"},
                                   {"черниковым", "черников"},
                                   {"ивана", "иван"},
                                   {"иванову", "иванов"},
                                   {"унковскому", "унковский"},
                                   {"владимира", "владимир"},
                                   {"путина", "путин"},
                                   {"евгения", "евгений"},
                               }
    );
    WriteMap(
        dir / "lemma_map_full.txt", {
                                        {"алексеем", "алексей"},
                                        {"черниковым", "черников"},
                                        {"ивана", "иван"},
                                        {"иванову", "иванов"},
                                        {"унковскому", "унковский"},
                                        {"москве", "москва"},
                                        {"дудка", "дудка"},
                                        {"александр", "александр"},
                                        {"владимира", "владимир"},
                                        {"путина", "путин"},
                                        {"евгения", "евгений"},
                                    }
    );

    return dir;
}

std::vector<ExtractedConcept>
Extract(const PersonExtractor& extractor, const std::string& passage) {
    return extractor.ExtractPersons({passage});
}

std::unordered_set<std::string>
ExtractNormalized(const PersonExtractor& extractor, const std::string& passage) {
    std::unordered_set<std::string> norms;
    for (const auto& c : Extract(extractor, passage)) {
        norms.insert(c.normalized);
    }
    return norms;
}

bool Contains(const std::unordered_set<std::string>& items, std::string_view needle) {
    return items.count(std::string(needle)) > 0;
}

const ExtractedConcept*
FindByNorm(const std::vector<ExtractedConcept>& concepts, std::string_view normalized) {
    for (const auto& c : concepts) {
        if (c.normalized == normalized) {
            return &c;
        }
    }
    return nullptr;
}

bool Assert(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_fail;
        return false;
    }
    ++g_pass;
    return true;
}

}  // namespace

void RunPersonExtractorRulesTests() {
    const auto dict_dir = CreateDictionaryDir();
    const PersonExtractor extractor(dict_dir.string());
    const PersonExtractor extractor_with_custom_stop(dict_dir.string(), {"иван"});
    bool ok = true;

    {
        const auto got = ExtractNormalized(extractor, "В Москве проходит встреча.");
        ok &= Assert(
            got.empty(), "preposition/start sentence noise should not be extracted"
        );
    }
    {
        const auto concepts = Extract(extractor, "Встреча с Алексеем Черниковым.");
        const auto* c = FindByNorm(concepts, "алексе черников");
        ok &= Assert(c != nullptr, "person name should be extracted");
        ok &= Assert(
            c != nullptr && c->id == -6764516418740727974LL,
            "concept id should match Python hash_int64(blake2b-64)"
        );
        ok &= Assert(
            c != nullptr && c->original == "Алексеем Черниковым",
            "original span should preserve source text for person name"
        );
    }
    {
        const auto got = ExtractNormalized(extractor, "Также Иван Иванов пришел.");
        ok &= Assert(
            Contains(got, "ива иван"),
            "hard stop at sentence start should not block following person span"
        );
        ok &=
            Assert(!Contains(got, "такж"), "hard stop token should never be normalized");
    }
    {
        const auto got =
            ExtractNormalized(extractor, "В селе Мочалово Юхновского района был снег.");
        ok &= Assert(got.empty(), "geo span should be rejected");
    }
    {
        const auto got = ExtractNormalized(extractor, "ИВАН ИВАНОВ приехал.");
        ok &= Assert(got.empty(), "all-caps spans should be skipped");
    }
    {
        const auto got = ExtractNormalized(extractor, "Александр Дудка приехал.");
        ok &= Assert(
            Contains(got, "александр дудк"),
            "surname should be stemmed when lemma does not change the token"
        );
        ok &= Assert(
            !Contains(got, "александра дудк"), "name should not drift to wrong form"
        );
    }
    {
        const auto got =
            ExtractNormalized(extractor, "Иван встретился с Иванову. Иван выступил.");
        ok &= Assert(
            Contains(got, "ива"), "single known person may be shortened by stemmer"
        );
        ok &=
            Assert(Contains(got, "иванов"), "surname should remain after normalization");
    }
    {
        const auto got = ExtractNormalized(extractor, "Иван пришел.");
        const auto got_blocked =
            ExtractNormalized(extractor_with_custom_stop, "Иван пришел.");
        ok &= Assert(Contains(got, "ива"), "baseline extractor should keep Иван");
        ok &= Assert(
            got_blocked.empty(),
            "custom C++ title stop words should block single-token person extraction"
        );
    }
    {
        const auto concepts = Extract(extractor, "С С Унковскому поручили.");
        const auto* c = FindByNorm(concepts, "унковск");
        ok &= Assert(c != nullptr, "initials should not block surname extraction");
        ok &= Assert(
            c != nullptr && c->id == -8629412372075498192LL,
            "concept id for unknown surname should be stable"
        );
        ok &= Assert(
            c != nullptr && c->original == "Унковскому",
            "leading initials should be trimmed from original person span"
        );
    }
    {
        const auto concepts =
            Extract(extractor, "Иван Иванову, Александр Дудка поручили.");
        const auto* first = FindByNorm(concepts, "ива иванов");
        const auto* second = FindByNorm(concepts, "александр дудк");
        ok &= Assert(
            first != nullptr,
            "first person before comma should be extracted as standalone concept"
        );
        ok &= Assert(
            second != nullptr,
            "second person after comma should be extracted as standalone concept"
        );
        ok &= Assert(
            first != nullptr && first->original == "Иван Иванову",
            "original for first comma-separated person should be exact"
        );
        ok &= Assert(
            second != nullptr && second->original == "Александр Дудка",
            "original for second comma-separated person should be exact"
        );
    }
    {
        const auto got = ExtractNormalized(extractor, "Александр Хинштейном выступил.");
        ok &= Assert(
            Contains(got, "александр хинштейн"),
            "unknown surname inflection should be normalized with stemmer"
        );
    }
    {
        const auto got = ExtractNormalized(extractor, "Петренко выступил.");
        ok &= Assert(
            Contains(got, "петренк"),
            "unknown single-token surname should pass suffix heuristic and be stemmed"
        );
    }
    {
        const auto got = ExtractNormalized(extractor, "Кот выступил.");
        ok &= Assert(got.empty(), "short unknown single token should be rejected");
    }
    {
        const auto got = ExtractNormalized(extractor, "Ильич выступил.");
        ok &= Assert(
            Contains(got, "ильич"),
            "patronymic-like suffix should be accepted even when token is unknown"
        );
    }
    {
        const auto got = ExtractNormalized(extractor, "Владимир выступил.");
        ok &= Assert(
            Contains(got, "владимир"),
            "single geo-like name владимир should be kept by dedicated exception"
        );
    }
    {
        const auto got = ExtractNormalized(extractor, "Евгений выступил.");
        ok &= Assert(
            got.empty(), "single geo-like name without exception should be rejected"
        );
    }
    {
        const auto concepts = Extract(extractor, "Сказал Президент Иван Иванов.");
        const auto* c = FindByNorm(concepts, "ива иван");
        ok &= Assert(
            c != nullptr, "leading known context token should be trimmed from person span"
        );
        ok &= Assert(
            c != nullptr && c->original == "Иван Иванов",
            "trimmed context token must not leak into original person span"
        );
    }
    {
        const auto got = ExtractNormalized(extractor, "Financial Times сообщил.");
        ok &= Assert(got.empty(), "latin org-like pair should be rejected");
    }
    {
        const auto concepts = Extract(extractor, "John Smith сообщил.");
        const auto* c = FindByNorm(concepts, "john smith");
        ok &= Assert(c != nullptr, "latin person pair should be accepted");
        ok &= Assert(
            c != nullptr && c->id == 463651412877132554LL,
            "latin concept id should match Python hash_int64(blake2b-64)"
        );
        ok &= Assert(
            c != nullptr && c->original == "John Smith",
            "original should preserve latin person pair"
        );
    }
    {
        const auto got = ExtractNormalized(extractor, "John Ronald Reuel приехал.");
        ok &= Assert(got.empty(), "three-token fully latin spans should be rejected");
    }
    {
        const auto concepts = Extract(extractor, "Жан-Клод Ван-Дамм приехал.");
        ok &= Assert(
            concepts.size() == 1, "hyphenated person should be extracted as one concept"
        );
        ok &= Assert(
            concepts.size() == 1 && concepts[0].original == "Жан-Клод Ван-Дамм",
            "hyphenated original should preserve punctuation and case"
        );
    }
    {
        const auto concepts = extractor.ExtractPersons(
            {"Иван Иванов.", "В Москве снег.", "Александр Дудка приехал."}
        );
        bool found_first = false;
        bool found_third = false;
        for (const auto& c : concepts) {
            if (c.normalized == "ива иван" && c.passage_index == 0 &&
                c.original == "Иван Иванов") {
                found_first = true;
            }
            if (c.normalized == "александр дудк" && c.passage_index == 2 &&
                c.original == "Александр Дудка") {
                found_third = true;
            }
        }
        ok &= Assert(
            found_first, "concept should keep correct passage_index for passage 0"
        );
        ok &= Assert(
            found_third, "concept should keep correct passage_index for passage 2"
        );
    }

    std::filesystem::remove_all(dict_dir);
    if (ok) {
        std::cout << "All person extractor rule tests passed.\n";
    }
}
