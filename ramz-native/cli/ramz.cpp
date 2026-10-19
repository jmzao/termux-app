#include "ramz.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
std::string config_path() {
  const char *home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/.config/ramz.conf";
}

bool parse_size(const std::string &text, size_t *out) {
  if (!out || text.empty() || text[0] == '-') return false;
  errno = 0;
  char *end = nullptr;
  const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
  if (errno == ERANGE || end == text.c_str() || *end != '\0' ||
      value > static_cast<unsigned long long>(SIZE_MAX)) return false;
  *out = static_cast<size_t>(value);
  return true;
}

bool parse_int(const std::string &text, int *out) {
  if (!out || text.empty()) return false;
  errno = 0;
  char *end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (errno == ERANGE || end == text.c_str() || *end != '\0' ||
      value < INT_MIN || value > INT_MAX) return false;
  *out = static_cast<int>(value);
  return true;
}

bool make_parent_directory(const std::string &path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return true;
  const std::string parent = path.substr(0, slash);
  std::string current;
  size_t start = 0;
  if (parent[0] == '/') { current = "/"; start = 1; }
  while (start < parent.size()) {
    const size_t next = parent.find('/', start);
    const std::string part = parent.substr(start, next == std::string::npos ? std::string::npos : next - start);
    if (!part.empty()) {
      if (!current.empty() && current.back() != '/') current += '/';
      current += part;
      if (mkdir(current.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    if (next == std::string::npos) break;
    start = next + 1;
  }
  return true;
}

void defaults(ramz_config &c) { ramz_config_default(&c); }

void load(ramz_config &c) {
  std::ifstream f(config_path());
  if (!f) return;
  std::string key, value;
  while (std::getline(f, key, '=')) {
    if (!std::getline(f, value)) break;
    if (key == "algorithm") {
      if (value == "lz4") c.algorithm = RAMZ_LZ4;
      else if (value == "zstd") c.algorithm = RAMZ_ZSTD;
    } else if (key == "block_bytes") {
      parse_size(value, &c.min_compress_bytes);
    } else if (key == "min_savings") {
      parse_size(value, &c.min_savings_bytes);
    } else if (key == "max_stored_bytes") {
      parse_size(value, &c.max_stored_bytes);
    } else if (key == "zstd_level") {
      parse_int(value, &c.zstd_level);
    } else if (key == "max_cpu_percent") {
      parse_int(value, &c.max_cpu_percent);
    }
  }
}

void print_config(const ramz_config &c) {
  std::cout << "algorithm=" << (c.algorithm == RAMZ_ZSTD ? "zstd" : "lz4") << '\n'
            << "block_bytes=" << c.min_compress_bytes << '\n'
            << "min_savings=" << c.min_savings_bytes << '\n'
            << "max_stored_bytes=" << c.max_stored_bytes << '\n'
            << "zstd_level=" << c.zstd_level << '\n'
            << "max_cpu_percent=" << c.max_cpu_percent << '\n';
}

void help() {
  std::cout << "ramz: explicit userspace compressed-block store\n\n"
            << "Usage:\n  ramz status\n  ramz stats\n  ramz benchmark [--bytes N] [--algorithm lz4|zstd]\n"
            << "  ramz config [--algorithm lz4|zstd] [--block-bytes N] [--min-savings N]\n"
            << "              [--max-stored-bytes N] [--zstd-level 1..22] [--max-cpu-percent 1..100]\n\n"
            << "config writes a local profile used by this CLI. It never changes arbitrary processes.\n"
            << "RAMZ-aware programs configure their own in-process store through libramz.\n";
}

bool has_option(int argc, char **argv, const char *option) {
  for (int i = 2; i < argc; ++i) if (std::strcmp(argv[i], option) == 0) return true;
  return false;
}
}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) { help(); return 2; }
  const std::string command = argv[1];
  ramz_config c;
  defaults(c);
  load(c);

  if (command == "status") {
    std::cout << "ramz mode=explicit-blocks root=not-required transparent-process-compression=disabled\n";
    print_config(c);
    return 0;
  }
  if (command == "stats") {
    std::cout << "scope=ramz CLI process only\noriginal_bytes=0\nstored_payload_bytes=0\n"
              << "metadata_bytes=0\ncompressed_blocks=0\n";
    return 0;
  }
  if (command == "benchmark") {
    std::vector<char *> args;
    args.push_back(const_cast<char *>("ramz-benchmark"));
    for (int i = 2; i < argc; ++i) args.push_back(argv[i]);
    std::string configured_algorithm;
    if (!has_option(argc, argv, "--algorithm")) {
      configured_algorithm = c.algorithm == RAMZ_ZSTD ? "zstd" : "lz4";
      args.push_back(const_cast<char *>("--algorithm"));
      args.push_back(configured_algorithm.data());
    }
    args.push_back(nullptr);
    const pid_t child = fork();
    if (child < 0) { std::cerr << "cannot start benchmark\n"; return 1; }
    if (child == 0) { execvp(args[0], args.data()); _exit(127); }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) { std::cerr << "benchmark wait failed\n"; return 1; }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
  }
  if (command != "config") { help(); return 2; }

  for (int i = 2; i < argc; ++i) {
    auto require = [&](const char *name) -> const char * {
      if (i + 1 >= argc) { std::cerr << "missing value for " << name << '\n'; std::exit(2); }
      return argv[++i];
    };
    const std::string arg = argv[i];
    if (arg == "--algorithm") {
      const std::string v = require("--algorithm");
      if (v == "lz4") c.algorithm = RAMZ_LZ4;
      else if (v == "zstd") c.algorithm = RAMZ_ZSTD;
      else { std::cerr << "algorithm must be lz4 or zstd\n"; return 2; }
    } else if (arg == "--block-bytes") {
      if (!parse_size(require("--block-bytes"), &c.min_compress_bytes)) { std::cerr << "invalid block size\n"; return 2; }
    } else if (arg == "--min-savings") {
      if (!parse_size(require("--min-savings"), &c.min_savings_bytes)) { std::cerr << "invalid savings\n"; return 2; }
    } else if (arg == "--max-stored-bytes") {
      if (!parse_size(require("--max-stored-bytes"), &c.max_stored_bytes)) { std::cerr << "invalid storage limit\n"; return 2; }
    } else if (arg == "--zstd-level") {
      if (!parse_int(require("--zstd-level"), &c.zstd_level)) { std::cerr << "invalid zstd level\n"; return 2; }
    } else if (arg == "--max-cpu-percent") {
      if (!parse_int(require("--max-cpu-percent"), &c.max_cpu_percent)) { std::cerr << "invalid CPU limit\n"; return 2; }
    } else { std::cerr << "unknown option: " << arg << '\n'; return 2; }
  }

  if ((c.algorithm != RAMZ_LZ4 && c.algorithm != RAMZ_ZSTD) || c.min_compress_bytes == 0 ||
      c.zstd_level < 1 || c.zstd_level > 22 || c.max_cpu_percent < 1 || c.max_cpu_percent > 100) {
    std::cerr << "invalid configuration\n";
    return 2;
  }
  const std::string p = config_path();
  if (!make_parent_directory(p)) { std::cerr << "cannot create configuration directory\n"; return 1; }
  std::ofstream f(p, std::ios::trunc);
  if (!f) { std::cerr << "cannot write configuration\n"; return 1; }
  f << "algorithm=" << (c.algorithm == RAMZ_ZSTD ? "zstd" : "lz4") << '\n'
    << "block_bytes=" << c.min_compress_bytes << '\n'
    << "min_savings=" << c.min_savings_bytes << '\n'
    << "max_stored_bytes=" << c.max_stored_bytes << '\n'
    << "zstd_level=" << c.zstd_level << '\n'
    << "max_cpu_percent=" << c.max_cpu_percent << '\n';
  if (!f) { std::cerr << "cannot write configuration\n"; return 1; }
  print_config(c);
  return 0;
}
