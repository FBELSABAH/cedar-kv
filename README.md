# Cedar KV

Cedar KV is a single-client TCP server written in C11. It records each write in an append-only journal before acknowledging it, then replays the journal on restart. This is an AI-assisted learning project. Codex produced the initial implementation and automated checks in November 2026.

## Run

Requires a POSIX system, a C compiler, Make, and Python 3 for the client, tests, and benchmark. Development took place on macOS. The [Ubuntu CI workflow](.github/workflows/test.yml) runs the integration suite with both the ordinary and sanitized builds.

```
make
./cedar-kv 7379 demo.log
```

In another terminal:

```
printf 'SET greeting hello world\nGET greeting\nSTATS\nDELETE greeting\nGET greeting\n' | nc -N 127.0.0.1 7379
```

Some netcat versions use `-q 1` instead of `-N`. For a portable client, use `python3 client.py 7379 "SET greeting hello world" "GET greeting"`.

Stop the server with Ctrl-C. Restart with the same journal to recover saved values. Use a new journal path for a fresh database.

## Protocol

ASCII commands end with LF. One line response per command. Values may contain spaces and may be empty. Keys are nonempty, space-free, at most 128 bytes. Commands are at most 4095 bytes before LF. Control bytes, including CR and NUL, are rejected.

| Command | Response |
|---|---|
| `SET key value` | `OK` after persistence succeeds |
| `GET key` | `VALUE value` or `NOT_FOUND` |
| `DELETE key` | `OK`, including absent keys |
| `STATS` | `KEYS n` |
| `PING` | `PONG` |

## Architecture

```
localhost TCP -> line framing/validation -> chained hash table
                                    |          ^
                                    v          |
                             append-only journal
                                    |
                               fflush + fsync
                                    |
                               OK response
startup -> journal replay -> rebuild hash table
```

The hash table uses FNV-1a hashing, 4093 buckets and separate chaining. SET allocates replacement storage before committing a record. The journal is flushed and fsynced before the table changes and the success response is sent. DELETE is logged too. An exclusive file lock prevents two cooperating server processes from using the same journal.

On restart, complete valid records are replayed. A short unterminated final record is truncated. Invalid complete records cause startup to fail without discarding the journal. A lost connection after a commit can leave a committed operation whose response was not received; the protocol has no exactly-once request IDs.

## Verification

```
make test
make sanitize
make benchmark
```

Integration tests cover reference-map agreement over 1000 writes, updates/deletion/empty values, forced process termination and restart, deliberately injected incomplete journal tails, complete invalid records, exclusive journal locking, TCP fragmentation/pipelining, oversized requests and embedded NUL input. The recovery tests simulate an incomplete tail; they do not exhaustively interrupt every disk-write boundary or simulate power failure.

AddressSanitizer and UndefinedBehaviorSanitizer are run against the same suite. Process-kill tests do not establish leak-free graceful shutdown. No sanitizer findings means none were observed in these exercised paths, not a proof of safety.

`benchmark-results.json` contains a measured single-machine run: 1000 sequential SET requests and 1000 GET requests over loopback. Timing includes Python client overhead and TCP round trips; every SET fsyncs. p50/p95 refer to client-observed request latency. This is not a Redis comparison, concurrent load test, or production capacity claim.

## Limits and tradeoffs

- One connected client is served at a time. An idle client blocks others; no timeouts, concurrency or authentication.
- Binds only to 127.0.0.1. Do not expose it publicly without a separate security design.
- At most 100000 distinct keys. Hash buckets do not resize; collision-heavy inputs degrade performance.
- Values live in memory. The journal grows without compaction. No TTL, cache eviction, replication, transactions or distributed consensus.
- fsync acknowledgment demonstrates an OS flush boundary. There is no claim of protection against all hardware/power-loss failures; macOS F_FULLFSYNC and directory-entry syncing are not implemented.
- No record checksum: syntactically valid corruption is not detected. Keep a test database rather than valuable data.
- C is used for the server; Python is used only for the client, tests and benchmark.

## Walk through a write and a restart

1. Build it and run SET/GET/DELETE yourself. Follow a request from `serve` to `mutate` and `slot`.
2. Explain pointer-to-pointer hash lookup, ownership of allocated strings, and collision chains.
3. Explain why journal persistence comes before `OK`, and why an unacknowledged write might still be recovered.
4. Run the tests and reproduce the incomplete-tail recovery test.
5. Personally implement one change (for example EXISTS), add its test, and explain a bug you encountered.
6. Re-run the sanitizers and benchmark. Record your own observations and contribution in this README.

Do not claim independent implementation, Linux experience, distributed systems expertise, or proficiency based only on receiving this generated project.
