/*
 * Plain C ABI wrapper around PersonExtractor.
 *
 * This is the boundary cgo (and any other C FFI) talks to. It exposes no C++
 * types — only opaque handles, primitive types, and C strings — so it is safe
 * to call from Go, Rust, etc.
 *
 * Ownership rules:
 *   - ce_extractor_create() returns a handle you must free with
 *     ce_extractor_destroy().
 *   - ce_extract() returns a ce_result* you must free with ce_result_free().
 *     All char* inside it are owned by that result and freed together with it.
 *   - On error, create/extract return NULL; call ce_last_error() for a message.
 *     The returned error pointer is thread-local and valid until the next C-API
 *     call on the same thread — copy it if you need to keep it.
 */
#ifndef CONCEPT_EXTRACTOR_GRAPH_C_H
#define CONCEPT_EXTRACTOR_GRAPH_C_H

#include <stdint.h>

/* Export macro: makes the API symbols visible even when the library is built
 * with hidden default visibility. */
#if defined(_WIN32) || defined(__CYGWIN__)
#  ifdef CONCEPT_EXTRACTOR_C_BUILD
#    define CE_API __declspec(dllexport)
#  else
#    define CE_API __declspec(dllimport)
#  endif
#else
#  define CE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to a PersonExtractor instance. */
typedef struct ce_extractor ce_extractor;

/* One extracted person-name concept (mirrors C++ ExtractedConcept). */
typedef struct {
    int64_t id;            /* stable BLAKE2b-64 id over `normalized`        */
    const char* normalized; /* normalized form, e.g. "александр хинштейн"   */
    const char* original;   /* as in text, e.g. "Александром Хинштейном"    */
    int32_t passage_index;  /* index into the passages array passed in      */
} ce_concept;

/* Result of one ce_extract() call. */
typedef struct {
    ce_concept* items;
    int32_t count;
} ce_result;

/*
 * Create an extractor that loads its dictionary from `dict_dir`.
 * Optional `title_stop_words` is an array of `stop_count` lowercased C strings
 * (pass NULL / 0 for none). Returns NULL on failure (see ce_last_error()).
 */
CE_API ce_extractor* ce_extractor_create(
    const char* dict_dir,
    const char** title_stop_words,
    int32_t stop_count
);

/* Destroy an extractor. NULL is allowed and ignored. */
CE_API void ce_extractor_destroy(ce_extractor* extractor);

/*
 * Run extraction over `count` passages (array of C strings).
 * Returns a heap-allocated ce_result* (free with ce_result_free), or NULL on
 * error. A successful call with no matches returns a result with count == 0.
 */
CE_API ce_result* ce_extract(
    ce_extractor* extractor,
    const char** passages,
    int32_t count
);

/* Free a result returned by ce_extract(). NULL is allowed and ignored. */
CE_API void ce_result_free(ce_result* result);

/* Last error message for the current thread, or NULL if none. */
CE_API const char* ce_last_error(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* CONCEPT_EXTRACTOR_GRAPH_C_H */
