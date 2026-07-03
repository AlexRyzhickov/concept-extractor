/*
 * Implementation of the plain C ABI declared in graph_c.h.
 *
 * It wraps the C++ PersonExtractor. Every entry point is wrapped in try/catch
 * so that C++ exceptions never cross the FFI boundary (which would be UB).
 * Errors are reported via a thread-local message retrievable with
 * ce_last_error().
 */
#include "graph_c.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "graph/person_extractor.h"

namespace {

// Thread-local last-error buffer. We own this std::string; ce_last_error()
// hands out its c_str(), valid until the next API call on this thread.
thread_local std::string g_last_error;

void SetError(const char* msg) {
    g_last_error = msg ? msg : "unknown error";
}

void ClearError() {
    g_last_error.clear();
}

// Duplicate a std::string into a malloc'd C string (so the consumer side, and
// ce_result_free, can use free()).
char* DupString(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (out == nullptr) {
        return nullptr;
    }
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

}  // namespace

// The opaque handle simply holds the C++ object.
struct ce_extractor {
    PersonExtractor impl;

    ce_extractor(const std::string& dir, std::vector<std::string> stops)
        : impl(dir, std::move(stops)) {}
};

extern "C" {

ce_extractor* ce_extractor_create(
    const char* dict_dir,
    const char** title_stop_words,
    int32_t stop_count
) {
    ClearError();
    if (dict_dir == nullptr) {
        SetError("dict_dir is NULL");
        return nullptr;
    }
    try {
        std::vector<std::string> stops;
        if (title_stop_words != nullptr && stop_count > 0) {
            stops.reserve(static_cast<size_t>(stop_count));
            for (int32_t i = 0; i < stop_count; ++i) {
                if (title_stop_words[i] != nullptr) {
                    stops.emplace_back(title_stop_words[i]);
                }
            }
        }
        return new ce_extractor(std::string(dict_dir), std::move(stops));
    } catch (const std::exception& e) {
        SetError(e.what());
    } catch (...) {
        SetError("unknown C++ exception in ce_extractor_create");
    }
    return nullptr;
}

void ce_extractor_destroy(ce_extractor* extractor) {
    delete extractor;  // delete nullptr is a no-op
}

ce_result* ce_extract(
    ce_extractor* extractor,
    const char** passages,
    int32_t count
) {
    ClearError();
    if (extractor == nullptr) {
        SetError("extractor is NULL");
        return nullptr;
    }
    if (count < 0 || (count > 0 && passages == nullptr)) {
        SetError("invalid passages/count");
        return nullptr;
    }

    try {
        std::vector<std::string> input;
        input.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
            input.emplace_back(passages[i] != nullptr ? passages[i] : "");
        }

        const std::vector<ExtractedConcept> concepts =
            extractor->impl.ExtractPersons(input);

        auto* result = static_cast<ce_result*>(std::malloc(sizeof(ce_result)));
        if (result == nullptr) {
            SetError("out of memory");
            return nullptr;
        }
        result->items = nullptr;
        result->count = 0;

        const size_t n = concepts.size();
        if (n > 0) {
            result->items =
                static_cast<ce_concept*>(std::malloc(n * sizeof(ce_concept)));
            if (result->items == nullptr) {
                std::free(result);
                SetError("out of memory");
                return nullptr;
            }
            for (size_t i = 0; i < n; ++i) {
                result->items[i].id = concepts[i].id;
                result->items[i].normalized = DupString(concepts[i].normalized);
                result->items[i].original = DupString(concepts[i].original);
                result->items[i].passage_index = concepts[i].passage_index;
            }
            result->count = static_cast<int32_t>(n);
        }
        return result;
    } catch (const std::exception& e) {
        SetError(e.what());
    } catch (...) {
        SetError("unknown C++ exception in ce_extract");
    }
    return nullptr;
}

void ce_result_free(ce_result* result) {
    if (result == nullptr) {
        return;
    }
    for (int32_t i = 0; i < result->count; ++i) {
        // const_cast because the struct exposes const char* to consumers, but
        // we allocated them and own them here.
        std::free(const_cast<char*>(result->items[i].normalized));
        std::free(const_cast<char*>(result->items[i].original));
    }
    std::free(result->items);
    std::free(result);
}

const char* ce_last_error(void) {
    return g_last_error.empty() ? nullptr : g_last_error.c_str();
}

}  // extern "C"
