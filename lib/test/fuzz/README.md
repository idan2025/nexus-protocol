# libnexus Fuzz Harnesses

Three libFuzzer-style targets for the wire-format parsers. ASan/UBSan under
libFuzzer catches OOB reads, sign overflow, and leaks on malformed input.

## Targets

| Binary         | Entry point                  | Exercises                                      |
|----------------|------------------------------|------------------------------------------------|
| `fuzz_packet`  | `nx_packet_deserialize`      | Compact 13-byte header + payload framing       |
| `fuzz_announce`| `nx_announce_parse`          | 130-byte announce payload + signature path     |
| `fuzz_message` | `nx_msg_parse` + accessors   | NXM TLV envelope, text/location/msgid fields   |

## Build modes

### libFuzzer (clang)

```sh
cd build
CC=clang CXX=clang++ cmake .. -DNX_FUZZ=ON
cmake --build . -j
./lib/test/fuzz_packet   -runs=100000 corpus_packet
./lib/test/fuzz_message  -runs=100000 corpus_message
./lib/test/fuzz_announce -runs=100000 corpus_announce
```

Seed each run by pointing at `lib/test/fuzz/corpus/<target>/`.

### Standalone corpus replay (GCC)

No clang? Build a plain `main()` that walks a corpus directory and feeds
each file to `LLVMFuzzerTestOneInput`. Used in CI for regression replay.

```sh
cd build
cmake .. -DNX_FUZZ_STANDALONE=ON
cmake --build . -j
ctest -R fuzz_ --output-on-failure
```

The standalone driver has no coverage feedback. It only proves that the
seed corpus (and any captured crashes) don't re-trip the parser.

## Adding seeds

Drop binary files into `lib/test/fuzz/corpus/<target>/`. They are picked up
by both modes. A shrunk reproducer from libFuzzer (`crash-*` file) is a
fine seed — commit it alongside the fix.
