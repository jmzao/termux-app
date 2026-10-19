#include "ramz.h"

#include <zstd.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

extern "C" {
int LZ4_compressBound(int inputSize);
int LZ4_compress_default(const char *src, char *dst, int srcSize, int dstCapacity);
int LZ4_decompress_safe(const char *src, char *dst, int compressedSize, int dstCapacity);
}

namespace {
constexpr uint64_t kFNVOffset = 1469598103934665603ULL;
constexpr uint64_t kFNVPrime = 1099511628211ULL;

uint64_t checksum(const void *data, size_t size) {
  const auto *p = static_cast<const unsigned char *>(data);
  uint64_t h = kFNVOffset;
  for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= kFNVPrime; }
  return h;
}
uint64_t ns_now() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool valid_config(const ramz_config &c) {
  return (c.algorithm == RAMZ_LZ4 || c.algorithm == RAMZ_ZSTD) &&
         c.min_compress_bytes > 0 && c.zstd_level >= 1 && c.zstd_level <= 22 &&
         c.max_cpu_percent >= 1 && c.max_cpu_percent <= 100;
}
size_t metadata_cost() { return sizeof(ramz_handle) + sizeof(size_t) * 2 + sizeof(uint64_t) + 32; }

struct Block {
  std::vector<unsigned char> payload;
  size_t original_size = 0;
  uint64_t sum = 0;
  ramz_algorithm algorithm = RAMZ_LZ4;
  bool compressed = false;
};
}

struct ramz_store {
  std::mutex mutex;
  ramz_config config{};
  ramz_stats stats{};
  ramz_handle next_handle = 1;
  std::unordered_map<ramz_handle, Block> blocks;
};

extern "C" {
void ramz_config_default(ramz_config *config) {
  if (!config) return;
  config->algorithm = RAMZ_LZ4;
  config->min_compress_bytes = 4096;
  config->min_savings_bytes = 128;
  config->max_stored_bytes = 0;
  config->zstd_level = 3;
  config->max_cpu_percent = 100;
  config->verify_after_compress = 1;
}

ramz_store *ramz_store_create(const ramz_config *input) {
  ramz_config c; ramz_config_default(&c);
  if (input) c = *input;
  if (!valid_config(c)) return nullptr;
  try {
    auto *store = new ramz_store();
    store->config = c;
    return store;
  } catch (...) { return nullptr; }
}

void ramz_store_destroy(ramz_store *store) { delete store; }

ramz_result ramz_store_set_config(ramz_store *store, const ramz_config *config) {
  if (!store || !config || !valid_config(*config)) return RAMZ_EINVAL;
  std::lock_guard<std::mutex> lock(store->mutex);
  store->config = *config;
  return RAMZ_OK;
}

ramz_result ramz_put(ramz_store *store, const void *data, size_t size, ramz_handle *out_handle) {
  if (!store || !data || !out_handle || size == 0) return RAMZ_EINVAL;
  std::lock_guard<std::mutex> lock(store->mutex);
  if (store->next_handle == std::numeric_limits<ramz_handle>::max()) return RAMZ_ESTATE;
  const ramz_config c = store->config;
  Block block;
  block.original_size = size;
  block.sum = checksum(data, size);
  block.algorithm = c.algorithm;
  bool attempted = size >= c.min_compress_bytes;
  bool compressed = false;
  uint64_t started = 0;

  if (attempted && size <= static_cast<size_t>(std::numeric_limits<int>::max())) {
    try {
      size_t bound = 0;
      if (c.algorithm == RAMZ_LZ4) bound = static_cast<size_t>(LZ4_compressBound(static_cast<int>(size)));
      else bound = ZSTD_compressBound(size);
      std::vector<unsigned char> candidate(bound);
      started = ns_now();
      size_t candidate_size = 0;
      bool codec_ok = false;
      if (c.algorithm == RAMZ_LZ4) {
        int n = LZ4_compress_default(static_cast<const char *>(data), reinterpret_cast<char *>(candidate.data()),
                                     static_cast<int>(size), static_cast<int>(candidate.size()));
        if (n > 0) { candidate_size = static_cast<size_t>(n); codec_ok = true; }
      } else {
        size_t n = ZSTD_compress(candidate.data(), candidate.size(), data, size, c.zstd_level);
        if (!ZSTD_isError(n)) { candidate_size = n; codec_ok = true; }
      }
      const uint64_t codec_elapsed = ns_now() - started;
      store->stats.compression_ns += codec_elapsed;
      /* Best-effort per-call CPU duty cap. It deliberately trades latency for less CPU. */
      if (c.max_cpu_percent < 100 && codec_elapsed > 0) {
        const uint64_t sleep_ns = codec_elapsed * static_cast<uint64_t>(100 - c.max_cpu_percent) /
                                  static_cast<uint64_t>(c.max_cpu_percent);
        std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
      }
      if (!codec_ok) {
        store->stats.compression_failures++;
      } else if (candidate_size + c.min_savings_bytes <= size) {
        candidate.resize(candidate_size);
        bool verified = true;
        if (c.verify_after_compress) {
          std::vector<unsigned char> verify(size);
          size_t decoded = 0;
          if (c.algorithm == RAMZ_LZ4) {
            int n = LZ4_decompress_safe(reinterpret_cast<const char *>(candidate.data()), reinterpret_cast<char *>(verify.data()),
                                        static_cast<int>(candidate.size()), static_cast<int>(size));
            if (n >= 0) decoded = static_cast<size_t>(n); else verified = false;
          } else {
            size_t n = ZSTD_decompress(verify.data(), verify.size(), candidate.data(), candidate.size());
            if (!ZSTD_isError(n)) decoded = n; else verified = false;
          }
          verified = verified && decoded == size && checksum(verify.data(), size) == block.sum;
        }
        if (verified) {
          // resize() preserves capacity; copy into a tight vector before releasing the
          // temporary compression-bound allocation, otherwise it can erase RSS savings.
          std::vector<unsigned char> tight(candidate.begin(), candidate.end());
          block.payload.swap(tight);
          block.compressed = true;
          compressed = true;
        } else { store->stats.integrity_failures++; }
      }
    } catch (const std::bad_alloc &) { store->stats.compression_failures++; }
  }

  if (!compressed) {
    try { block.payload.assign(static_cast<const unsigned char *>(data), static_cast<const unsigned char *>(data) + size); }
    catch (const std::bad_alloc &) { return RAMZ_ENOMEM; }
    store->stats.raw_blocks++;
    if (attempted) store->stats.skipped_blocks++;
  } else store->stats.compressed_blocks++;

  const uint64_t projected = store->stats.stored_payload_bytes + block.payload.size() + metadata_cost();
  if (c.max_stored_bytes && projected > c.max_stored_bytes) return RAMZ_ENOMEM;
  ramz_handle handle = store->next_handle++;
  try { store->blocks.emplace(handle, std::move(block)); }
  catch (const std::bad_alloc &) { return RAMZ_ENOMEM; }
  const Block &saved = store->blocks.at(handle);
  store->stats.puts++;
  store->stats.original_bytes += size;
  store->stats.stored_payload_bytes += saved.payload.size();
  store->stats.metadata_bytes += metadata_cost();
  store->stats.peak_stored_bytes = std::max(store->stats.peak_stored_bytes,
      store->stats.stored_payload_bytes + store->stats.metadata_bytes);
  *out_handle = handle;
  return compressed ? RAMZ_OK : (attempted ? RAMZ_EINCOMPRESSIBLE : RAMZ_OK);
}

ramz_result ramz_get(ramz_store *store, ramz_handle handle, void *out, size_t out_size, size_t *out_written) {
  if (!store || !out) return RAMZ_EINVAL;
  std::lock_guard<std::mutex> lock(store->mutex);
  auto it = store->blocks.find(handle);
  if (it == store->blocks.end()) return RAMZ_ENOTFOUND;
  const Block &block = it->second;
  if (out_size < block.original_size) return RAMZ_EINVAL;
  uint64_t started = ns_now();
  bool ok = true;
  size_t decoded = 0;
  if (block.compressed) {
    if (block.algorithm == RAMZ_LZ4) {
      if (block.payload.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
          block.original_size > static_cast<size_t>(std::numeric_limits<int>::max())) ok = false;
      else { int n = LZ4_decompress_safe(reinterpret_cast<const char *>(block.payload.data()), static_cast<char *>(out),
                                         static_cast<int>(block.payload.size()), static_cast<int>(block.original_size));
             if (n < 0) ok = false; else decoded = static_cast<size_t>(n); }
    } else {
      size_t n = ZSTD_decompress(out, out_size, block.payload.data(), block.payload.size());
      if (ZSTD_isError(n)) ok = false; else decoded = n;
    }
  } else { std::memcpy(out, block.payload.data(), block.original_size); decoded = block.original_size; }
  store->stats.decompression_ns += ns_now() - started;
  if (!ok || decoded != block.original_size || checksum(out, block.original_size) != block.sum) {
    store->stats.decompression_failures++;
    if (ok) store->stats.integrity_failures++;
    return RAMZ_ECORRUPT;
  }
  store->stats.gets++;
  if (out_written) *out_written = decoded;
  return RAMZ_OK;
}

ramz_result ramz_release(ramz_store *store, ramz_handle handle) {
  if (!store) return RAMZ_EINVAL;
  std::lock_guard<std::mutex> lock(store->mutex);
  auto it = store->blocks.find(handle);
  if (it == store->blocks.end()) return RAMZ_ENOTFOUND;
  store->stats.original_bytes -= it->second.original_size;
  store->stats.stored_payload_bytes -= it->second.payload.size();
  store->stats.metadata_bytes -= metadata_cost();
  store->blocks.erase(it);
  store->stats.releases++;
  return RAMZ_OK;
}

ramz_result ramz_get_stats(ramz_store *store, ramz_stats *out) {
  if (!store || !out) return RAMZ_EINVAL;
  std::lock_guard<std::mutex> lock(store->mutex); *out = store->stats; return RAMZ_OK;
}

const char *ramz_result_string(ramz_result r) {
  switch (r) { case RAMZ_OK: return "ok"; case RAMZ_EINVAL: return "invalid argument"; case RAMZ_ENOMEM: return "out of memory or store limit"; case RAMZ_ENOTFOUND: return "unknown handle"; case RAMZ_ECORRUPT: return "corrupt block"; case RAMZ_EINCOMPRESSIBLE: return "stored raw: insufficient savings"; case RAMZ_ESTATE: return "invalid store state"; default: return "unknown error"; }
}
#ifdef RAMZ_ENABLE_TEST_HOOKS
ramz_result ramz_test_corrupt(ramz_store *store, ramz_handle handle) {
  if (!store) return RAMZ_EINVAL;
  std::lock_guard<std::mutex> lock(store->mutex);
  auto it = store->blocks.find(handle);
  if (it == store->blocks.end() || it->second.payload.empty()) return RAMZ_ENOTFOUND;
  it->second.payload[0] ^= 0xA5; return RAMZ_OK;
}
#endif
}
