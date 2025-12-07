# EmbraceDB

## Build
- Configure: `cmake -S . -B build`
- Build: `cmake --build build`
- Run app: `./build/embrace`

## Testing
- All tests: `cd build && ctest --output-on-failure`
- Verbose: `cd build && ctest -V`
- By suite: `cd build && ctest -R BtreeTest` / `ctest -R WalTest`
- Single test: `cd build && ./tests/embrace_tests --gtest_filter=BtreeTest.NodeSplitting`
- Tests auto-discover files matching `tests/test_*.cpp`.

## CI
GitHub Actions workflow at `.github/workflows/test.yml` builds and runs the suite on push/PR.

## Notes
- B+Tree coverage: CRUD, structure (split/merge/rebalance), edge cases, WAL recovery, stress (reverse, interleaved, random), corruption limits.
- Status and WAL helpers covered.
- For coverage reports or leak checks, enable tooling (e.g., lcov, valgrind) in your environment.
