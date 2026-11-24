#pragma once

#include "contracts.hpp"

#include <iostream>
#include <sstream>
#include <string>

using PrimeCountFunction = decltype(&PrimeCount);
using PiFunction = decltype(&Pi);

inline void run_command(const std::string &command, std::istringstream &input,
                        PrimeCountFunction prime_count, PiFunction pi) {
    std::string extra;
    if (command == "1") {
        int a, b;
        if (!(input >> a >> b) || a < 1 || b < a || (input >> extra)) {
            std::cerr << "введите 1 A B, где 1 <= A <= B\n";
            return;
        }
        std::cout << prime_count(a, b) << '\n';
    } else if (command == "2") {
        int k;
        if (!(input >> k) || k < 1 || (input >> extra)) {
            std::cerr << "введите 2 K, где K >= 1\n";
            return;
        }
        std::cout << pi(k) << '\n';
    } else {
        std::cerr << "неизвестная команда\n";
    }
}
