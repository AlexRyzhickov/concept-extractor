#include <cstdio>

int g_pass = 0;
int g_fail = 0;

void RunPersonExtractorRulesTests();

int main() {
    RunPersonExtractorRulesTests();

    std::printf("\n======================================\n");
    std::printf("  %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        std::printf("  ALL TESTS PASSED\n");
    }
    std::printf("======================================\n");
    return g_fail > 0 ? 1 : 0;
}
