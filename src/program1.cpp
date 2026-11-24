#include "commands.hpp"

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string command;
        if (input >> command) {
            run_command(command, input, PrimeCount, Pi);
        }
    }
}
