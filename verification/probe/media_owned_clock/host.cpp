#include "clock.h"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

using media_owned::Clock;
static void report(const Clock& c, int result, const media_owned::Update& u = {}) {
    const auto ms = c.milliseconds();
    std::uint64_t seconds_bits = 0, scaled_bits = 0;
    std::memcpy(&seconds_bits, &ms.seconds, sizeof seconds_bits);
    std::memcpy(&scaled_bits, &ms.scaled, sizeof scaled_bits);
    std::cout << "{\"result\":" << result << ",\"generation\":" << c.generation() << ",\"operation\":" << c.operation()
              << ",\"rate\":" << c.rate_numerator() << ",\"armed\":" << c.armed() << ",\"intent\":" << c.intent()
              << ",\"paused\":" << c.paused() << ",\"queued\":" << c.queued() << ",\"end\":" << int(c.end())
              << ",\"selected\":" << u.selected << ",\"token\":" << u.frame.token << ",\"start\":" << u.frame.start
              << ",\"stop\":" << u.frame.end << ",\"consumed\":" << u.consumed << ",\"late\":" << u.late
              << ",\"superseded\":" << u.superseded << ",\"ms\":" << ms.truncated << ",\"in_range\":" << ms.in_range
              << ",\"seconds_bits\":" << seconds_bits << ",\"scaled_bits\":" << scaled_bits << ",\"numerator\":\""
              << std::hex << std::setfill('0');
    for (unsigned i = 8; i-- > 0;) std::cout << std::setw(8) << c.numerator().word[i];
    std::cout << std::dec << "\"}\n";
}
int main(int argc, char**) {
    if (argc > 1) {
        constexpr unsigned iterations = 100000;
        Clock c(10000000);
        c.begin(1, 0, INT32_MAX, false, 0);
        c.submit({c.generation(), 1, 0, INT64_MAX});
        c.update(0);
        unsigned accepted = 0;
        const auto before = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < iterations; ++i) {
            // A full bounded batch, including two overlapping candidates.
            const auto generation = c.generation();
            c.submit({generation, 2, 0, INT64_MAX});
            c.submit({generation, 3, 0, INT64_MAX});
            c.submit({generation, 4, 0, INT64_MAX});
            accepted += c.update(i).consumed;
        }
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - before)
                            .count();
        std::cout << "{\"iterations\":" << iterations << ",\"consumed\":" << accepted << ",\"nanoseconds\":" << ns
                  << ",\"core_bytes\":" << sizeof c << "}\n";
        return accepted == iterations * Clock::capacity ? 0 : 1;
    }
    std::uint64_t frequency;
    if (!(std::cin >> frequency)) return 2;
    Clock c(frequency);
    std::string command;
    while (std::cin >> command) {
        std::uint64_t now = 0, operation = 0, generation = 0;
        std::int64_t start = 0;
        std::int32_t end = 0, rate = 0;
        int loop = 0, result = 0;
        media_owned::Update update;
        if (command == "begin") {
            std::cin >> operation >> start >> end >> loop >> now;
            result = c.begin(operation, start, end, loop != 0, now);
        } else if (command == "seek") {
            std::cin >> start >> end >> now;
            result = c.seek(start, end, now);
        } else if (command == "rate") {
            std::cin >> rate >> now;
            result = c.set_rate(rate, now);
        } else if (command == "frame") {
            media_owned::Frame f;
            std::cin >> f.generation >> f.token >> f.start >> f.end;
            result = int(c.submit(f));
        } else if (command == "eof") {
            std::cin >> generation;
            result = c.provider_eof(generation);
        } else {
            std::cin >> now;
            if (command == "update") {
                update = c.update(now);
                result = update.accepted;
            } else if (command == "pause")
                result = c.pause(now);
            else if (command == "resume")
                result = c.resume(now);
            else if (command == "stop")
                result = c.stop(now);
            else if (command == "loop")
                result = c.restart_loop(now);
            else
                return 2;
        }
        if (!std::cin) return 2;
        report(c, result, update);
    }
}
