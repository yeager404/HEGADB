# Quick Reference: Testing Bitmask-Based Filtering

## One-Line Test Commands

```bash
# Build
cd "/home/yeager404/Workspace/OpenFHE/Prototype 1/build" && cmake .. && make -j4

# Run all tests
cd bin && ./proto1_tests && ./prototype1_demo && ./test_bitmask_decrypt && ./test_row_filtering && ./test_bitmask_filter_exec

# Run individual tests
./proto1_tests              # 26 unit tests (40 seconds)
./prototype1_demo           # End-to-end demo (7 seconds)
./test_bitmask_decrypt      # Bitmask decryption (3 seconds)
./test_row_filtering        # Row filtering (3 seconds)
./test_bitmask_filter_exec  # BitmaskFilterExec operator (5 seconds)
```

## Test Descriptions

| Test | Purpose | Time | Status |
|------|---------|------|--------|
| `proto1_tests` | 26 unit tests covering all components | 40s | ✓ PASS |
| `prototype1_demo` | End-to-end query execution demo | 7s | ✓ PASS |
| `test_bitmask_decrypt` | Bitmask decryption correctness | 3s | ✓ PASS |
| `test_row_filtering` | Row filtering with bitmask | 3s | ✓ PASS |
| `test_bitmask_filter_exec` | BitmaskFilterExec operator | 5s | ✓ PASS |

## What Each Test Verifies

### proto1_tests (26 tests)
- ✓ Encryption/decryption
- ✓ Homomorphic operations
- ✓ Expression evaluation
- ✓ Storage persistence
- ✓ Query execution
- ✓ Plan validation

### prototype1_demo
- ✓ FHE setup
- ✓ Dataset encryption
- ✓ Bitmask generation
- ✓ Bitmask decryption
- ✓ Row filtering
- ✓ Result reconstruction
- ✓ Plaintext equivalence

### test_bitmask_decrypt
- ✓ Encrypted bitmask decryption
- ✓ Plaintext verification
- ✓ Format validation (0s and 1s)

### test_row_filtering
- ✓ Bitmask-based row selection
- ✓ Data integrity
- ✓ Correct filtering logic

### test_bitmask_filter_exec
- ✓ Homomorphic predicate evaluation
- ✓ Bitmask-only output (no columns)
- ✓ Operator correctness

## Expected Results

```
All 30 tests PASSED ✓

Unit Tests:                 26/26 PASSED
End-to-End Demo:            PASSED
Bitmask Decryption:         PASSED
Row Filtering:              PASSED
BitmaskFilterExec Operator: PASSED
```

## Performance Expectations

| Operation | Time |
|-----------|------|
| Key generation | 3-5 ms |
| Encryption (10 rows) | 100-150 ms |
| Query execution | 7000 ms |
| Bitmask decryption | <1 ms |
| Row filtering | <1 ms |
| Row reconstruction | <1 ms |

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Build fails | Ensure OpenFHE is installed: `cmake .. -DOpenFHE_DIR=/path/to/openfhe` |
| Tests timeout | Normal - homomorphic ops are slow. Wait 40+ seconds. |
| Bitmask incorrect | Check plaintext modulus (p=257) and input range [0,128] |
| Decryption fails | Verify private key is available and matches public key |

## Files to Test

- `examples/demo.cpp` - Main demo
- `examples/test_bitmask_decrypt.cpp` - Bitmask decryption
- `examples/test_row_filtering.cpp` - Row filtering
- `examples/test_bitmask_filter_exec.cpp` - Operator test
- `tests/test_*.cpp` - Unit tests

## Documentation

- `docs/bitmask-filtering.md` - Architecture and design
- `docs/TESTING.md` - Comprehensive testing guide
- `README.md` - Project overview

## Key Features Tested

✓ Bitmask generation (homomorphic WHERE clause)
✓ Bitmask decryption (lightweight, client-side)
✓ Row filtering (server-side, using decrypted bitmask)
✓ Row reconstruction (client-side, using bitmask)
✓ End-to-end correctness (plaintext equivalence)
✓ Performance metrics (bandwidth reduction ~97%)
✓ Security properties (server cannot decrypt)

## Next Steps

1. Run `./proto1_tests` to verify all unit tests pass
2. Run `./prototype1_demo` to see end-to-end flow
3. Run component tests to verify individual features
4. Review `docs/bitmask-filtering.md` for architecture
5. Review `docs/TESTING.md` for detailed testing guide
