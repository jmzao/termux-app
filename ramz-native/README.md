# RAMZ native package source

This is the opt-in userspace compressed-block library shipped by the Termux-ZRAM source fork.

For a host or Termux build:

```sh
make                    # production libramz.so: no corruption hook
make TEST_HOOKS=1 test  # integrity suite, including deliberate corruption
./build/ramz status
./build/ramz benchmark
```

The program using the library controls which byte blocks are stored. It does not intercept `malloc`, relocate arbitrary pointers, compress other processes, or implement kernel zram. See `../TERMUX_ZRAM.md` for the app/bootstrap boundary.
