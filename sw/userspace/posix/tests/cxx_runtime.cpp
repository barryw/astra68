#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

namespace {

int constructed;
thread_local int local_value = 7;

struct Lifetime {
    Lifetime() { constructed = 11; }
    ~Lifetime() { constructed = 0; }
} lifetime;

struct Base {
    virtual ~Base() = default;
};

struct Derived final : Base {
    int value = 19;
};

} // namespace

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (constructed != 11)
        return 1;

    std::vector<int> values{2, 3, 5, 7};
    std::string text = "astra";
    if (values.size() != 4 || text + "68" != "astra68")
        return 2;

    Derived object;
    Base *base = &object;
    auto *derived = dynamic_cast<Derived *>(base);
    if (derived == nullptr || derived->value != 19 ||
        typeid(*base) != typeid(Derived))
        return 3;

    try {
        throw std::runtime_error("caught");
    } catch (const std::runtime_error &error) {
        if (std::string(error.what()) != "caught")
            return 4;
    }

    std::mutex mutex;
    std::condition_variable changed;
    bool ready = false;
    int worker_value = 0;
    std::thread worker([&] {
        local_value = 23;
        {
            std::lock_guard<std::mutex> lock(mutex);
            worker_value = local_value;
            ready = true;
        }
        changed.notify_one();
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return ready; });
    }
    worker.join();

    if (worker_value != 23 || local_value != 7)
        return 5;

    std::puts("ASTRA C++ PASS");
    return 0;
}
