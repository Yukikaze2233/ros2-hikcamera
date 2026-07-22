#include "hikcamera/shm_types.hpp"

#include <cstring>
#include <format>
#include <expected>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace hikcamera {
namespace ring {

static auto total_size() noexcept -> size_t { return sizeof(SharedRingHeader); }

// Init all sync primitives. On failure, destroys already-initialized ones
// and returns a non-zero error code. Caller must still unmap/unlink.
static auto init_sync_primitives(SharedRingHeader* hdr) -> int {
    // mutex
    {
        pthread_mutexattr_t ma;
        int r = pthread_mutexattr_init(&ma);
        if (r != 0) return r;
        r = pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
        if (r != 0) { pthread_mutexattr_destroy(&ma); return r; }
        r = pthread_mutex_init(&hdr->mutex, &ma);
        pthread_mutexattr_destroy(&ma);
        if (r != 0) return r;
    }
    // condvar
    {
        pthread_condattr_t ca;
        int r = pthread_condattr_init(&ca);
        if (r != 0) { pthread_mutex_destroy(&hdr->mutex); return r; }
        r = pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED);
        if (r != 0) { pthread_condattr_destroy(&ca); pthread_mutex_destroy(&hdr->mutex); return r; }
        r = pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
        if (r != 0) { pthread_condattr_destroy(&ca); pthread_mutex_destroy(&hdr->mutex); return r; }
        r = pthread_cond_init(&hdr->cond, &ca);
        pthread_condattr_destroy(&ca);
        if (r != 0) { pthread_mutex_destroy(&hdr->mutex); return r; }
    }
    // rwlocks per slot
    for (size_t i = 0; i < kShmSlotCount; ++i) {
        pthread_rwlockattr_t ra;
        int r = pthread_rwlockattr_init(&ra);
        if (r != 0) {
            for (size_t j = 0; j < i; ++j) pthread_rwlock_destroy(&hdr->slots[j].lock);
            pthread_cond_destroy(&hdr->cond);
            pthread_mutex_destroy(&hdr->mutex);
            return r;
        }
        r = pthread_rwlockattr_setpshared(&ra, PTHREAD_PROCESS_SHARED);
        if (r != 0) {
            pthread_rwlockattr_destroy(&ra);
            for (size_t j = 0; j < i; ++j) pthread_rwlock_destroy(&hdr->slots[j].lock);
            pthread_cond_destroy(&hdr->cond);
            pthread_mutex_destroy(&hdr->mutex);
            return r;
        }
        r = pthread_rwlock_init(&hdr->slots[i].lock, &ra);
        pthread_rwlockattr_destroy(&ra);
        if (r != 0) {
            for (size_t j = 0; j < i; ++j) pthread_rwlock_destroy(&hdr->slots[j].lock);
            pthread_cond_destroy(&hdr->cond);
            pthread_mutex_destroy(&hdr->mutex);
            return r;
        }
    }
    return 0;
}

auto writer_create(const char* name) -> std::expected<SharedRingHeader*, std::string> {
    if (name == nullptr || std::strlen(name) == 0)
        return std::unexpected{"SHM name is empty"};

    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd == -1)
        return std::unexpected{std::format("shm_open(O_EXCL) '{}': {}", name, strerror(errno))};

    const size_t total = total_size();
    if (ftruncate(fd, static_cast<off_t>(total)) == -1) {
        close(fd); shm_unlink(name);
        return std::unexpected{std::format("ftruncate: {}", strerror(errno))};
    }

    auto* ring = reinterpret_cast<SharedRingHeader*>(
        mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    close(fd);

    if (ring == MAP_FAILED) {
        shm_unlink(name);
        return std::unexpected{std::format("mmap: {}", strerror(errno))};
    }

    int ir = init_sync_primitives(ring);
    if (ir != 0) {
        munmap(ring, total); shm_unlink(name);
        return std::unexpected{std::format("pthread init failed: {}", ir)};
    }

    ring->magic           = kShmMagic;
    ring->version         = kShmVersion;
    ring->layout_size     = static_cast<uint32_t>(total);
    ring->slot_count      = kShmSlotCount;
    ring->max_pixel_bytes = kShmMaxPixelBytes;
    ring->latest_sequence.store(0, std::memory_order_release);
    ring->ready.store(true, std::memory_order_release);
    return ring;
}

auto reader_open(const char* name) -> std::expected<SharedRingHeader*, std::string> {
    if (name == nullptr || std::strlen(name) == 0)
        return std::unexpected{"SHM name is empty"};
    int fd = shm_open(name, O_RDWR, 0);
    if (fd == -1)
        return std::unexpected{std::format("shm_open '{}': {}", name, strerror(errno))};
    const size_t total = total_size();
    auto* ring = reinterpret_cast<SharedRingHeader*>(
        mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    close(fd);
    if (ring == MAP_FAILED)
        return std::unexpected{std::format("mmap: {}", strerror(errno))};

    auto fail = [&](std::string msg) { munmap(ring, total); return std::unexpected{std::move(msg)}; };

    if (!ring->ready.load(std::memory_order_acquire)) return fail("SHM ring not ready");
    if (ring->magic != kShmMagic)  return fail(std::format("Bad magic: 0x{:08X}", ring->magic));
    if (ring->version != kShmVersion) return fail(std::format("Version mismatch: got {}, expected {}", ring->version, kShmVersion));
    if (ring->layout_size != total) return fail(std::format("Layout size mismatch: got {}, expected {}", ring->layout_size, total));
    if (ring->slot_count != kShmSlotCount) return fail(std::format("Slot count mismatch: got {}, expected {}", ring->slot_count, kShmSlotCount));
    if (ring->max_pixel_bytes != kShmMaxPixelBytes) return fail(std::format("Max pixel bytes mismatch: got {}, expected {}", ring->max_pixel_bytes, kShmMaxPixelBytes));
    return ring;
}

auto close(SharedRingHeader* ring, const char* name, bool unlink) -> void {
    if (ring == nullptr) return;
    munmap(ring, total_size());
    if (unlink && name != nullptr && std::strlen(name) > 0) shm_unlink(name);
}

}  // namespace ring
}  // namespace hikcamera
