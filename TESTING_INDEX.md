# Testing Resources Index

## Quick Links

- **Quick Start**: See [QUICK_TEST.md](QUICK_TEST.md)
- **Detailed Guide**: See [docs/TESTING.md](docs/TESTING.md)
- **Architecture**: See [docs/bitmask-filtering.md](docs/bitmask-filtering.md)

## Test Executables

Located in `build/bin/`:

1. **proto1_tests** (26 unit tests, ~40 seconds)
   - Comprehensive unit tests for all components
   - Run: `./proto1_tests`

2. **prototype1_demo** (end-to-end demo, ~7 seconds)
   - Complete query execution flow
   - Run: `./prototype1_demo`

3. **test_bitmask_decrypt** (bitmask decryption, ~3 seconds)
   - Verifies bitmask decryption correctness
   - Run: `./test_bitmask_decrypt`

4. **test_row_filtering** (row filtering, ~3 seconds)
   - Verifies row filtering with bitmask
   - Run: `./test_row_filtering`

5. **test_bitmask_filter_exec** (operator test, ~5 seconds)
   - Verifies BitmaskFilterExec operator
   - Run: `./test_bitmask_filter_exec`

## Build Instructions

```bash
cd "/home/yeager404/Workspace/OpenFHE/Prototype 1"
mkdir -p build && cd build
cmake ..
make -j4
cd bin
```

## Run All Tests

```bash
./proto1_tests
./prototype1_demo
./test_bitmask_decrypt
./test_row_filtering
./test_bitmask_filter_exec
```

## Test Results

**Total: 30/30 tests PASSED ✓**

- Unit Tests: 26/26 PASSED
- End-to-End Demo: PASSED
- Bitmask Decryption: PASSED
- Row Filtering: PASSED
- BitmaskFilterExec Operator: PASSED

## What Gets Tested

### Bitmask Generation
- Homomorphic predicate evaluation
- Correct 0/1 values
- Multiple predicates (AND, OR, GT, LT, EQ)

### Bitmask Decryption
- Lightweight decryption
- Plaintext verification
- Format validation

### Row Filtering
- Bitmask-based selection
- Data integrity
- Correctness

### Server-Side Filtering
- applyBitmaskFilter() method
- Encrypted row retrieval
- Batch handling

### Client-Side Reconstruction
- Row reconstruction from decrypted columns
- Bitmask-based filtering
- Result correctness

### End-to-End Flow
- Complete query execution
- Plaintext vs encrypted equivalence
- Performance metrics

## Performance Metrics

| Metric | Value |
|--------|-------|
| Query Execution | 7030 ms |
| Bitmask Size (encrypted) | ~9.6 KB |
| Bitmask Size (plaintext) | ~80 bytes |
| Bandwidth Reduction | ~97% |
| Client Decryption Time | <1 ms |

## Documentation Files

- `QUICK_TEST.md` - Quick reference guide
- `docs/TESTING.md` - Comprehensive testing guide
- `docs/bitmask-filtering.md` - Architecture and design
- `README.md` - Project overview
- `COMPLETION_REPORT.md` - Implementation status

## Source Files

### Implementation
- `include/proto1/operators.h` - BitmaskFilterExec class
- `src/execution/operators.cpp` - BitmaskFilterExec implementation
- `include/proto1/client.h` - decryptBitmask() method
- `src/client/client.cpp` - decryptBitmask() implementation
- `include/proto1/storage.h` - applyBitmaskFilter() interface
- `src/storage/filesystem_storage.cpp` - applyBitmaskFilter() implementation

### Tests
- `examples/demo.cpp` - End-to-end demo
- `examples/test_bitmask_decrypt.cpp` - Bitmask decryption test
- `examples/test_row_filtering.cpp` - Row filtering test
- `examples/test_bitmask_filter_exec.cpp` - BitmaskFilterExec test
- `tests/test_crypto.cpp` - Crypto unit tests
- `tests/test_expressions.cpp` - Expression unit tests
- `tests/test_execution.cpp` - Execution unit tests

## Troubleshooting

### Build Issues
- Ensure OpenFHE is installed
- Use: `cmake .. -DOpenFHE_DIR=/path/to/openfhe`

### Test Timeouts
- Normal - homomorphic operations are slow
- Wait 40+ seconds for full test suite

### Incorrect Results
- Check plaintext modulus (p=257)
- Verify input data range [0,128]
- Ensure private key is available

## Next Steps

1. Run `./proto1_tests` to verify all unit tests pass
2. Run `./prototype1_demo` to see end-to-end flow
3. Run component tests to verify individual features
4. Review documentation for architecture details
5. Modify tests for your own use cases

## Support

For detailed information, see:
- [docs/TESTING.md](docs/TESTING.md) - Comprehensive testing guide
- [docs/bitmask-filtering.md](docs/bitmask-filtering.md) - Architecture details
- [README.md](README.md) - Project overview
