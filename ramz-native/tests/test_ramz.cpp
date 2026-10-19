#include "ramz.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

static std::vector<unsigned char> repetitive(size_t n) { std::vector<unsigned char> x(n); for(size_t i=0;i<n;++i)x[i]=static_cast<unsigned char>((i/128)%7); return x; }
static std::vector<unsigned char> random_bytes(size_t n) { std::mt19937_64 r(0xBAD5EED); std::vector<unsigned char>x(n);for(auto &b:x)b=static_cast<unsigned char>(r());return x; }

int main() {
  ramz_config c; ramz_config_default(&c); c.algorithm=RAMZ_LZ4; c.min_compress_bytes=1; c.min_savings_bytes=1;
  ramz_store *s=ramz_store_create(&c); assert(s);
  auto compressible=repetitive(1<<20); auto incompressible=random_bytes(1<<20);
  ramz_handle a=0,b=0; assert(ramz_put(s,compressible.data(),compressible.size(),&a)==RAMZ_OK);
  auto rb=ramz_put(s,incompressible.data(),incompressible.size(),&b); assert(rb==RAMZ_EINCOMPRESSIBLE || rb==RAMZ_OK);
  std::vector<unsigned char> out(1<<20); size_t n=0;
  assert(ramz_get(s,a,out.data(),out.size(),&n)==RAMZ_OK && n==out.size() && out==compressible);
  assert(ramz_get(s,b,out.data(),out.size(),&n)==RAMZ_OK && out==incompressible);
  assert(ramz_get(s,987654,out.data(),out.size(),&n)==RAMZ_ENOTFOUND);
  assert(ramz_get(s,a,out.data(),100,&n)==RAMZ_EINVAL);
#ifdef RAMZ_ENABLE_TEST_HOOKS
  assert(ramz_test_corrupt(s,a)==RAMZ_OK); assert(ramz_get(s,a,out.data(),out.size(),&n)==RAMZ_ECORRUPT);
#endif
  assert(ramz_release(s,a)==RAMZ_OK); assert(ramz_release(s,b)==RAMZ_OK);
  ramz_store_destroy(s);

  // Enforce a hard store limit before creating a handle or retaining a copy.
  c.max_stored_bytes=1; s=ramz_store_create(&c); assert(s);
  assert(ramz_put(s,compressible.data(),compressible.size(),&a)==RAMZ_ENOMEM);
  assert(ramz_release(s,1)==RAMZ_ENOTFOUND); ramz_store_destroy(s);

  c.max_stored_bytes=0; c.algorithm=RAMZ_ZSTD; s=ramz_store_create(&c); assert(s); assert(ramz_put(s,compressible.data(),compressible.size(),&a)==RAMZ_OK);
  assert(ramz_get(s,a,out.data(),out.size(),&n)==RAMZ_OK && out==compressible); ramz_store_destroy(s);

  // Independent writers/readers share a store safely; each uses explicit ownership.
  c.algorithm=RAMZ_LZ4; s=ramz_store_create(&c); assert(s);
  std::vector<std::thread> workers;
  for (int i=0; i<8; ++i) workers.emplace_back([&] {
    auto data=repetitive(65536); std::vector<unsigned char> recovered(data.size()); ramz_handle h=0; size_t written=0;
    assert(ramz_put(s,data.data(),data.size(),&h)==RAMZ_OK);
    assert(ramz_get(s,h,recovered.data(),recovered.size(),&written)==RAMZ_OK && written==data.size() && recovered==data);
    assert(ramz_release(s,h)==RAMZ_OK);
  });
  for (auto &worker : workers) worker.join();
  ramz_stats stress{}; assert(ramz_get_stats(s,&stress)==RAMZ_OK && stress.puts==8 && stress.gets==8 && stress.releases==8);
  ramz_store_destroy(s);
  std::cout << "PASS: LZ4/Zstd recovery, raw fallback, limits, corruption detection, concurrent stress\n";
}
