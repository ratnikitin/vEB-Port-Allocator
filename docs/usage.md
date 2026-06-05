# Usage Guide

## Quick Start

```bash
# 1. Build
git clone https://github.com/ratnikitin/vEB-Port-Allocator.git
cd vEB-Port-Allocator && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# 2. Run tests
ctest --output-on-failure

# 3. Start daemon (no config needed — uses built-in defaults)
sudo ./portd

# 4. Use the CLI
./portcli alloc          # returns first free port (1024)
./portcli allocfrom 8080 # returns 8080 or next available
./portcli free 1024      # returns port to pool
./portcli status         # prints JSON stats
./portcli ping           # checks daemon is alive
```

---

## Configuration

Copy `scripts/sample_config.json` to `/etc/portd/config.json`:

```json
{
  "listen": "unix:///var/run/portd/portd.sock",
  "log_level": "info",
  "use_syslog": true,
  "pools": [
    {
      "id": 1,
      "name": "nat_pool",
      "range_start": 1024,
      "range_end": 65535,
      "default_lease_sec": 0
    }
  ]
}
```

Multiple pools can coexist with different IDs and ranges.

---

## Embedding in C Applications

```c
#include <client.h>

int main(void) {
    /* Connect to daemon */
    port_client_t *pc = port_client_connect("unix:///var/run/portd/portd.sock");
    if (!pc) { perror("connect"); return 1; }

    /* Allocate a port from pool 1 */
    uint16_t port = port_client_alloc(pc, 1);
    if (port == 0) {
        fprintf(stderr, "alloc failed: %s\n",
            port_client_strerror(port_client_last_error(pc)));
        port_client_disconnect(pc);
        return 1;
    }

    printf("Got port: %u\n", port);

    /* ... use port for NAT / listen / connect ... */

    /* Return port to pool */
    port_client_free(pc, 1, port);
    port_client_disconnect(pc);
    return 0;
}
```

Compile: `gcc myapp.c -lportclient -o myapp`

---

## VPN Integration (WireGuard / OpenVPN)

Add to your WireGuard interface config:

```ini
# /etc/wireguard/wg0.conf
[Interface]
PostUp   = PORT=$(portcli -p 1 alloc) && iptables -t nat -A POSTROUTING -o eth0 -p udp --dport $PORT -j MASQUERADE; echo $PORT > /run/wg0.port
PostDown = iptables -t nat -D POSTROUTING -o eth0 -p udp --dport $(cat /run/wg0.port) -j MASQUERADE; portcli free $(cat /run/wg0.port)
```

---

## HAProxy Integration

In HAProxy's `source` directive, use a pre-allocated port:

```bash
# In a shell wrapper around haproxy:
export HAPROXY_PORT=$(portcli -p 2 alloc)
haproxy -f haproxy.cfg
portcli -p 2 free $HAPROXY_PORT
```

Or patch HAProxy's source port selection to call `libportclient` directly.

---

## Docker / Kubernetes

Run portd as a sidecar:

```yaml
# docker-compose.yml
services:
  portd:
    image: yourorg/portd:1.0
    volumes:
      - portd-socket:/var/run/portd
  myapp:
    image: yourapp
    volumes:
      - portd-socket:/var/run/portd
    environment:
      PORTD_SOCKET: unix:///var/run/portd/portd.sock

volumes:
  portd-socket:
```

---

## Monitoring

`portcli status` returns JSON you can feed to Prometheus via a small exporter:

```bash
portcli -p 1 status | jq '{
  "portd_free":      .free,
  "portd_allocated": .allocated,
  "portd_alloc_total": .alloc_calls,
  "portd_fail_total":  .fail_calls
}'
```
