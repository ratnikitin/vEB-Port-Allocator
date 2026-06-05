# vEB Port Allocator

**Ultra-fast TCP/UDP port pool manager** built on a van Emde Boas tree.
Allocates and releases network ports in **< 60 ns** — orders of magnitude faster
than the kernel's default ephemeral port search.

```
portcli alloc        # → 1024  (in ~40 ns)
portcli free 1024    # → ok    (in ~30 ns)
portcli status       # → JSON pool stats
```

---

## Why

Standard Linux conntrack and HAProxy/nginx port selection use linear or
tree-based searches that degrade as the port space fragments. At 10,000+
concurrent connections, this becomes the bottleneck.

The **van Emde Boas tree** over U = 65,536 gives true O(log log U) = **O(1)**
operations via two levels of 256-bit bitmaps, exploiting `BSF`/`TZCNT`
hardware instructions for branch-free minimum extraction.

| Structure | alloc | free | memory/pool |
|-----------|-------|------|-------------|
| Linear scan | O(U) | O(1) | O(1) |
| Red-Black tree | O(log n) | O(log n) | O(n) |
| **vEB (this project)** | **O(1)** | **O(1)** | **8 KB** |

---

## Features

- **Daemon (`portd`)** — Unix socket / TCP server, manages multiple named pools
- **CLI (`portcli`)** — shell scripts, debugging, health checks
- **Library (`libportclient`)** — embed in any C application
- **Thread-safe pools** — POSIX mutex + atomic stats counters
- **Multiple pools** — separate ranges per service (VPN, proxy, game servers…)
- **Configurable** — JSON config file with hot-reload support
- **Secure** — runs as unprivileged user, systemd hardening, socket permissions
- **Tiny** — portd binary ~50 KB, pool state ~8 KB, no external dependencies
- **Portable** — Linux (primary), FreeBSD (adaptable), cross-compile for ARM/MIPS

---

## Quick Start

```bash
# Build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
ctest --output-on-failure     # run tests

# Start daemon (uses built-in defaults if no config exists)
sudo ./portd

# CLI (in another terminal)
./portcli alloc          # allocate a port
./portcli allocfrom 8080 # allocate >= 8080
./portcli free 1024      # release a port
./portcli status         # JSON stats
./portcli ping           # health check
```

---

## Install

```bash
sudo ./scripts/install.sh
# Installs portd + portcli to /usr/local/sbin,
# libportclient to /usr/local/lib,
# config to /etc/portd/config.json,
# systemd unit (auto-enabled).

sudo systemctl start portd
portcli ping   # → pong
```

---

## Integration (C)

```c
#include <client.h>

port_client_t *pc = port_client_connect("unix:///var/run/portd/portd.sock");

uint16_t port = port_client_alloc(pc, 1 /* pool_id */);
// ... use port ...
port_client_free(pc, 1, port);

port_client_disconnect(pc);
```

Compile: `gcc myapp.c -lportclient -o myapp`

---

## Configuration

```json
{
  "listen":    "unix:///var/run/portd/portd.sock",
  "log_level": "info",
  "use_syslog": true,
  "pools": [
    { "id": 1, "name": "nat_pool",    "range_start": 1024,  "range_end": 65535 },
    { "id": 2, "name": "vpn_pool",    "range_start": 32768, "range_end": 61000 },
    { "id": 3, "name": "proxy_pool",  "range_start": 10000, "range_end": 20000 }
  ]
}
```

---

## Benchmarks (x86_64, single core, -O2)

```
vEB minimum + delete (raw)        min=18  med=22  p99=41   ns/op
pool_alloc + pool_free / 2        min=28  med=35  p99=68   ns/op
pool_alloc_from (random desired)  min=30  med=38  p99=75   ns/op
Throughput (alloc+free pairs)     ~15 M ops/s
Full 64K port scan                ~200 µs total
```

---

## Project Structure

```
veb-port-allocator/
├── include/         # Public headers (veb.h, port_manager.h, client.h)
├── src/
│   ├── veb/         # van Emde Boas tree core (veb.c, veb_internal.h)
│   ├── manager/     # Daemon: pool.c, server.c, main.c, protocol.h
│   ├── client/      # Client library: client.c
│   ├── cli/         # CLI tool: portcli.c
│   └── utils/       # Logging, config parser
├── tests/           # Unit tests + benchmark
├── scripts/         # install.sh, portd.service, sample_config.json
├── docs/            # architecture.md, usage.md, small_business_guide.md
└── CMakeLists.txt
```

---

## Use Cases

- **VPN providers** — per-client NAT port allocation at line rate
- **Game server hosting** — eliminate port conflicts across instances
- **Reverse proxies** — deterministic source port selection under load
- **IoT gateways** — 8 KB footprint, runs on OpenWrt/Buildroot
- **Container platforms** — shared port coordinator for `hostNetwork` pods

See [docs/small_business_guide.md](docs/small_business_guide.md) for deployment recipes.

---

## License

MIT — see [LICENSE](LICENSE).

---

## Internals

The tree stores **free ports** as present elements. Allocation = `veb_minimum()` + `veb_delete()`. Release = `veb_insert()`. No heap allocations after startup. The entire vEB tree is a single 8 KB stack-allocated struct.

See [docs/architecture.md](docs/architecture.md) for the full design.
