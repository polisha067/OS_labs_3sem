#include "commands.hpp"

#include <dlfcn.h>

class Library {
  public:
    Library() = default;
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;

    ~Library() {
        if (handle != nullptr) {
            dlclose(handle);
        }
    }

    bool load(const char *path) {
        void *new_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (new_handle == nullptr) {
            std::cerr << dlerror() << '\n';
            return false;
        }

        auto new_prime_count = reinterpret_cast<PrimeCountFunction>(
            dlsym(new_handle, "PrimeCount"));
        auto new_pi = reinterpret_cast<PiFunction>(dlsym(new_handle, "Pi"));
        if (new_prime_count == nullptr || new_pi == nullptr) {
            std::cerr << "в библиотеке " << path << " нет нужных функций\n";
            dlclose(new_handle);
            return false;
        }

        if (handle != nullptr) {
            dlclose(handle);
        }
        handle = new_handle;
        prime_count = new_prime_count;
        pi = new_pi;
        return true;
    }

    PrimeCountFunction prime_count = nullptr;
    PiFunction pi = nullptr;

  private:
    void *handle = nullptr;
};

int main() {
    const char *paths[] = {"./libimpl1." LIB_EXT, "./libimpl2." LIB_EXT};
    int current = 0;
    Library library;
    if (!library.load(paths[current])) {
        return 1;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string command;
        if (!(input >> command)) {
            continue;
        }
        if (command == "0") {
            std::string extra;
            if (input >> extra) {
                std::cerr << "команда 0 вводится без аргументов\n";
                continue;
            }
            int next = 1 - current;
            if (library.load(paths[next])) {
                current = next;
                std::cout << "реализация " << current + 1 << '\n';
            }
        } else {
            run_command(command, input, library.prime_count, library.pi);
        }
    }
}
