#include "contracts.hpp"

#include <algorithm>

int PrimeCount(int a, int b)
{
    int count = 0;
    for (long long number = std::max(a, 2); number <= b; ++number) {
        bool prime = true;
        for (int divisor = 2; divisor < number; ++divisor) {
            if (number % divisor == 0) {
                prime = false;
                break;
            }
        }
        if (prime) {
            ++count;
        }
    }
    return count;
}

float Pi(int k)
{
    double sum = 0.0;
    double sign = 1.0;
    for (int i = 0; i < k; ++i) {
        sum += sign / (2.0 * i + 1.0);
        sign = -sign;
    }
    return static_cast<float>(4.0 * sum);
}
