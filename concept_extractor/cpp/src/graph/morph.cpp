#include "graph/morph.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

bool EndsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.substr(s.size() - suffix.size(), suffix.size()) == suffix;
}

std::string DropLastCyrillicChar(std::string_view s) {
    if (s.size() < 2) {
        return std::string(s);
    }
    return std::string(s.substr(0, s.size() - 2));
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// File I/O helpers
// ═══════════════════════════════════════════════════════════════════════════════

std::unordered_set<std::string> MorphDictionary::ReadSet(const std::string& path) {
    // Read entire file into memory for speed
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("Cannot open: " + path);
    auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string buf(size, '\0');
    f.read(buf.data(), static_cast<std::streamsize>(size));

    // Count lines for reserve
    size_t line_count = 0;
    for (char c : buf)
        if (c == '\n')
            ++line_count;

    std::unordered_set<std::string> result;
    result.reserve(line_count);

    size_t pos = 0;
    while (pos < buf.size()) {
        auto nl = buf.find('\n', pos);
        if (nl == std::string::npos)
            nl = buf.size();
        if (nl > pos) {
            result.emplace(buf, pos, nl - pos);
        }
        pos = nl + 1;
    }
    return result;
}

std::unordered_map<std::string, std::string> MorphDictionary::ReadMap(
    const std::string& path
) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("Cannot open: " + path);
    auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string buf(size, '\0');
    f.read(buf.data(), static_cast<std::streamsize>(size));

    size_t line_count = 0;
    for (char c : buf)
        if (c == '\n')
            ++line_count;

    std::unordered_map<std::string, std::string> result;
    result.reserve(line_count);

    size_t pos = 0;
    while (pos < buf.size()) {
        auto nl = buf.find('\n', pos);
        if (nl == std::string::npos)
            nl = buf.size();
        auto tab = buf.find('\t', pos);
        if (tab < nl) {
            result.emplace(buf.substr(pos, tab - pos), buf.substr(tab + 1, nl - tab - 1));
        }
        pos = nl + 1;
    }
    return result;
}

std::unordered_set<std::string> MorphDictionary::ReadSetIfExists(
    const std::string& path
) {
    if (!std::filesystem::exists(path)) {
        return {};
    }
    return ReadSet(path);
}

std::unordered_map<std::string, std::string> MorphDictionary::ReadMapIfExists(
    const std::string& path
) {
    if (!std::filesystem::exists(path)) {
        return {};
    }
    return ReadMap(path);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Constructor
// ═══════════════════════════════════════════════════════════════════════════════

MorphDictionary::MorphDictionary(const std::string& dir) {
    auto sep = dir.empty() || dir.back() == '/' ? "" : "/";
    person_forms_ = ReadSet(dir + sep + "person_forms.txt");
    name_forms_ = ReadSetIfExists(dir + sep + "name_forms.txt");
    surname_forms_ = ReadSetIfExists(dir + sep + "surname_forms.txt");
    patron_forms_ = ReadSetIfExists(dir + sep + "patron_forms.txt");
    geo_forms_ = ReadSet(dir + sep + "geo_forms.txt");
    org_forms_ = ReadSetIfExists(dir + sep + "org_forms.txt");
    trade_forms_ = ReadSetIfExists(dir + sep + "trade_forms.txt");
    known_forms_ = ReadSet(dir + sep + "known_forms.txt");
    noun_forms_ = ReadSetIfExists(dir + sep + "noun_forms.txt");
    adj_forms_ = ReadSetIfExists(dir + sep + "adj_forms.txt");
    verb_forms_ = ReadSetIfExists(dir + sep + "verb_forms.txt");
    lemma_map_person_ = ReadMap(dir + sep + "lemma_map.txt");
    lemma_map_full_ = ReadMapIfExists(dir + sep + "lemma_map_full.txt");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lookups
// ═══════════════════════════════════════════════════════════════════════════════

bool MorphDictionary::IsPerson(std::string_view w) const {
    return person_forms_.count(std::string(w)) > 0;
}

bool MorphDictionary::IsName(std::string_view w) const {
    return name_forms_.count(std::string(w)) > 0;
}

bool MorphDictionary::IsSurname(std::string_view w) const {
    return surname_forms_.count(std::string(w)) > 0;
}

bool MorphDictionary::IsPatron(std::string_view w) const {
    return patron_forms_.count(std::string(w)) > 0;
}

bool MorphDictionary::IsGeo(std::string_view w) const {
    return geo_forms_.count(std::string(w)) > 0;
}

bool MorphDictionary::IsOrg(std::string_view w) const {
    return org_forms_.count(std::string(w)) > 0;
}

bool MorphDictionary::IsTrade(std::string_view w) const {
    return trade_forms_.count(std::string(w)) > 0;
}

bool MorphDictionary::IsKnown(std::string_view w) const {
    return known_forms_.count(std::string(w)) > 0;
}

bool MorphDictionary::IsNoun(std::string_view w) const {
    return noun_forms_.count(std::string(w)) > 0;
}

bool MorphDictionary::IsAdj(std::string_view w) const {
    return adj_forms_.count(std::string(w)) > 0;
}

bool MorphDictionary::IsVerb(std::string_view w) const {
    return verb_forms_.count(std::string(w)) > 0;
}

bool MorphDictionary::HasLemma(std::string_view w) const {
    const auto word = std::string(w);
    return lemma_map_full_.count(word) > 0 || lemma_map_person_.count(word) > 0;
}

std::string MorphDictionary::Lemmatize(std::string_view w) const {
    const auto word = std::string(w);
    const bool person_like =
        IsPerson(word) || IsName(word) || IsSurname(word) || IsPatron(word);
    auto normalize_person_lemma = [&](const std::string& lemma) -> std::string {
        if (!person_like || lemma.empty()) {
            return lemma;
        }
        // Ambiguous dictionary rows may map "александр" -> "александра".
        // Keep original token when lemma is just +1 trailing "а".
        if (lemma.size() == word.size() + 2 && EndsWith(lemma, "а") &&
            lemma.substr(0, word.size()) == word) {
            return word;
        }
        // Some surnames can map as plural-family form ("черниковы").
        // If source is "черниковым", normalize to singular surname stem.
        if (word.size() == lemma.size() + 2 && EndsWith(word, "м") &&
            EndsWith(lemma, "ы") && word.substr(0, lemma.size()) == lemma) {
            return DropLastCyrillicChar(lemma);
        }
        return lemma;
    };

    if (person_like) {
        auto it_person = lemma_map_person_.find(word);
        if (it_person != lemma_map_person_.end()) {
            return normalize_person_lemma(it_person->second);
        }
        auto it_full = lemma_map_full_.find(word);
        if (it_full != lemma_map_full_.end()) {
            return normalize_person_lemma(it_full->second);
        }
    } else {
        auto it_full = lemma_map_full_.find(word);
        if (it_full != lemma_map_full_.end()) {
            return it_full->second;
        }
        auto it_person = lemma_map_person_.find(word);
        if (it_person != lemma_map_person_.end()) {
            return normalize_person_lemma(it_person->second);
        }
    }

    // If a known word has no explicit lemma map entry, keep it as-is.
    // Fallback stripping is only for unknown tokens.
    if (known_forms_.count(word) > 0) {
        return word;
    }

    if (word.size() < 10) {
        return word;
    }

    if (word.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ"
            "абвгдеёжзийклмнопрстуфхцчшщъыьэюя-"
        ) != std::string::npos) {
        return word;
    }

    auto stripped = StripSuffix(word);
    if (stripped.size() < 6) {
        return word;
    }
    if (stripped == word) {
        return word;
    }
    return stripped;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Suffix stripping for unknown Cyrillic names
// ═══════════════════════════════════════════════════════════════════════════════

std::string MorphDictionary::StripSuffix(std::string_view w) {
    constexpr size_t kMinRemaining = 6;  // 3 Cyrillic chars

    // Strip only 2-char endings (instrumental/adj-like) to avoid over-truncation.
    if (w.size() >= kMinRemaining + 4) {
        auto tail4 = w.substr(w.size() - 4);
        // clang-format off
        if (tail4 == "ом" || tail4 == "ем" || tail4 == "ём" ||
            tail4 == "ым" || tail4 == "им" ||
            tail4 == "ой" || tail4 == "ей") {
            return std::string(w.substr(0, w.size() - 4));
        }
        // clang-format on
    }

    return std::string(w);
}
