# Prototype 1 — FHE Database Execution Engine

A working prototype of a single-node encrypted query execution engine using OpenFHE's BFV scheme. Demonstrates homomorphic evaluation of relational predicates (equality, comparison) and boolean logic over encrypted columnar data.

## What This Is

A **research prototype** that:
- Encrypts relational data using BFV (exact integer arithmetic)
- Executes WHERE clauses entirely on encrypted data
- Returns encrypted results that only the client can decrypt
- Maintains slot alignment across columns for row reconstruction
- Streams results chunk-by-chunk to avoid memory explosion

## What This Is NOT

- Not a production database (no transactions, replication, distributed execution)
- Not a full SQL engine (no parser, optimizer, or complex joins)
- Not a cryptographic library (uses OpenFHE for all FHE operations)
- Not a GPU implementation (CPU-only, but designed for GPU extension)

## Quick Start

### Prerequisites
- OpenFHE 1.5.1+ (installed and built)
- CMake 3.16+
- C++17 compiler
- GoogleTest (included as submodule)

### Build

```bash
cd "Prototype 1"
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Run Tests

```bash
./bin/proto1_tests
```

Expected output: **26/26 tests pass** (~51 seconds)

### Run Demo

```bash
./bin/prototype1_demo
```

Expected output:
```
╔══════════════════════════════════════════════════════╗
║  Prototype 1 — FHE Database Execution Engine Demo   ║
╚══════════════════════════════════════════════════════╝

Step 1: FHE Setup (BFV, p=257, depth=8, N=128)
  Slot count: 64
  Key generation: 7 ms

Step 2: Create 10-row dataset
  Query: SELECT id,age,salary WHERE id=5 AND age>3
  Plaintext reference result (1 rows):
    id=5 age=6 salary=4

Step 3: Encrypt and persist dataset
  Encryption + storage: 237 ms

Step 4: Build physical query plan
  ProjectExec([id, age, salary])
    └─ FilterExec(AND(EQ(id,5), GT(age,3)))
         └─ TableScanExec(employees)
  Plan validation: OK

Step 5: Execute encrypted query
  Query execution: 11383 ms

Step 6: Client decrypts and reconstructs rows
  Encrypted query result (1 rows):
  { salary=4, age=6, id=5 }

Step 7: Verify encrypted result matches plaintext reference
  ✓ PASS — encrypted result matches plaintext reference

Performance Summary:
  Key generation:         7 ms
  Encryption+store:     237 ms
  Query execution:    11383 ms
  Total:              11767 ms

Storage Metrics (id column, chunk 0):
  Plaintext size:    80 bytes
  Ciphertext size:   9615 bytes
  Expansion ratio:   120.2x
```

## Architecture

### High-Level Dataflow

```
Client                          Server
  │                               │
  ├─ Generate FHE keys            │
  ├─ Encrypt query literals       │
  └─ Send encrypted query ────────→ Receive encrypted query
                                   │
                                   ├─ Load encrypted chunks
                                   ├─ Evaluate WHERE clause
                                   │  (all on ciphertexts)
                                   ├─ Apply selection mask
                                   ├─ Project columns
                                   └─ Send encrypted results
  ← Receive encrypted results ─────┘
  │
  ├─ Decrypt results
  ├─ Apply decrypted mask
  └─ Reconstruct rows
```

### Component Overview

#### 1. Crypto Layer (`src/crypto/bfv_context.cpp`)
- **BFVKeyContext**: Wraps OpenFHE CryptoContext
- **BFVEncryptedVector**: Opaque ciphertext handle
- **Operations**: Add, Mult, Sub, Negate, AddPlain, MultPlain
- **Serialization**: Binary format for ciphertext persistence

**Why BFV?**
- Exact integer arithmetic (required for EQ, GT, LT)
- CKKS is approximate (unsuitable for relational predicates)

**Parameters:**
- Plaintext modulus: p=257 (prime, enables NTT)
- Multiplicative depth: 8 (sufficient for Fermat-based equality)
- Ring dimension: 128 (64 slots after NTT)
- Security: HEStd_NotSet (research prototype; production uses HEStd_128_classic)

#### 2. Storage Layer (`src/storage/filesystem_storage.cpp`)
- **EncryptedStorage**: Abstract interface
- **FilesystemStorage**: Disk-based implementation
- **Format**: Binary ciphertexts + JSON metadata
- **Layout**: `<root>/<tableId>/<columnId>/p<partitionId>/chunk_<chunkId>.{bin,meta.json}`

**Invariant:** Slot i in all columns of a chunk = same logical row

#### 3. Execution Layer (`src/execution/operators.cpp`)
- **TableScanExec**: Loads encrypted chunks from storage
  - Supports column projection pushdown
  - Streams chunk-by-chunk
- **FilterExec**: Evaluates WHERE predicates
  - Returns encrypted selection mask
  - No plaintext filtering on server
- **ProjectExec**: Selects output columns
  - Preserves selection mask

#### 4. Expression Evaluation (`src/expression/expression_executor.cpp`)
- **Expression Tree**: ColumnRef, EncryptedLiteral, Equal, GT, LT, And, Or, Not
- **Evaluation**: Homomorphic operations on ciphertexts

**Comparison Predicates:**

| Operation | Formula | Depth | Notes |
|-----------|---------|-------|-------|
| EQ(a,b) | 1 - (a-b)^(p-1) | 8 | Fermat's little theorem |
| GT(a,b) | sum_{k=1}^{(p-1)/2} EQ(a-b, k) | 8 | O(p) multiplications |
| LT(a,b) | GT(b,a) | 8 | Symmetric |

**Boolean Arithmetic:**
- AND(M1, M2) = M1 * M2
- OR(M1, M2) = M1 + M2 - M1*M2
- NOT(M) = 1 - M

#### 5. Data Ingestion (`src/catalog/ingestion.cpp`)
- CSV parsing
- Plaintext batch creation
- Encryption pipeline
- Chunked storage

#### 6. Client Component (`src/client/client.cpp`)
- **QueryClient**: Trusted client-side component
- Encrypts query literals
- Decrypts results
- Reconstructs rows from encrypted batches + decrypted mask

#### 7. Supporting Components
- **Catalog** (`src/catalog/catalog.h`): Schema registry
- **PlanValidator** (`src/planner/plan_validator.cpp`): Query plan validation
- **EncryptedBatch** (`src/execution/encrypted_batch.cpp`): Aligned column chunks
- **ResultStream** (`src/result/result_stream.cpp`): Streaming result iterator
- **Logger** (`src/logger.h`): Structured logging (never logs plaintext)

## Example Query

### Plaintext SQL
```sql
SELECT id, age, salary
FROM employees
WHERE id = 5 AND age > 3
```

### Physical Execution Plan
```
ProjectExec([id, age, salary])
  └─ FilterExec(AND(EQ(id,5), GT(age,3)))
       └─ TableScanExec(employees)
```

### Encrypted Execution
1. **TableScanExec**: Load encrypted id, age, salary columns
2. **FilterExec**: Evaluate predicate
   - M1 = EQ(id, Enc(5)) → [0,0,0,0,1,0,0,0,0,0]
   - M2 = GT(age, Enc(3)) → [0,0,0,1,1,1,1,0,0,0]
   - M = M1 * M2 → [0,0,0,0,1,0,0,0,0,0]
3. **ProjectExec**: Return encrypted id, age, salary + encrypted mask M
4. **Client**: Decrypt M and columns, reconstruct row where M[i]=1

### Result
```
id=5, age=6, salary=4
```

## Design Decisions

### 1. Columnar Storage
- **Why**: Enables slot alignment across columns
- **Benefit**: Rows reconstructible without decryption
- **Invariant**: Slot i in column A = slot i in column B = same row

### 2. Chunk-by-Chunk Execution
- **Why**: FHE ciphertexts are large (9.6 KB per 64 values)
- **Model**: Streaming iterator (next() → EncryptedBatch)
- **Benefit**: Constant memory regardless of table size

### 3. Fermat-Based Equality
- **Formula**: EQ(a,b) = 1 - (a-b)^(p-1)
- **Why**: Leverages Fermat's little theorem for exact comparison
- **Depth**: ceil(log2(p-1)) = 8 for p=257
- **Constraint**: Values must be in [0, (p-1)/2] for unambiguous GT/LT

### 4. Client-Side Decryption
- **Why**: Private key never leaves client
- **Security**: Server learns only ciphertext sizes and access patterns
- **Model**: Server executes on ciphertexts; client decrypts results

### 5. No Encrypted Indexes
- **Why**: Prototype focuses on correctness, not performance
- **Future**: Blind equality indexes, chunk-level min/max

## Performance

### Benchmark (10-row dataset)

| Operation | Time |
|-----------|------|
| Key generation | 7 ms |
| Encryption + storage | 237 ms |
| Query execution | 11,383 ms |
| **Total** | **11,767 ms** |

### Storage Expansion

| Metric | Value |
|--------|-------|
| Plaintext (10 rows × 8 bytes) | 80 bytes |
| Ciphertext (1 chunk) | 9,615 bytes |
| **Expansion ratio** | **120.2x** |

### Bottlenecks

1. **Comparison depth**: Fermat approach requires 8 multiplications per comparison
2. **No pipelining**: Each operation waits for previous to complete
3. **No GPU**: All operations on CPU

## Security Model

### What's Protected
- ✅ Plaintext column values (encrypted)
- ✅ Query results (encrypted until client decryption)
- ✅ Private key (client-side only)

### What's Leaked
- ❌ Query structure (visible to server)
- ❌ Ciphertext sizes (reveals row counts)
- ❌ Access patterns (which chunks are scanned)
- ❌ Comparison results (via selection mask size)

### Threat Model
- **Honest-but-curious server**: Executes queries correctly but tries to learn plaintext
- **Trusted client**: Generates keys, encrypts literals, decrypts results
- **No collusion**: Client and server don't collude

## Limitations

1. **Comparison depth**: Fermat approach requires depth=ceil(log2(p-1))
   - For p=65537: depth=16 (impractical)
   - For p=257: depth=8 (practical)
   - Future: Lagrange interpolation for larger p

2. **No string support**: Only INT32, INT64, BOOL

3. **Single-node only**: No distributed execution

4. **No query optimization**: Physical plans manually constructed

5. **No encrypted indexes**: Full table scans only

6. **No transactions**: Single-version data

7. **Research parameters**: HEStd_NotSet (production requires HEStd_128_classic)

## Future Work

### Prototype 2
- [ ] Larger plaintext moduli (p > 1000) via Lagrange interpolation
- [ ] SQL parser + optimizer (DataFusion integration)
- [ ] Distributed execution (multi-shard coordinator)
- [ ] Encrypted indexes (chunk-level min/max)
- [ ] GPU acceleration (OpenFHE GPU backend)

### Prototype 3+
- [ ] String support (bit-packed encrypted strings)
- [ ] Aggregation (SUM, COUNT, AVG over encrypted data)
- [ ] Encrypted joins (equi-joins on encrypted keys)
- [ ] Transactions (MVCC with encrypted versions)
- [ ] Replication (encrypted data replication)

## References

- **OpenFHE**: https://github.com/openfheorg/openfhe-development
- **BFV Scheme**: Brakerski, Fan, Vercauteren (2012)
- **Homomorphic Comparison**: Cheon, Kim, Kim, Song (2015)

## License

This prototype is provided as-is for research and educational purposes.

## Contact

For questions or issues, refer to the implementation status document: `docs/implementation-status.md`
