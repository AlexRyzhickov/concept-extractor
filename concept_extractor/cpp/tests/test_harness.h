#pragma once
/**
 * @file test_harness.h
 * @brief Minimal test harness: CHECK macros + shared pass/fail counters.
 *
 * Every test file includes this header. Counters are defined in test_main.cpp.
 */

#include <cstdio>
#include <cstdlib>

extern int g_pass;
extern int g_fail;

#define CHECK(cond, msg)                                                          \
    do {                                                                          \
        if (!(cond)) {                                                            \
            std::fprintf(                                                         \
                stderr, "FAIL [%s:%d] %s\n  %s\n", __FILE__, __LINE__, #cond, msg \
            );                                                                    \
            ++g_fail;                                                             \
        } else {                                                                  \
            ++g_pass;                                                             \
        }                                                                         \
    } while (0)

#define CHECK_EQ(a, b, msg)                                         \
    do {                                                            \
        if ((a) != (b)) {                                           \
            std::fprintf(                                           \
                stderr,                                             \
                "FAIL [%s:%d] %s != %s (%d vs %d)\n"                \
                "  %s\n",                                           \
                __FILE__, __LINE__, #a, #b, (int)(a), (int)(b), msg \
            );                                                      \
            ++g_fail;                                               \
        } else {                                                    \
            ++g_pass;                                               \
        }                                                           \
    } while (0)

#define CHECK_VEC_EQ(vec, expected, msg)                                           \
    do {                                                                           \
        bool ok_ = (vec).size() == (expected).size();                              \
        if (ok_) {                                                                 \
            for (size_t i_ = 0; i_ < (vec).size(); ++i_)                           \
                if ((vec)[i_] != (expected)[i_]) {                                 \
                    ok_ = false;                                                   \
                    break;                                                         \
                }                                                                  \
        }                                                                          \
        if (!ok_) {                                                                \
            std::fprintf(                                                          \
                stderr,                                                            \
                "FAIL [%s:%d] vector mismatch\n"                                   \
                "  %s\n  got:      [",                                             \
                __FILE__, __LINE__, msg                                            \
            );                                                                     \
            for (size_t i_ = 0; i_ < (vec).size(); ++i_)                           \
                std::fprintf(stderr, "%s%d", i_ ? ", " : "", (int)(vec)[i_]);      \
            std::fprintf(stderr, "]\n  expected: [");                              \
            for (size_t i_ = 0; i_ < (expected).size(); ++i_)                      \
                std::fprintf(stderr, "%s%d", i_ ? ", " : "", (int)(expected)[i_]); \
            std::fprintf(stderr, "]\n");                                           \
            ++g_fail;                                                              \
        } else {                                                                   \
            ++g_pass;                                                              \
        }                                                                          \
    } while (0)
