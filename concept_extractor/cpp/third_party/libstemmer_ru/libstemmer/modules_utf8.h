/* libstemmer/modules_utf8.h: UTF-8 Russian-only stemming module list.
 *
 * This file is trimmed for concept_extractor from Snowball's generated modules list.
 */

#include "../src_c/stem_UTF_8_russian.h"

typedef enum {
    ENC_UNKNOWN = 0,
    ENC_UTF_8
} stemmer_encoding_t;

struct stemmer_encoding {
    const char *name;
    stemmer_encoding_t enc;
};

static const struct stemmer_encoding encodings[] = {
    {"UTF_8", ENC_UTF_8},
    {0, ENC_UNKNOWN}
};

struct stemmer_modules {
    const char *name;
    stemmer_encoding_t enc;
    struct SN_env *(*create)(void);
    void (*close)(struct SN_env *);
    int (*stem)(struct SN_env *);
};

static const struct stemmer_modules modules[] = {
    {"ru", ENC_UTF_8, russian_UTF_8_create_env, russian_UTF_8_close_env,
     russian_UTF_8_stem},
    {"rus", ENC_UTF_8, russian_UTF_8_create_env, russian_UTF_8_close_env,
     russian_UTF_8_stem},
    {"russian", ENC_UTF_8, russian_UTF_8_create_env, russian_UTF_8_close_env,
     russian_UTF_8_stem},
    {0, ENC_UNKNOWN, 0, 0, 0}
};

static const char *algorithm_names[] = {"russian", 0};
