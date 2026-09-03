#include "terminal_core.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using pocketssh::TerminalCore;

constexpr size_t kBytesPerMiB = 1024 * 1024;
constexpr size_t kSamples = 11;

std::string ascii_scroll_stream()
{
    std::string stream;
    stream.reserve(kBytesPerMiB);
    while (stream.size() < kBytesPerMiB) stream += "terminal scrolling sample payload...............................................\r\n";
    stream.resize(kBytesPerMiB);
    return stream;
}

std::string ansi_utf8_scroll_stream()
{
    std::string stream;
    stream.reserve(kBytesPerMiB);
    while (stream.size() < kBytesPerMiB) stream += "\x1b[31m\xE2\x9C\x93\x1b[0m terminal ansi utf8 scrolling payload........................................\r\n";
    stream.resize(kBytesPerMiB);
    return stream;
}

void feed_in_chunks(TerminalCore &term, const std::string &stream)
{
    for (size_t offset = 0; offset < stream.size();) {
        const size_t count = std::min<size_t>(4096, stream.size() - offset);
        term.feed(stream.data() + offset, count);
        offset += count;
    }
}

uint64_t elapsed_us(const std::function<void()> &operation)
{
    const auto started = Clock::now();
    operation();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
}

struct BenchmarkResult {
    const char *name;
    std::array<uint64_t, kSamples> samples_us_per_mib{};
};

uint64_t scale_per_mib(uint64_t elapsed_us_value, size_t logical_bytes)
{
    return logical_bytes == 0 ? 0 : elapsed_us_value * kBytesPerMiB / logical_bytes;
}

BenchmarkResult benchmark_screen_fill()
{
    BenchmarkResult result{"screen_fill"};
    const std::string block(512, 'x');
    for (size_t sample = 0; sample < kSamples; ++sample) {
        TerminalCore term(80, 24, 512);
        result.samples_us_per_mib[sample] = scale_per_mib(elapsed_us([&] {
            for (size_t offset = 0; offset < kBytesPerMiB; offset += block.size()) {
                term.reset();
                term.feed(block.data(), block.size());
            }
        }), kBytesPerMiB);
    }
    return result;
}

BenchmarkResult benchmark_stream(const char *name, const std::string &stream)
{
    BenchmarkResult result{name};
    for (size_t sample = 0; sample < kSamples; ++sample) {
        TerminalCore term(80, 24, 512);
        result.samples_us_per_mib[sample] = scale_per_mib(elapsed_us([&] { feed_in_chunks(term, stream); }), stream.size());
    }
    return result;
}

BenchmarkResult benchmark_viewport()
{
    BenchmarkResult result{"scrollback_viewport"};
    constexpr size_t kSteps = 16384;
    constexpr size_t kLogicalBytes = kSteps * 24 * 80;
    const std::string stream = ascii_scroll_stream();
    for (size_t sample = 0; sample < kSamples; ++sample) {
        TerminalCore term(80, 24, 512);
        feed_in_chunks(term, stream);
        result.samples_us_per_mib[sample] = scale_per_mib(elapsed_us([&] {
            for (size_t step = 0; step < kSteps; ++step) {
                term.scroll_view((step & 1) == 0 ? 1 : -1);
                for (size_t row = 0; row < term.rows(); ++row) (void)term.row(row);
            }
        }), kLogicalBytes);
    }
    return result;
}

uint64_t median(std::array<uint64_t, kSamples> values)
{
    std::sort(values.begin(), values.end());
    return values[kSamples / 2];
}

uint64_t p95(std::array<uint64_t, kSamples> values)
{
    std::sort(values.begin(), values.end());
    return values[(kSamples * 95 + 99) / 100 - 1];
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "usage: benchmark_terminal_core <output-json>\n";
        return 2;
    }
    const std::string ascii = ascii_scroll_stream();
    const std::string ansi_utf8 = ansi_utf8_scroll_stream();
    const std::array<BenchmarkResult, 4> results = {
        benchmark_screen_fill(),
        benchmark_stream("ascii_scroll", ascii),
        benchmark_stream("ansi_utf8_scroll", ansi_utf8),
        benchmark_viewport(),
    };

    std::ofstream output(argv[1]);
    if (!output) return 2;
    output << "{\n  \"schema\": 1,\n  \"unit\": \"us_per_mib\",\n  \"samples\": 11,\n  \"benchmarks\": [\n";
    for (size_t index = 0; index < results.size(); ++index) {
        const auto &result = results[index];
        output << "    {\"name\": \"" << result.name << "\", \"median\": " << median(result.samples_us_per_mib)
               << ", \"p95\": " << p95(result.samples_us_per_mib) << ", \"values\": [";
        for (size_t sample = 0; sample < result.samples_us_per_mib.size(); ++sample) {
            if (sample != 0) output << ", ";
            output << result.samples_us_per_mib[sample];
        }
        output << "]}" << (index + 1 == results.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    std::cout << "saved host benchmark: " << argv[1] << "\n";
    return 0;
}
