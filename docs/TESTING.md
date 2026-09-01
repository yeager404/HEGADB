# Testing Guide: Bitmask-Based Filtering

## Quick Start

### 1. Build the Project
```bash
cd "/home/yeager404/Workspace/OpenFHE/Prototype 1"
mkdir -p build && cd build
cmake ..
make -j4
```

### 2. Run All Tests
```bash
cd bin
./proto1_tests          # 26 unit tests
./prototype1_demo       # End-to-end demo
./test_bitmask_decrypt  # Bitmask decryption test
./test_row_filtering    # Row filtering test
./test_bitmask_filter_exec  # BitmaskFilterExec operator test
```

---

## Test Suite Overview

### A. Unit Tests (26 tests, ~40 seconds)

```bash
./proto1_tests
```

**Coverage:**
- Crypto operations (encryption, decryption, homomorphic ops)
- Expression evaluation (equality, GT, LT, boolean ops)
- Storage and persistence
- Execution operators
- Plan validation

**Expected Output:**
```
[==========] 26 tests from 4 test suites ran. (40679 ms total)
[  PASSED  ] 26 tests.
```

---

### B. End-to-End Demo (~7 seconds)

```bash
./prototype1_demo
```

**What it tests:**
1. FHE setup (key generation)
2. Dataset creation and encryption
3. Query plan building
4. Bitmask-based query execution
5. Client-side bitmask decryption
6. Server-side row filtering
7. Client-side row reconstruction
8. Result verification

**Expected Output:**
```
Step 5: Execute encrypted query (bitmask-only)
  Query execution (bitmask): 7030 ms

Step 6: Client decrypts bitmask
  Chunk 0: 1/10 rows match

Step 7: Server applies bitmask and returns encrypted rows
  Returned 1 filtered batch(es)

Step 8: Client decrypts and reconstructs rows
  Encrypted query result (1 rows):
  { id=5, age=6, salary=4 }

Step 9: Verify encrypted result matches plaintext reference
  ✓ PASS — encrypted result matches plaintext reference
```

---

### C. Component Tests

#### Test 1: Bitmask Decryption (~3 seconds)

```bash
./test_bitmask_decrypt
```

**What it tests:**
- `QueryClient::decryptBitmask()` method
- Correct decryption of selection mask
- Bitmask format (0s and 1s)

**Expected Output:**
```
=== Test: Bitmask Decryption ===

Original mask:   1 0 1 0 1 0 0 0 0 0 
Decrypted mask:  1 0 1 0 1 0 0 0 0 0 

✓ PASS: Bitmask decryption correct
```

**What it verifies:**
- Encrypted bitmask can be decrypted correctly
- Decrypted values match original plaintext
- Bitmask size is correct

---

#### Test 2: Row Filtering (~3 seconds)

```bash
./test_row_filtering
```

**What it tests:**
- Row filtering using decrypted bitmask
- Correct row selection based on mask values
- Data integrity during filtering

**Expected Output:**
```
=== Test: Row Filtering with Bitmask ===

Original rows:  10 20 30 40 50 
Bitmask:        1 0 1 0 1 
Filtered rows:  10 30 50 

✓ PASS: Row filtering correct
```

**What it verifies:**
- Rows with bitmask[i] == 1 are selected
- Rows with bitmask[i] == 0 are filtered out
- Filtered data is correct

---

#### Test 3: BitmaskFilterExec Operator (~5 seconds)

```bash
./test_bitmask_filter_exec
```

**What it tests:**
- `BitmaskFilterExec` physical operator
- Homomorphic predicate evaluation
- Bitmask-only output (no columns)

**Expected Output:**
```
=== Test: BitmaskFilterExec Operator ===

Batches returned: 1
Batch logicalRowCount: 5
Columns in batch: 0
Has selectionMask: yes
Bitmask: 0 0 1 1 1 

✓ PASS: BitmaskFilterExec returns only bitmask
```

**What it verifies:**
- BitmaskFilterExec returns only encrypted bitmask
- No data columns are included
- Bitmask is correctly computed (id > 2 → [0,0,1,1,1])
- logicalRowCount is preserved

---

## Test Scenarios

### Scenario 1: Simple Equality Filter

**Query:** `SELECT * WHERE id = 5`

**Expected Bitmask:** `[0, 0, 0, 0, 1, 0, 0, 0, 0, 0]`

**Test:** Run `./prototype1_demo`

---

### Scenario 2: Greater-Than Filter

**Query:** `SELECT * WHERE age > 3`

**Expected Bitmask:** `[0, 0, 1, 1, 1, 1, 1, 0, 0, 0]`

**Test:** Modify `test_bitmask_filter_exec.cpp` to use GT predicate

---

### Scenario 3: Complex AND Filter

**Query:** `SELECT * WHERE id = 5 AND age > 3`

**Expected Bitmask:** `[0, 0, 0, 0, 1, 0, 0, 0, 0, 0]`

**Test:** Run `./prototype1_demo` (this is the default)

---

### Scenario 4: Multiple Chunks

**Query:** `SELECT * WHERE value > 50` on 100-row dataset

**Test:** Modify `test_bitmask_filter_exec.cpp` to use larger dataset

---

## Performance Testing

### Measure Query Execution Time

```bash
./prototype1_demo 2>&1 | grep "Query execution"
```

**Expected:** ~7000 ms for 10-row dataset

### Measure Bitmask Decryption Time

```bash
./test_bitmask_decrypt 2>&1 | grep "PASS"
```

**Expected:** <1 ms

### Measure Row Filtering Time

```bash
./test_row_filtering 2>&1 | grep "PASS"
```

**Expected:** <1 ms

---

## Correctness Verification

### 1. Plaintext vs Encrypted Equivalence

**Test:** `./proto1_tests` → `StorageTest.PlaintextVsEncryptedEquivalence`

**Verifies:**
- Encrypted query results match plaintext results
- No data corruption during encryption/decryption

---

### 2. Bitmask Accuracy

**Test:** `./test_bitmask_filter_exec`

**Verifies:**
- Bitmask correctly identifies matching rows
- No false positives or false negatives

---

### 3. Row Reconstruction

**Test:** `./prototype1_demo` → Step 8-9

**Verifies:**
- Rows are correctly reconstructed from decrypted columns
- Bitmask filtering produces correct results

---

## Debugging

### Enable Debug Logging

All tests output debug logs. To see them:

```bash
./prototype1_demo 2>&1 | grep DEBUG
```

### Check Specific Component

**Bitmask generation:**
```bash
./prototype1_demo 2>&1 | grep "Step 5"
```

**Bitmask decryption:**
```bash
./prototype1_demo 2>&1 | grep "Step 6"
```

**Row filtering:**
```bash
./prototype1_demo 2>&1 | grep "Step 7"
```

**Row reconstruction:**
```bash
./prototype1_demo 2>&1 | grep "Step 8"
```

---

## Test Results Summary

### All Tests Passing ✓

```
Unit Tests:                 26/26 PASSED
End-to-End Demo:            PASSED
Bitmask Decryption:         PASSED
Row Filtering:              PASSED
BitmaskFilterExec Operator: PASSED

Total: 30/30 tests PASSED
```

---

## Common Issues and Solutions

### Issue: Tests fail with "Key not found"

**Solution:** Ensure OpenFHE is properly installed and CMake can find it.

```bash
cd build
cmake .. -DOpenFHE_DIR=/path/to/openfhe/install
```

### Issue: Tests timeout

**Solution:** Homomorphic operations are slow. This is expected. Typical times:
- Key generation: 3-5 ms
- Encryption: 100-150 ms
- Query execution: 5000-7000 ms
- Decryption: <1 ms

### Issue: Bitmask values are incorrect

**Solution:** Check that:
1. Plaintext modulus (p=257) is correct
2. Predicate is correctly defined
3. Input data is in valid range [0, 128]

---

## Next Steps

1. **Performance Optimization:** Profile and optimize hot paths
2. **Larger Datasets:** Test with 1000+ rows
3. **Complex Queries:** Test with multiple predicates
4. **Batch Processing:** Test with multiple chunks
5. **Security Analysis:** Verify information leakage is acceptable

---

## Files

- `examples/demo.cpp` - End-to-end demo
- `examples/test_bitmask_decrypt.cpp` - Bitmask decryption test
- `examples/test_row_filtering.cpp` - Row filtering test
- `examples/test_bitmask_filter_exec.cpp` - BitmaskFilterExec test
- `tests/test_crypto.cpp` - Crypto unit tests
- `tests/test_expressions.cpp` - Expression unit tests
- `tests/test_execution.cpp` - Execution unit tests
