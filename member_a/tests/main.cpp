#include "test.hpp"

int main(int argc, char** argv) {
    const std::pair<const char*, void(*)()> suites[] = {
        {"validation", test::validation}, {"generator", test::generator},
        {"comparison", test::comparison}, {"chunks", test::chunks},
        {"handoff", test::handoff}, {"variants", test::variants}, {"hub", test::hub}
    };
    try {
        if (argc == 2 && std::string(argv[1]) == "grid") {
            test::grid();
            return 0;
        }
        if (argc > 2) throw std::invalid_argument("usage: a_correctness [suite]");
        for (const auto& suite : suites) {
            if (argc == 1 || std::string(argv[1]) == suite.first) {
                suite.second();
                std::cout << "PASS " << suite.first << '\n';
                if (argc == 2) return 0;
            }
        }
        if (argc == 2) throw std::invalid_argument("unknown test suite");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
