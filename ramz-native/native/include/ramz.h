#ifndef RAMZ_H
#define RAMZ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ramz_store ramz_store;
typedef uint64_t ramz_handle;

typedef enum { RAMZ_LZ4 = 1, RAMZ_ZSTD = 2 } ramz_algorithm;
typedef enum {
  RAMZ_OK = 0,
  RAMZ_EINVAL = -1,
  RAMZ_ENOMEM = -2,
  RAMZ_ENOTFOUND = -3,
  RAMZ_ECORRUPT = -4,
  RAMZ_EINCOMPRESSIBLE = -5,
  RAMZ_ESTATE = -6
} ramz_result;

typedef struct {
  ramz_algorithm algorithm;
  size_t min_compress_bytes;      /* Skip tiny objects. Default: 4096. */
  size_t min_savings_bytes;       /* Require net payload savings. Default: 128. */
  size_t max_stored_bytes;        /* 0 = no limit. */
  int zstd_level;                 /* 1..22; default 3. */
  int max_cpu_percent;            /* 1..100; call-level throttle, default 100. */
  int verify_after_compress;      /* Decompress and checksum before release. */
} ramz_config;

typedef struct {
  uint64_t puts, gets, releases;
  uint64_t compressed_blocks, raw_blocks, skipped_blocks;
  uint64_t compression_failures, decompression_failures, integrity_failures;
  uint64_t original_bytes, stored_payload_bytes, metadata_bytes;
  uint64_t compression_ns, decompression_ns;
  uint64_t peak_stored_bytes;
} ramz_stats;

void ramz_config_default(ramz_config *config);
ramz_store *ramz_store_create(const ramz_config *config);
void ramz_store_destroy(ramz_store *store);
ramz_result ramz_store_set_config(ramz_store *store, const ramz_config *config);
/* Always assigns a handle when RAMZ_OK or RAMZ_EINCOMPRESSIBLE is returned.
 * RAMZ_EINCOMPRESSIBLE means the block was retained raw because compression was
 * not a net win; callers must still release the returned handle. */
ramz_result ramz_put(ramz_store *store, const void *data, size_t size, ramz_handle *out_handle);
ramz_result ramz_get(ramz_store *store, ramz_handle handle, void *out, size_t out_size, size_t *out_written);
ramz_result ramz_release(ramz_store *store, ramz_handle handle);
ramz_result ramz_get_stats(ramz_store *store, ramz_stats *out);
const char *ramz_result_string(ramz_result result);
#ifdef RAMZ_ENABLE_TEST_HOOKS
ramz_result ramz_test_corrupt(ramz_store *store, ramz_handle handle);
#endif

#ifdef __cplusplus
}
#endif
#endif
