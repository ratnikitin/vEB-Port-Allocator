# Architecture — vEB Port Allocator

## Overview

The project provides three deliverables:

| Target | Type | Purpose |
|--------|------|---------|
| `portd` | Daemon | Unix/TCP socket server, manages port pools |
| `portcli` | CLI tool | Interactive testing and scripting |
| `libportclient` | Library | Embed port allocation into any C/C++ application |

---

## Data Structure: van Emde Boas Tree (2-level, U=65536)

### Key insight

A port number is a `uint16_t` in `[0, 65535]`. We decompose each key:

```
key = cluster_index × 256 + position
    = (key >> 8)           +  (key & 0xFF)
```

This gives a 2-level tree:

```
veb_t
├── summary: bits256_t          // bit j set ↔ cluster[j] non-empty
└── clusters[256]: veb_cluster_t
        └── bits: bits256_t     // bit i set ↔ key (j<<8)|i in set
```

**Total size: 256 clusters × 32 bytes + 32 bytes summary = ~8 KB**

### Bit operations

Each `bits256_t` is 4 × `uint64_t`. All key operations reduce to:

```c
// Find lowest set bit in 256-bit word
uint8_t bits256_ctz(const bits256_t *b) {
    if (b->w[0]) return __builtin_ctzll(b->w[0]);
    if (b->w[1]) return 64 + __builtin_ctzll(b->w[1]);
    if (b->w[2]) return 128 + __builtin_ctzll(b->w[2]);
    return 192 + __builtin_ctzll(b->w[3]);
}
```

`__builtin_ctzll` maps to a single `BSF`/`TZCNT` instruction on x86. The entire minimum-extraction path is ~5–8 instructions.

### Complexity

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| insert | O(1) | Two bit-set ops + summary update |
| delete | O(1) | Two bit-clear ops + summary update |
| minimum | O(1) | `ctz(summary)` + `ctz(cluster)` |
| successor | O(1) | Scan cluster tail, or next cluster |
| predecessor | O(1) | Scan cluster head, or prev cluster |

Theoretical O(log log U) = O(4), but in practice constant-factor: **< 20 ns** for alloc on modern hardware.

---

## Pool Manager

```
port_pool_t
├── veb_t *veb           // free-port set (present = available)
├── pthread_mutex_t lock // guards veb mutations
├── atomic_uint64 alloc_calls / free_calls / fail_calls
├── uint16_t range_start, range_end
└── char name[64]
```

**Convention:** the vEB set holds **free** ports. Allocating = `veb_minimum` + `veb_delete`. Freeing = `veb_insert`.

---

## Daemon Protocol

Fixed 8-byte request / 8-byte response over a persistent connection.

```
Request:  [magic:1][op:1][pool_id:1][flags:1][port:2][reserved:2]
Response: [magic:1][op:1][error:2][port:2][reserved:2]
```

`OP_STATUS` appends: `[json_len:2][json:json_len]` after the base response.

The server uses `epoll` with edge-triggered notifications. Each connected client keeps one persistent fd; requests are processed inline (no thread-per-client overhead).

---

## Security

- Socket permissions `0660` (owner + group only).
- systemd service runs as dedicated `portd` user with `NoNewPrivileges`, `ProtectSystem=strict`, and syscall filtering.
- All pool-id and port-range values are bounds-checked before dispatch.
- Magic bytes validated on every request to detect corrupt/fuzz data.
- No heap allocations in the critical path after startup.

---

## Memory Layout

```
Process memory after startup (~1 pool):
  portd binary:         ~50 KB (stripped)
  veb_t (per pool):       8 KB
  port_pool_t:          <1 KB
  epoll internals:      ~1 KB
  Total:              ~60 KB
```

Suitable for embedded systems (IoT gateways, minimal VMs).
