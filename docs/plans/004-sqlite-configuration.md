# Plan 004: SQLite Runtime Configuration

Date: 2026-07-18

## Goal

Move Rimau Web Server runtime configuration from key-value config files into SQLite.

## Implemented

- Removed runtime use of `--config`.
- Added `--database path`.
- Added `--set key=value`.
- Added SQLite bootstrap for `rimau_config`.
- Added default database path `data/rimau.sqlite3`.
- Added CMake SQLite dependency. This phase initially used the system CMake SQLite package; Plan 015 later replaced that with bundled static SQLite.
- Updated `make serve` and `./scripts/serve-dev.sh` to write dev settings into SQLite before starting the server.
- Added SQLite config unit test.
- Added protocol gate keys `http1_enabled`, `http2_enabled`, and `http3_enabled`.
- Added HTTP keep-alive keys `http_keep_alive_enabled`, `http_keep_alive_timeout_seconds`, and `http_keep_alive_max_requests`.
- Added limited SIGHUP live reload for restart-free dynamic config values.

## SQLite Table

```sql
CREATE TABLE rimau_config (
  key TEXT PRIMARY KEY NOT NULL,
  value TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

## Supported Keys

- `host`
- `port`
- `document_root`
- `max_request_bytes`
- `http_keep_alive_enabled`
- `http_keep_alive_timeout_seconds`
- `http_keep_alive_max_requests`
- `listen_backlog`
- `server_name`
- `worker_threads`
- `epoll_max_events`
- `reuse_port_enabled`
- `tcp_keepalive_enabled`
- `tcp_keepalive_idle_seconds`
- `tcp_keepalive_interval_seconds`
- `tcp_keepalive_probe_count`
- `graceful_shutdown_timeout_seconds`
- `connection_pool_size`
- `http1_enabled`
- `http2_enabled`
- `http3_enabled`
- `tls_enabled`
- `tls_certificate_file`
- `tls_private_key_file`

## Commands

```bash
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --database data/rimau.sqlite3 --set port=18080
./build/rimau-server --database data/rimau.sqlite3 --set http2_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --protocols
./build/rimau-server --database data/rimau.sqlite3
```

Dev server:

```bash
make serve
```

## Not Implemented Yet

- Schema migration/version table.
- SIGHUP reload for listener, worker, pool, and TLS changes; those currently require restart.
- Admin/control API for config.
- Production backup and permission policy.
- Config audit history.
