#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "graph/morph.h"

/// A single extracted person-name concept.
struct ExtractedConcept {
    int64_t id = 0;             ///< Stable int64 id (BLAKE2b-64 over normalized)
    std::string normalized;     ///< Normalized form ("александр хинштейн")
    std::string original;       ///< As in text ("Александром Хинштейном")
    int32_t passage_index = 0;  ///< Index in the input passages vector
};

/**
 * @brief Extracts person names from text passages.
 *
 * Single-phase API: `ExtractPersons(passages)` — tokenize, build capitalized spans,
 * filter to person names via MorphDictionary, normalize, deduplicate.
 *
 * No external runtime dependencies. Dictionary loaded from txt files in C++.
 *
 * ## Thread-safety
 * Immutable after construction — safe to call from multiple threads.
 */
class PersonExtractor {
   public:
    /**
     * @param dict_dir  Directory with morph txt files (person_forms.txt, etc.)
     * @param title_stop_words  Lowercased words that break named-entity spans.
     */
    PersonExtractor(
        const std::string& dict_dir,
        std::vector<std::string> title_stop_words = {}
    );

    /// Extract person names from all passages.
    std::vector<ExtractedConcept> ExtractPersons(
        const std::vector<std::string>& passages
    ) const;

   private:
    std::shared_ptr<const MorphDictionary> morph_;
    std::unordered_set<std::string> title_stop_;
    bool allow_single_initial_ = true;
    int32_t min_token_len_ = 2;
};
