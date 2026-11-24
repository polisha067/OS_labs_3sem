#include "contracts.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

int PrimeCount(int a, int b)
{
    if (b < 2 || a > b) {
        return 0;
    }

    std::vector<bool> prime(static_cast<std::size_t>(b) + 1, true);
    for (int divisor = 2; divisor <= b / divisor; ++divisor) {
        if (prime[divisor]) {
            for (long long multiple = 1LL * divisor * divisor;
                 multiple <= b; multiple += divisor) {
                prime[multiple] = false;
            }
        }
    }

    int count = 0;
    for (long long number = std::max(a, 2); number <= b; ++number) {
        if (prime[number]) {
            ++count;
        }
    }
    return count;
}

float Pi(int k)
{
    double product = 2.0;
    for (int i = 0; i < k; ++i) {
        double n = 2.0 * (i + 1);
        product *= (n / (n - 1.0)) * (n / (n + 1.0));
    }
    return static_cast<float>(product);
}
