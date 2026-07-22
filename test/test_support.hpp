#pragma once
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "hikcamera/shared_frame_writer.hpp"
#include "hikcamera/shared_frame_reader.hpp"
#include "hikcamera/shared_frame.hpp"
#include "hikcamera/shm_types.hpp"
using namespace hikcamera;
using namespace std::chrono_literals;

// Thread-safe failure counter.
static std::atomic<int> g_failures{0};

inline void record_failure(const char* func, int line, const char* msg) {
    std::cerr << "FAIL [" << func << ":" << line << "] " << msg << '\n';
    g_failures.fetch_add(1, std::memory_order_relaxed);
}

#define FAIL(msg) record_failure(__func__, __LINE__, msg)

// Fail-fast: abort the test immediately.
#define REQUIRE(cond) do { if (!(cond)) { FAIL("REQUIRE failed: " #cond); return; } } while (0)

// Soft check: record failure but continue.
#define CHECK(cond)  do { if (!(cond)) FAIL("CHECK failed: " #cond); } while (0)
#define CHECK_EQ(a,b) CHECK((a)==(b))
#define CHECK_GT(a,b) CHECK((a)>(b))

// Thread-safe failure capture for worker threads.
#define TCHECK(cond, fail_flag) do { if (!(cond)) { fail_flag.store(1, std::memory_order_relaxed); return; } } while (0)

inline void fill_pattern(std::span<unsigned char> buf, uint64_t seq) {
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = static_cast<unsigned char>((seq * 7 + i * 13) & 0xFF);
    if (buf.size() >= 24) {
        std::memcpy(buf.data(), &seq, sizeof(seq));
        const char m[] = "SHM_TEST_MARKER";
        std::memcpy(buf.data() + buf.size() - 16, m, 16);
    }
}

inline void verify_pattern(std::span<const unsigned char> data, uint64_t seq) {
    if (data.size() >= 24) {
        uint64_t s = 0;
        std::memcpy(&s, data.data(), sizeof(s));
        CHECK_EQ(s, seq);
        const char m[] = "SHM_TEST_MARKER";
        CHECK_EQ(std::memcmp(data.data() + data.size() - 16, m, 16), 0);
    }
}

inline std::string make_name(const char* suffix) {
    return std::string{"/hik_test_"} + suffix + "_" + std::to_string(getpid());
}
