#include "ramz.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

static uint64_t kb(const char *key) {
  FILE *f = std::fopen("/proc/self/status", "r"); if (!f) return 0;
  char line[256]; unsigned long value = 0;
  while (std::fgets(line, sizeof(line), f)) {
    if (std::strcmp(key, "VmRSS:") == 0 && std::sscanf(line, "VmRSS: %lu kB", &value) == 1) { std::fclose(f); return value; }
    if (std::strcmp(key, "VmSize:") == 0 && std::sscanf(line, "VmSize: %lu kB", &value) == 1) { std::fclose(f); return value; }
  }
  std::fclose(f); return 0;
}
static uint64_t rss_kb() { return kb("VmRSS:"); }
static uint64_t vms_kb() { return kb("VmSize:"); }
static uint64_t cpu_ns() {
  rusage r{}; getrusage(RUSAGE_SELF, &r);
  return (static_cast<uint64_t>(r.ru_utime.tv_sec + r.ru_stime.tv_sec) * 1000000000ULL) +
         (static_cast<uint64_t>(r.ru_utime.tv_usec + r.ru_stime.tv_usec) * 1000ULL);
}
static void fill_data(const std::string &kind, unsigned char *d, size_t n) {
  std::mt19937_64 random(0xC0FFEE);
  if (kind == "random") for (size_t i=0; i<n; ++i) d[i]=static_cast<unsigned char>(random());
  else if (kind == "structured") for (size_t i=0; i<n; ++i) d[i]=static_cast<unsigned char>((i%8192<6000)?('A'+(i/113)%4):random());
  else for (size_t i=0; i<n; ++i) d[i]=static_cast<unsigned char>((i/256)%11);
}
static double ms(uint64_t ns) { return static_cast<double>(ns)/1000000.0; }
static void trim_heap() {
#if defined(__GLIBC__)
  malloc_trim(0);
#endif
}
int main(int argc, char **argv) {
  size_t bytes=32ULL*1024*1024; ramz_algorithm alg=RAMZ_LZ4;
  for (int i=1; i<argc; ++i) { std::string a=argv[i]; if(a=="--bytes"&&i+1<argc) bytes=std::strtoull(argv[++i],nullptr,10); else if(a=="--algorithm"&&i+1<argc) alg=std::string(argv[++i])=="zstd"?RAMZ_ZSTD:RAMZ_LZ4; }
  const long page = sysconf(_SC_PAGESIZE);
  if (bytes == 0 || page <= 0) return 2;
  std::cout << "ramz benchmark\nalgorithm=" << (alg==RAMZ_ZSTD?"zstd":"lz4") << "\nrequested_bytes=" << bytes << "\n";
  for (const std::string kind : {"repetitive", "structured", "random"}) {
    auto *data = static_cast<unsigned char *>(mmap(nullptr, bytes, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0));
    if (data == MAP_FAILED) { std::perror("mmap source"); return 1; }
    fill_data(kind, data, bytes); // Touch every page: RSS reflects a real resident source payload.
    const uint64_t baseline_rss=rss_kb(), baseline_vms=vms_kb();
    ramz_config c; ramz_config_default(&c); c.algorithm=alg; c.min_compress_bytes=1; c.min_savings_bytes=1; c.verify_after_compress=1;
    ramz_store *store=ramz_store_create(&c); if (!store) return 1;
    ramz_handle handle=0; const uint64_t put_cpu_start=cpu_ns(); auto put_wall_start=std::chrono::steady_clock::now();
    const ramz_result put=ramz_put(store,data,bytes,&handle);
    const auto put_wall_end=std::chrono::steady_clock::now(); const uint64_t put_cpu_end=cpu_ns();
    // mmap source is explicitly released only after ramz_put completed its verified copy.
    munmap(data, bytes); trim_heap();
    ramz_stats st{}; ramz_get_stats(store,&st); const uint64_t compact_rss=rss_kb(), compact_vms=vms_kb();
    auto *out=static_cast<unsigned char *>(mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    if (out == MAP_FAILED) return 1;
    const uint64_t get_cpu_start=cpu_ns(); auto get_wall_start=std::chrono::steady_clock::now();
    const ramz_result get=ramz_get(store,handle,out,bytes,nullptr);
    const auto get_wall_end=std::chrono::steady_clock::now(); const uint64_t get_cpu_end=cpu_ns();
    volatile unsigned char proof=out[bytes-1]; (void)proof; munmap(out,bytes);
    const bool valid=get==RAMZ_OK;
    const long long rss_delta=static_cast<long long>(compact_rss)-static_cast<long long>(baseline_rss);
    std::cout << "case=" << kind << " put=" << ramz_result_string(put) << " integrity=" << (valid?"ok":"failed") << "\n"
      << "  rss_baseline_kb=" << baseline_rss << " rss_after_compact_kb=" << compact_rss << " rss_delta_kb=" << rss_delta << "\n"
      << "  vms_baseline_kb=" << baseline_vms << " vms_after_compact_kb=" << compact_vms << "\n"
      << "  original_bytes=" << st.original_bytes << " stored_payload_bytes=" << st.stored_payload_bytes << " metadata_estimate_bytes=" << st.metadata_bytes << "\n"
      << "  net_accounted_bytes=" << (st.stored_payload_bytes+st.metadata_bytes) << " compression_cpu_ms=" << ms(put_cpu_end-put_cpu_start) << " compression_wall_ms=" << std::chrono::duration<double,std::milli>(put_wall_end-put_wall_start).count() << "\n"
      << "  decompression_cpu_ms=" << ms(get_cpu_end-get_cpu_start) << " access_latency_wall_ms=" << std::chrono::duration<double,std::milli>(get_wall_end-get_wall_start).count() << "\n";
    ramz_store_destroy(store);
  }
}
