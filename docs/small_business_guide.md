# Small Business Deployment Guide

## Scenario 1: VPN Provider (WireGuard NAT)

**Problem:** Each client connection needs a unique source port for NAT masquerade.
With 10,000+ concurrent sessions, the kernel's default port search becomes a bottleneck.

**Solution:** portd allocates a port in <60 ns vs. ~2 µs for conntrack linear scan.

```bash
# /etc/wireguard/scripts/peer-up.sh
#!/bin/bash
PEER_IP="$1"
PORT=$(portcli -p 1 alloc)
if [ -z "$PORT" ]; then echo "No ports available!"; exit 1; fi
echo "$PORT" > "/run/portd/peer-${PEER_IP}.port"
iptables -t nat -A POSTROUTING -s "$PEER_IP" -j SNAT --to-source "0.0.0.0:${PORT}"
echo "Assigned port $PORT to peer $PEER_IP"

# /etc/wireguard/scripts/peer-down.sh
#!/bin/bash
PEER_IP="$1"
PORT=$(cat "/run/portd/peer-${PEER_IP}.port" 2>/dev/null)
[ -z "$PORT" ] && exit 0
iptables -t nat -D POSTROUTING -s "$PEER_IP" -j SNAT --to-source "0.0.0.0:${PORT}"
portcli -p 1 free "$PORT"
rm -f "/run/portd/peer-${PEER_IP}.port"
```

Config `/etc/portd/config.json`:
```json
{
  "listen": "unix:///var/run/portd/portd.sock",
  "log_level": "warn",
  "use_syslog": true,
  "pools": [
    { "id": 1, "name": "vpn_nat", "range_start": 10000, "range_end": 60000 }
  ]
}
```

**Expected throughput:** 10,000 connections/sec, each allocation < 60 ns.

---

## Scenario 2: Game Server Hosting (Minecraft / CS2 / Valheim)

**Problem:** Hosting many game instances on one server. Each instance gets its own port.
Docker/bare-metal deployments fragment the port space over time.

**Setup:**
```bash
# Start a game server with portd-allocated port
start_game_server() {
    local game=$1
    local PORT=$(portcli -p 2 alloc)
    echo "Starting $game on port $PORT"
    docker run -d --name "${game}_${PORT}" \
        -p "${PORT}:25565/udp" \
        minecraft-server
    echo "$PORT" >> /var/run/game_ports.list
}

stop_game_server() {
    local container=$1
    local PORT=$(docker inspect --format='{{(index (index .NetworkSettings.Ports "25565/udp") 0).HostPort}}' "$container")
    docker stop "$container"
    portcli -p 2 free "$PORT"
}
```

Pool config for game servers:
```json
{
  "id": 2,
  "name": "game_servers",
  "range_start": 25000,
  "range_end": 30000,
  "default_lease_sec": 0
}
```

---

## Scenario 3: Reverse Proxy / CDN (HAProxy / nginx)

**Problem:** HAProxy with `source` addresses needs unique ephemeral ports per backend.
Default kernel port selection under heavy load causes `EADDRNOTAVAIL` errors.

**Integration (HAProxy Lua):**
```lua
-- /etc/haproxy/portd.lua
local socket = require("socket")

local function alloc_port()
    local s = socket.unix()
    s:connect("/var/run/portd/portd.sock")
    -- 8-byte request: magic=0xAB op=1(alloc) pool_id=3 flags=0 port=0 reserved=0
    s:send(string.pack("BBBH HH", 0xAB, 1, 3, 0, 0, 0))
    local resp = s:receive(8)
    s:close()
    local magic, op, err, port = string.unpack("BBhH", resp)
    return (err == 0) and port or nil
end

core.register_fetches("portd_alloc", function(txn)
    return alloc_port()
end)
```

---

## Scenario 4: IoT Gateway (ARM/MIPS)

**Problem:** Hundreds of MQTT/CoAP devices, each TCP session NATted through the gateway.
Embedded hardware has <128 MB RAM and a slow CPU.

**Why portd wins:**
- Entire vEB tree: **8 KB RAM** (fits in L1 cache on any ARM)
- No dynamic allocations in hot path
- Single-threaded mode available (remove pthread, compile with `-DSINGLE_THREAD`)
- Can be cross-compiled: `CC=arm-linux-gnueabihf-gcc cmake ..`

**Minimal footprint build:**
```bash
cmake -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DCMAKE_C_FLAGS="-Os -ffunction-sections -fdata-sections" \
      -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections" \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cross.cmake \
      ..
```

Resulting binary size: `portd` ~35 KB stripped.

**OpenWrt integration:**
```
# /etc/init.d/portd
START=50
STOP=10
start() { /usr/sbin/portd --daemon --config /etc/portd/config.json; }
stop()  { killall portd; }
```

---

## Scenario 5: Kubernetes / Docker on a Single Host

**Problem:** Containers in `hostNetwork` mode share the port space. Without coordination,
two containers may try to bind the same port simultaneously.

**Solution:** portd as a shared sidecar allocates ports exclusively.

**Kubernetes DaemonSet:**
```yaml
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: portd
  namespace: kube-system
spec:
  selector:
    matchLabels:
      app: portd
  template:
    metadata:
      labels:
        app: portd
    spec:
      hostNetwork: true
      hostPID: false
      containers:
      - name: portd
        image: yourorg/portd:1.0
        securityContext:
          runAsUser: 65534
          readOnlyRootFilesystem: true
        volumeMounts:
        - name: run-portd
          mountPath: /var/run/portd
        - name: config
          mountPath: /etc/portd
      volumes:
      - name: run-portd
        hostPath:
          path: /var/run/portd
          type: DirectoryOrCreate
      - name: config
        configMap:
          name: portd-config
```

Application pods then mount `/var/run/portd` and call `portcli` or `libportclient`.

---

## Performance Reference (x86_64, single core)

| Operation | Time | Rate |
|-----------|------|------|
| `pool_alloc` (lowest free port) | ~30–60 ns | ~20M ops/s |
| `pool_alloc_from` (specific port) | ~40–80 ns | ~15M ops/s |
| `pool_free` | ~25–50 ns | ~25M ops/s |
| Full 64K port scan (`successor` walk) | ~200 µs | — |
| Pool startup (`veb_create_full`) | ~500 µs | — |

Memory: **8 KB** per pool of 64K ports, regardless of how many are allocated.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `portcli: cannot connect` | portd not running | `sudo systemctl start portd` |
| `alloc failed: pool exhausted` | All ports in use | Increase pool range or add a pool |
| `free: already free` | Double-free in app logic | Check your alloc/free symmetry |
| High CPU in portd | Many clients reconnecting | Keep persistent connections; reuse `port_client_t` |
| Permission denied on socket | Wrong group | `sudo usermod -aG portd $USER`, re-login |

---

## Monitoring with Prometheus

```bash
#!/bin/bash
# /usr/local/bin/portd-exporter (run via cron or systemd timer)
STATUS=$(portcli -p 1 status 2>/dev/null)
if [ $? -ne 0 ]; then echo "portd_up 0"; exit; fi

echo "portd_up 1"
echo "portd_free $(echo $STATUS | jq .free)"
echo "portd_allocated $(echo $STATUS | jq .allocated)"
echo "portd_alloc_total $(echo $STATUS | jq .alloc_calls)"
echo "portd_fail_total $(echo $STATUS | jq .fail_calls)"
```

Add to your node_exporter textfile collector for automatic Grafana dashboards.
