#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

/**
 * @brief Dictionary-based morphological analyzer for person-name extraction.
 *
 * Loads pre-prepared txt files from a directory:
 *   person_forms.txt  — one lowercased wordform per line (Name/Surn/Patr)
 *   geo_forms.txt     — one lowercased wordform per line (Geox)
 *   known_forms.txt   — one lowercased wordform per line (all dictionary words)
 *   lemma_map.txt     — tab-separated: wordform\tlemma (for Name/Surn/Patr)
 *
 * Optional files (loaded when present):
 *   name_forms.txt, surname_forms.txt, patron_forms.txt
 *   org_forms.txt, trade_forms.txt
 *   noun_forms.txt, adj_forms.txt, verb_forms.txt
 *   lemma_map_full.txt — tab-separated: wordform\tlemma (for all words)
 *
 * Files are prepared once via PersonExtractor.prepare_dictionary() in Python.
 *
 * ## Thread-safety
 * Immutable after construction — safe to share across threads.
 */
class MorphDictionary {
   public:
    /// Load from directory containing the 4 txt files.
    explicit MorphDictionary(const std::string& dir);

    bool IsPerson(std::string_view lower_word) const;
    bool IsName(std::string_view lower_word) const;
    bool IsSurname(std::string_view lower_word) const;
    bool IsPatron(std::string_view lower_word) const;
    bool IsGeo(std::string_view lower_word) const;
    bool IsOrg(std::string_view lower_word) const;
    bool IsTrade(std::string_view lower_word) const;
    bool IsKnown(std::string_view lower_word) const;
    bool IsNoun(std::string_view lower_word) const;
    bool IsAdj(std::string_view lower_word) const;
    bool IsVerb(std::string_view lower_word) const;
    bool HasLemma(std::string_view lower_word) const;
    std::string Lemmatize(std::string_view lower_word) const;

   private:
    std::unordered_set<std::string> person_forms_;
    std::unordered_set<std::string> name_forms_;
    std::unordered_set<std::string> surname_forms_;
    std::unordered_set<std::string> patron_forms_;
    std::unordered_set<std::string> geo_forms_;
    std::unordered_set<std::string> org_forms_;
    std::unordered_set<std::string> trade_forms_;
    std::unordered_set<std::string> known_forms_;
    std::unordered_set<std::string> noun_forms_;
    std::unordered_set<std::string> adj_forms_;
    std::unordered_set<std::string> verb_forms_;
    std::unordered_map<std::string, std::string> lemma_map_person_;
    std::unordered_map<std::string, std::string> lemma_map_full_;

    static std::string StripSuffix(std::string_view word);
    static std::unordered_set<std::string> ReadSetIfExists(const std::string& path);
    static std::unordered_map<std::string, std::string> ReadMapIfExists(
        const std::string& path
    );
    static std::unordered_set<std::string> ReadSet(const std::string& path);
    static std::unordered_map<std::string, std::string> ReadMap(const std::string& path);
};
