#include "graph/person_extractor.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "libstemmer.h"

// ═══════════════════════════════════════════════════════════════════════════════
// UTF-8 utilities (Cyrillic + Latin)
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

inline uint32_t DecodeUtf8(const char* p, size_t remaining, int& out_len) {
    auto b0 = static_cast<uint8_t>(p[0]);
    if (b0 < 0x80u) {
        out_len = 1;
        return b0;
    }
    if ((b0 & 0xE0u) == 0xC0u && remaining >= 2) {
        out_len = 2;
        return ((b0 & 0x1Fu) << 6) | (static_cast<uint8_t>(p[1]) & 0x3Fu);
    }
    if ((b0 & 0xF0u) == 0xE0u && remaining >= 3) {
        out_len = 3;
        return ((b0 & 0x0Fu) << 12) | ((static_cast<uint8_t>(p[1]) & 0x3Fu) << 6) |
               (static_cast<uint8_t>(p[2]) & 0x3Fu);
    }
    if ((b0 & 0xF8u) == 0xF0u && remaining >= 4) {
        out_len = 4;
        return ((b0 & 0x07u) << 18) | ((static_cast<uint8_t>(p[1]) & 0x3Fu) << 12) |
               ((static_cast<uint8_t>(p[2]) & 0x3Fu) << 6) |
               (static_cast<uint8_t>(p[3]) & 0x3Fu);
    }
    out_len = 1;
    return 0xFFFDu;
}

inline void EncodeUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80u) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

inline bool IsLetter(uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
           (cp >= 0x0410u && cp <= 0x044Fu) || cp == 0x0401u || cp == 0x0451u;
}

inline bool IsUpper(uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') || (cp >= 0x0410u && cp <= 0x042Fu) || cp == 0x0401u;
}

inline bool IsDigit(uint32_t cp) {
    return cp >= '0' && cp <= '9';
}

inline bool IsSentEnd(uint32_t cp) {
    return cp == '.' || cp == '!' || cp == '?' || cp == ';' || cp == 0x2026u;
}

inline uint32_t ToLowerCp(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z')
        return cp + 32;
    if (cp >= 0x0410u && cp <= 0x042Fu)
        return cp + 0x20u;
    if (cp == 0x0401u)
        return 0x0451u;
    return cp;
}

std::string ToLower(std::string_view s) {
    std::string r;
    r.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        int len = 0;
        uint32_t cp = DecodeUtf8(s.data() + i, s.size() - i, len);
        EncodeUtf8(r, ToLowerCp(cp));
        i += static_cast<size_t>(len);
    }
    return r;
}

bool StartsUpper(std::string_view s) {
    if (s.empty())
        return false;
    int len = 0;
    return IsUpper(DecodeUtf8(s.data(), s.size(), len));
}

bool IsAllCaps(std::string_view s) {
    int alpha = 0;
    size_t i = 0;
    while (i < s.size()) {
        int len = 0;
        uint32_t cp = DecodeUtf8(s.data() + i, s.size() - i, len);
        if (IsLetter(cp)) {
            if (!IsUpper(cp))
                return false;
            ++alpha;
        }
        i += static_cast<size_t>(len);
    }
    return alpha >= 2;
}

bool IsLatinFirst(std::string_view s) {
    if (s.empty())
        return false;
    auto b = static_cast<uint8_t>(s[0]);
    return (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z');
}

// ═══════════════════════════════════════════════════════════════════════════════
// BLAKE2b-64 (compatible with Python hashlib.blake2b(..., digest_size=8))
// ═══════════════════════════════════════════════════════════════════════════════

constexpr size_t kBlake2bBlockBytes = 128;
constexpr size_t kBlake2bOutBytes = 8;

constexpr std::array<uint64_t, 8> kBlake2bIv = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
    0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

constexpr std::array<std::array<uint8_t, 16>, 12> kBlake2bSigma = {{
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
    {{14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}},
    {{11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4}},
    {{7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8}},
    {{9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13}},
    {{2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9}},
    {{12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11}},
    {{13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10}},
    {{6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5}},
    {{10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0}},
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
    {{14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}},
}};

inline uint64_t Load64Le(const uint8_t* p) {
    return static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
           (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24) |
           (static_cast<uint64_t>(p[4]) << 32) | (static_cast<uint64_t>(p[5]) << 40) |
           (static_cast<uint64_t>(p[6]) << 48) | (static_cast<uint64_t>(p[7]) << 56);
}

inline void Store64Le(uint64_t x, uint8_t* out) {
    out[0] = static_cast<uint8_t>(x);
    out[1] = static_cast<uint8_t>(x >> 8);
    out[2] = static_cast<uint8_t>(x >> 16);
    out[3] = static_cast<uint8_t>(x >> 24);
    out[4] = static_cast<uint8_t>(x >> 32);
    out[5] = static_cast<uint8_t>(x >> 40);
    out[6] = static_cast<uint8_t>(x >> 48);
    out[7] = static_cast<uint8_t>(x >> 56);
}

inline uint64_t Rotr64(uint64_t x, uint32_t n) {
    return (x >> n) | (x << (64u - n));
}

inline void Blake2bG(
    std::array<uint64_t, 16>& v,
    uint32_t a,
    uint32_t b,
    uint32_t c,
    uint32_t d,
    uint64_t x,
    uint64_t y
) {
    v[a] = v[a] + v[b] + x;
    v[d] = Rotr64(v[d] ^ v[a], 32);
    v[c] = v[c] + v[d];
    v[b] = Rotr64(v[b] ^ v[c], 24);
    v[a] = v[a] + v[b] + y;
    v[d] = Rotr64(v[d] ^ v[a], 16);
    v[c] = v[c] + v[d];
    v[b] = Rotr64(v[b] ^ v[c], 63);
}

void Blake2bCompress(
    std::array<uint64_t, 8>& h,
    const std::array<uint8_t, kBlake2bBlockBytes>& block,
    uint64_t t0,
    uint64_t t1,
    bool is_last
) {
    std::array<uint64_t, 16> m{};
    std::array<uint64_t, 16> v{};

    for (size_t i = 0; i < 16; ++i) {
        m[i] = Load64Le(block.data() + i * sizeof(uint64_t));
    }

    for (size_t i = 0; i < 8; ++i) {
        v[i] = h[i];
        v[i + 8] = kBlake2bIv[i];
    }

    v[12] ^= t0;
    v[13] ^= t1;
    if (is_last) {
        v[14] = ~v[14];
    }

    for (size_t round = 0; round < 12; ++round) {
        const auto& s = kBlake2bSigma[round];
        Blake2bG(v, 0, 4, 8, 12, m[s[0]], m[s[1]]);
        Blake2bG(v, 1, 5, 9, 13, m[s[2]], m[s[3]]);
        Blake2bG(v, 2, 6, 10, 14, m[s[4]], m[s[5]]);
        Blake2bG(v, 3, 7, 11, 15, m[s[6]], m[s[7]]);
        Blake2bG(v, 0, 5, 10, 15, m[s[8]], m[s[9]]);
        Blake2bG(v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
        Blake2bG(v, 2, 7, 8, 13, m[s[12]], m[s[13]]);
        Blake2bG(v, 3, 4, 9, 14, m[s[14]], m[s[15]]);
    }

    for (size_t i = 0; i < 8; ++i) {
        h[i] ^= v[i] ^ v[i + 8];
    }
}

std::array<uint8_t, kBlake2bOutBytes> Blake2b64(std::string_view input) {
    std::array<uint64_t, 8> h = kBlake2bIv;
    // Parameter block for unkeyed BLAKE2b with digest_size=8.
    h[0] ^= 0x01010000ULL ^ static_cast<uint64_t>(kBlake2bOutBytes);

    uint64_t t0 = 0;
    uint64_t t1 = 0;
    size_t offset = 0;
    const auto* data = reinterpret_cast<const uint8_t*>(input.data());

    while (input.size() - offset > kBlake2bBlockBytes) {
        std::array<uint8_t, kBlake2bBlockBytes> block{};
        std::memcpy(block.data(), data + offset, kBlake2bBlockBytes);

        offset += kBlake2bBlockBytes;
        const auto prev_t0 = t0;
        t0 += static_cast<uint64_t>(kBlake2bBlockBytes);
        if (t0 < prev_t0) {
            ++t1;
        }
        Blake2bCompress(h, block, t0, t1, false);
    }

    std::array<uint8_t, kBlake2bBlockBytes> block{};
    const size_t final_len = input.size() - offset;
    if (final_len > 0) {
        std::memcpy(block.data(), data + offset, final_len);
    }

    const auto prev_t0 = t0;
    t0 += static_cast<uint64_t>(final_len);
    if (t0 < prev_t0) {
        ++t1;
    }
    Blake2bCompress(h, block, t0, t1, true);

    std::array<uint8_t, 64> full{};
    for (size_t i = 0; i < h.size(); ++i) {
        Store64Le(h[i], full.data() + i * sizeof(uint64_t));
    }

    std::array<uint8_t, kBlake2bOutBytes> out{};
    std::copy_n(full.data(), kBlake2bOutBytes, out.data());
    return out;
}

int64_t BytesToSignedInt64Be(const std::array<uint8_t, kBlake2bOutBytes>& bytes) {
    uint64_t value = 0;
    for (uint8_t b : bytes) {
        value = (value << 8) | static_cast<uint64_t>(b);
    }

    constexpr uint64_t kSignBit = 1ULL << 63;
    if (value < kSignBit) {
        return static_cast<int64_t>(value);
    }
    return std::numeric_limits<int64_t>::min() + static_cast<int64_t>(value - kSignBit);
}

int64_t HashInt64(std::string_view normalized) {
    return BytesToSignedInt64Be(Blake2b64(normalized));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tokenizer
// ═══════════════════════════════════════════════════════════════════════════════

struct Token {
    std::string text;
    bool is_sent_start = false;
    bool follows_comma = false;
};

std::vector<Token> Tokenize(std::string_view text) {
    std::vector<Token> tokens;
    tokens.reserve(text.size() / 5);
    bool next_sent = true;
    bool follows_comma = false;
    size_t i = 0;

    while (i < text.size()) {
        int len = 0;
        uint32_t cp = DecodeUtf8(text.data() + i, text.size() - i, len);

        if (IsSentEnd(cp)) {
            next_sent = true;
            follows_comma = false;
            i += static_cast<size_t>(len);
            continue;
        }

        if (IsLetter(cp)) {
            size_t start = i;
            i += static_cast<size_t>(len);

            while (i < text.size()) {
                int len2 = 0;
                uint32_t cp2 = DecodeUtf8(text.data() + i, text.size() - i, len2);
                if (IsLetter(cp2)) {
                    i += static_cast<size_t>(len2);
                } else if (cp2 == '-') {
                    size_t after = i + static_cast<size_t>(len2);
                    if (after < text.size()) {
                        int len3 = 0;
                        uint32_t cp3 =
                            DecodeUtf8(text.data() + after, text.size() - after, len3);
                        if (IsLetter(cp3)) {
                            i = after + static_cast<size_t>(len3);
                            continue;
                        }
                    }
                    break;
                } else {
                    break;
                }
            }

            tokens.push_back(
                {std::string(text.data() + start, i - start), next_sent, follows_comma}
            );
            next_sent = false;
            follows_comma = false;
            continue;
        }

        if (IsDigit(cp)) {
            next_sent = false;
            follows_comma = false;
            i += static_cast<size_t>(len);
            while (i < text.size()) {
                int len2 = 0;
                uint32_t cp2 = DecodeUtf8(text.data() + i, text.size() - i, len2);
                if (IsDigit(cp2) || cp2 == '.' || cp2 == ',') {
                    i += static_cast<size_t>(len2);
                } else {
                    break;
                }
            }
            continue;
        }

        if (cp == ',') {
            follows_comma = true;
            i += static_cast<size_t>(len);
            continue;
        }

        i += static_cast<size_t>(len);
    }

    return tokens;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Span extraction helpers
// ═══════════════════════════════════════════════════════════════════════════════

struct Span {
    std::vector<std::string> words;   // original case
    std::vector<std::string> lowers;  // lowercased
    bool starts_sent_start = false;
};

size_t LetterCount(std::string_view s) {
    size_t count = 0;
    size_t i = 0;
    while (i < s.size()) {
        int len = 0;
        uint32_t cp = DecodeUtf8(s.data() + i, s.size() - i, len);
        if (IsLetter(cp)) {
            ++count;
        }
        i += static_cast<size_t>(len);
    }
    return count;
}

bool IsSingleLetterToken(std::string_view s) {
    return LetterCount(s) == 1;
}

bool EndsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.substr(s.size() - suffix.size(), suffix.size()) == suffix;
}

bool LooksLikePatronymic(std::string_view lower) {
    return EndsWith(lower, "вич") || EndsWith(lower, "вна") || EndsWith(lower, "оглы") ||
           EndsWith(lower, "кызы") || EndsWith(lower, "уулу") || EndsWith(lower, "улы");
}

const std::unordered_set<std::string>& HardStopWords() {
    static const std::unordered_set<std::string> kWords = {
        "и",
        "а",
        "но",
        "или",
        "да",
        "ли",
        "же",
        "бы",
        "в",
        "во",
        "на",
        "по",
        "к",
        "ко",
        "о",
        "об",
        "обо",
        "с",
        "со",
        "у",
        "от",
        "до",
        "из",
        "за",
        "при",
        "для",
        "под",
        "над",
        "про",
        "без",
        "через",
        "между",
        "the",
        "and",
        "or",
        "for",
        "of",
        "in",
        "to",
        "on",
        "at",
        "by",
        "from",
        "with",
        "a",
        "an",
        "i",
        // Promoted from Python title-stop defaults based on silver-set deltas.
        "также",
        "кроме",
        "однако",
        "именно",
        "более",
        "после",
        "перед",
        "этот",
        "новый",
        "первый",
        "второй",
        "третий",
        "большой",
        "любой",
        "август",
        "март",
        "апрель",
        "май",
        "четверг",
    };
    return kWords;
}

const std::unordered_set<std::string>& LatinOrgWords() {
    static const std::unordered_set<std::string> kWords = {
        "times", "post",   "play",  "like",   "store",    "news",    "media",   "app",
        "apps",  "google", "fit",   "kids",   "save",     "up",      "bank",    "group",
        "club",  "team",   "first", "flight", "telegram", "youtube", "samsung", "galaxy",
    };
    return kWords;
}

bool IsHardStop(
    std::string_view lower,
    const std::unordered_set<std::string>& title_stop
) {
    return title_stop.count(std::string(lower)) > 0 ||
           HardStopWords().count(std::string(lower)) > 0;
}

bool IsPotentialPersonToken(
    std::string_view lower,
    const MorphDictionary& morph,
    int32_t min_token_len
) {
    if (LetterCount(lower) < static_cast<size_t>(min_token_len)) {
        return false;
    }
    if (IsLatinFirst(lower)) {
        return true;
    }

    const bool is_person = morph.IsPerson(lower) || morph.IsName(lower) ||
                           morph.IsSurname(lower) || morph.IsPatron(lower);
    if (is_person) {
        return true;
    }
    if (morph.IsGeo(lower) || morph.IsOrg(lower) || morph.IsTrade(lower)) {
        return false;
    }
    if (!morph.IsKnown(lower)) {
        return true;
    }
    return false;
}

struct TokenMorphTags {
    bool is_name = false;
    bool is_surname = false;
    bool is_patron = false;
    bool is_person = false;
    bool is_geo = false;
    bool is_org = false;
    bool is_trade = false;
    bool is_known = false;
    bool is_adj = false;
    bool is_latin = false;
};

TokenMorphTags AnalyzeToken(std::string_view lower, const MorphDictionary& morph) {
    TokenMorphTags t;
    t.is_latin = IsLatinFirst(lower);
    if (t.is_latin) {
        return t;
    }
    t.is_name = morph.IsName(lower);
    t.is_surname = morph.IsSurname(lower);
    t.is_patron = morph.IsPatron(lower);
    t.is_person = t.is_name || t.is_surname || t.is_patron || morph.IsPerson(lower);
    t.is_geo = morph.IsGeo(lower);
    t.is_org = morph.IsOrg(lower);
    t.is_trade = morph.IsTrade(lower);
    t.is_known = morph.IsKnown(lower);
    t.is_adj = morph.IsAdj(lower);
    return t;
}

bool IsContextPrefixToken(const TokenMorphTags& t) {
    if (t.is_latin) {
        return false;
    }
    if (t.is_name || t.is_patron) {
        return false;
    }
    if (!t.is_person && t.is_known) {
        return true;
    }
    return t.is_geo || t.is_org || t.is_trade || t.is_adj;
}

bool IsStrongPersonStart(const TokenMorphTags& t) {
    if (t.is_latin) {
        return true;
    }
    if (!t.is_person) {
        return false;
    }
    if (t.is_name || t.is_patron) {
        return true;
    }
    if (!t.is_surname) {
        return true;
    }
    return !t.is_adj && !t.is_geo && !t.is_org && !t.is_trade;
}

bool LooksLikeUnknownSinglePersonToken(std::string_view lower) {
    if (IsLatinFirst(lower)) {
        return false;
    }
    if (LetterCount(lower) < 4) {
        return false;
    }

    static const std::unordered_set<std::string> kWhitelist = {
        "зеленский", "зеленского", "макрон", "байден",   "байдена",
        "маск",      "джеффрис",   "месси",  "хинштейн",
    };
    if (kWhitelist.count(std::string(lower)) > 0) {
        return true;
    }

    static constexpr std::array<std::string_view, 21> kSuffixes = {
        "ский", "ского", "скому", "ским", "ских", "ов",  "ова",
        "ев",   "ева",   "ин",    "ина",  "ко",   "чук", "юк",
        "ович", "евич",  "ич",    "ян",   "янц",  "дзе", "швили",
    };
    for (auto s : kSuffixes) {
        if (EndsWith(lower, s)) {
            return true;
        }
    }
    return false;
}

void TrimLeadingContext(Span& span, const MorphDictionary& morph, int32_t min_token_len) {
    while (span.words.size() > 1) {
        const auto head = AnalyzeToken(span.lowers.front(), morph);
        const auto next = AnalyzeToken(span.lowers[1], morph);

        const bool next_person_like =
            IsStrongPersonStart(next) ||
            (!next.is_known &&
             LetterCount(span.words[1]) >= static_cast<size_t>(min_token_len));
        if (!next_person_like) {
            break;
        }

        if (!IsContextPrefixToken(head)) {
            break;
        }

        span.words.erase(span.words.begin());
        span.lowers.erase(span.lowers.begin());
    }
}

bool CanUseSingleInitial(
    const std::vector<Token>& tokens,
    int32_t idx,
    const MorphDictionary& morph,
    const std::unordered_set<std::string>& title_stop,
    bool allow_single_initial,
    int32_t min_token_len
) {
    if (!allow_single_initial) {
        return false;
    }

    const auto n = static_cast<int32_t>(tokens.size());
    for (int32_t j = idx + 1; j < n && j <= idx + 2; ++j) {
        const auto& next = tokens[static_cast<size_t>(j)];
        if (IsAllCaps(next.text) || !StartsUpper(next.text)) {
            return false;
        }
        auto next_lower = ToLower(next.text);
        if (IsHardStop(next_lower, title_stop)) {
            return false;
        }
        if (IsSingleLetterToken(next.text)) {
            continue;
        }
        return IsPotentialPersonToken(next_lower, morph, min_token_len);
    }
    return false;
}

bool CanStartPerson(
    std::string_view lower,
    const MorphDictionary& morph,
    const std::unordered_set<std::string>& title_stop,
    int32_t min_token_len
) {
    if (IsHardStop(lower, title_stop)) {
        return false;
    }
    return IsPotentialPersonToken(lower, morph, min_token_len);
}

bool IsPersonSpan(
    const Span& span,
    const MorphDictionary& morph,
    const std::unordered_set<std::string>& title_stop,
    int32_t min_token_len
) {
    if (span.words.empty() || span.words.size() > 4) {
        return false;
    }

    int32_t person_hits = 0;
    int32_t geo_hits = 0;
    int32_t org_hits = 0;
    int32_t unknown_hits = 0;
    int32_t known_common_hits = 0;
    int32_t latin_hits = 0;
    int32_t patron_hits = 0;
    int32_t name_hits = 0;
    int32_t surname_hits = 0;
    int32_t adj_hits = 0;
    int32_t geo_like_hits = 0;
    int32_t org_like_hits = 0;
    int32_t effective_tokens = 0;
    std::string_view single_lower;

    for (size_t i = 0; i < span.lowers.size(); ++i) {
        const auto& lower = span.lowers[i];

        if (IsHardStop(lower, title_stop)) {
            return false;
        }
        if (IsSingleLetterToken(span.words[i])) {
            continue;
        }
        if (LetterCount(span.words[i]) < static_cast<size_t>(min_token_len)) {
            return false;
        }
        if (effective_tokens == 0) {
            single_lower = lower;
        }
        ++effective_tokens;

        if (IsLatinFirst(lower)) {
            ++latin_hits;
            continue;
        }
        if (LooksLikePatronymic(lower)) {
            ++patron_hits;
        }

        const bool is_name = morph.IsName(lower);
        const bool is_surname = morph.IsSurname(lower);
        const bool is_patron = morph.IsPatron(lower);
        const bool is_person =
            morph.IsPerson(lower) || is_name || is_surname || is_patron;
        const bool is_geo = morph.IsGeo(lower);
        const bool is_org = morph.IsOrg(lower) || morph.IsTrade(lower);
        const bool is_known = morph.IsKnown(lower);
        const bool is_adj = morph.IsAdj(lower);

        if (is_person) {
            ++person_hits;
        }
        if (is_name) {
            ++name_hits;
        }
        if (is_surname) {
            ++surname_hits;
        }
        if (is_adj) {
            ++adj_hits;
        }
        if (is_geo) {
            ++geo_like_hits;
        }
        if (is_org) {
            ++org_like_hits;
        }
        if (is_geo && !is_person) {
            ++geo_hits;
        }
        if (is_org && !is_person) {
            ++org_hits;
        }
        if (!is_known) {
            ++unknown_hits;
        } else if (!is_person) {
            ++known_common_hits;
        }
    }

    if (effective_tokens == 0) {
        return false;
    }
    if (geo_hits > 0 && person_hits == 0) {
        return false;
    }
    if (org_hits > 0 && person_hits == 0) {
        return false;
    }
    if (person_hits > 0) {
        if (effective_tokens == 1) {
            if (patron_hits > 0) {
                return true;
            }
            const bool keep_geo_name =
                !single_lower.empty() && single_lower == "владимир";
            if (name_hits > 0) {
                if (org_like_hits > 0) {
                    return false;
                }
                if (geo_like_hits > 0 && !keep_geo_name) {
                    return false;
                }
                return true;
            }
            if (surname_hits > 0 && adj_hits == 0 && geo_like_hits == 0 &&
                org_like_hits == 0) {
                return true;
            }
            return false;
        }
        return true;
    }
    if (patron_hits > 0 && effective_tokens <= 3) {
        return true;
    }

    if (effective_tokens == 1) {
        if (unknown_hits == 1) {
            if (!single_lower.empty() &&
                LooksLikeUnknownSinglePersonToken(single_lower)) {
                return true;
            }
        }
        return false;
    }
    if (known_common_hits > 0) {
        return false;
    }
    if ((unknown_hits + latin_hits) != effective_tokens) {
        return false;
    }
    if (latin_hits > 0) {
        if (effective_tokens > 2) {
            return false;
        }
        if (latin_hits == effective_tokens) {
            for (const auto& lower : span.lowers) {
                if (LatinOrgWords().count(lower) > 0) {
                    return false;
                }
            }
        }
    }
    return true;
}

class RussianStemmer {
   public:
    RussianStemmer() : stemmer_(sb_stemmer_new("russian", "UTF_8")) {}

    ~RussianStemmer() {
        sb_stemmer_delete(stemmer_);
    }

    RussianStemmer(const RussianStemmer&) = delete;
    RussianStemmer& operator=(const RussianStemmer&) = delete;

    std::string Stem(std::string_view lower_token) {
        if (stemmer_ == nullptr || lower_token.empty()) {
            return std::string(lower_token);
        }

        const auto* stemmed = sb_stemmer_stem(
            stemmer_, reinterpret_cast<const sb_symbol*>(lower_token.data()),
            static_cast<int>(lower_token.size())
        );
        if (stemmed == nullptr) {
            return std::string(lower_token);
        }

        const int stemmed_len = sb_stemmer_length(stemmer_);
        if (stemmed_len <= 0) {
            return std::string(lower_token);
        }

        return std::string(
            reinterpret_cast<const char*>(stemmed), static_cast<size_t>(stemmed_len)
        );
    }

   private:
    struct sb_stemmer* stemmer_ = nullptr;
};

std::string StemRussian(std::string_view lower_token) {
    thread_local RussianStemmer stemmer;
    return stemmer.Stem(lower_token);
}

std::string NormalizeRussianToken(std::string_view lower) {
    return StemRussian(lower);
}

std::string NormalizeSpan(const Span& span) {
    std::string result;
    for (size_t i = 0; i < span.lowers.size(); ++i) {
        if (IsSingleLetterToken(span.words[i])) {
            continue;
        }
        const auto& lower = span.lowers[i];
        std::string token_norm;
        if (IsLatinFirst(lower)) {
            token_norm = lower;
        } else {
            token_norm = NormalizeRussianToken(lower);
        }
        if (token_norm.empty()) {
            continue;
        }
        if (!result.empty()) {
            result.push_back(' ');
        }
        result.append(token_norm);
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> Dedup(
    std::vector<std::pair<std::string, std::string>>& concepts
) {
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(concepts.size());

    for (auto& [norm, orig] : concepts) {
        if (norm.empty()) {
            continue;
        }
        result.emplace_back(std::move(norm), std::move(orig));
    }
    return result;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// PersonExtractor
// ═══════════════════════════════════════════════════════════════════════════════

PersonExtractor::PersonExtractor(
    const std::string& dict_dir,
    std::vector<std::string> title_stop_words
)
    : morph_(std::make_shared<MorphDictionary>(dict_dir)),
      allow_single_initial_(true),
      min_token_len_(2) {
    for (const auto& w : HardStopWords()) {
        title_stop_.insert(w);
    }
    for (auto& w : title_stop_words) {
        title_stop_.insert(std::move(w));
    }
}

std::vector<ExtractedConcept> PersonExtractor::ExtractPersons(
    const std::vector<std::string>& passages
) const {
    std::vector<ExtractedConcept> all_concepts;
    const auto& morph = *morph_;

    for (int32_t pi = 0; pi < static_cast<int32_t>(passages.size()); ++pi) {
        auto tokens = Tokenize(passages[static_cast<size_t>(pi)]);
        const auto n = static_cast<int32_t>(tokens.size());

        std::vector<std::pair<std::string, std::string>> passage_concepts;
        int32_t i = 0;

        while (i < n) {
            Span span;

            while (i < n) {
                const auto& tok = tokens[static_cast<size_t>(i)];
                auto lower = ToLower(tok.text);

                if (!span.words.empty() && tok.is_sent_start) {
                    break;
                }
                if (!span.words.empty() && tok.follows_comma) {
                    break;
                }
                if (IsAllCaps(tok.text))
                    break;
                if (!StartsUpper(tok.text))
                    break;
                if (IsHardStop(lower, title_stop_))
                    break;

                if (IsSingleLetterToken(tok.text) &&
                    !CanUseSingleInitial(
                        tokens, i, morph, title_stop_, allow_single_initial_,
                        min_token_len_
                    )) {
                    break;
                }

                if (tok.is_sent_start) {
                    if (CanStartPerson(lower, morph, title_stop_, min_token_len_)) {
                        if (span.words.empty()) {
                            span.starts_sent_start = true;
                        }
                        span.words.push_back(tok.text);
                        span.lowers.push_back(std::move(lower));
                        ++i;
                        continue;
                    }
                    break;
                }

                if (span.words.empty()) {
                    span.starts_sent_start = tok.is_sent_start;
                }
                span.words.push_back(tok.text);
                span.lowers.push_back(std::move(lower));
                ++i;
            }

            if (!span.words.empty() &&
                IsPersonSpan(span, morph, title_stop_, min_token_len_)) {
                TrimLeadingContext(span, morph, min_token_len_);
            }

            if (!span.words.empty() &&
                IsPersonSpan(span, morph, title_stop_, min_token_len_)) {
                std::string orig = span.words[0];
                for (size_t j = 1; j < span.words.size(); ++j) {
                    orig.push_back(' ');
                    orig.append(span.words[j]);
                }
                auto norm = NormalizeSpan(span);
                if (norm.size() >= 2) {
                    passage_concepts.emplace_back(std::move(norm), std::move(orig));
                }
            }

            if (span.words.empty()) {
                ++i;
            }
        }

        auto deduped = Dedup(passage_concepts);
        for (auto& [norm, orig] : deduped) {
            ExtractedConcept extracted;
            extracted.id = HashInt64(norm);
            extracted.normalized = std::move(norm);
            extracted.original = std::move(orig);
            extracted.passage_index = pi;
            all_concepts.push_back(std::move(extracted));
        }
    }

    return all_concepts;
}
