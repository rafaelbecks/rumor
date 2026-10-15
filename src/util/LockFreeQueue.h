#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace glow {

/**
 * Single-producer / single-consumer ring buffer.
 * MIDI/control thread pushes; audio thread pops. No locks, no allocations.
 */
template <typename T, size_t CapacityPowerOfTwo>
class LockFreeQueue {
 public:
  static_assert((CapacityPowerOfTwo & (CapacityPowerOfTwo - 1)) == 0,
                "Capacity must be a power of two");

  bool push(const T& item) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next = (head + 1) & kMask;
    if (next == tail_.load(std::memory_order_acquire)) {
      return false;  // full
    }
    buffer_[head] = item;
    head_.store(next, std::memory_order_release);
    return true;
  }

  bool pop(T& out) {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return false;  // empty
    }
    out = buffer_[tail];
    tail_.store((tail + 1) & kMask, std::memory_order_release);
    return true;
  }

 private:
  static constexpr size_t kMask = CapacityPowerOfTwo - 1;
  T buffer_[CapacityPowerOfTwo]{};
  std::atomic<size_t> head_{0};
  std::atomic<size_t> tail_{0};
};

}  // namespace glow
