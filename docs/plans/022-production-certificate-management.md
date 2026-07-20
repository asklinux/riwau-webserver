# Plan 022: Production Certificate Management

Date: 2026-07-20

Status: Guidance added. Production install layout is still not final.

## Scope

Dokumen ini menerangkan cara mengurus certificate/key TLS untuk Rimau dengan config SQLite sedia ada. Ia bukan automation ACME, bukan systemd unit, dan bukan install layout rasmi. Path di bawah ialah recommended production layout sehingga keputusan deployment rasmi dibuat. Needs verification.

Do not store private keys or certificate PEM content in SQLite. Rimau stores only file paths in `rimau_config`.

## Recommended Paths

Suggested layout:

```text
/usr/local/bin/rimau-server
/var/lib/rimau/rimau.sqlite3
/srv/rimau/public/
/etc/rimau/certs/default/fullchain.pem
/etc/rimau/certs/default/privkey.pem
/etc/rimau/certs/sni/api.example.test/fullchain.pem
/etc/rimau/certs/sni/api.example.test/privkey.pem
/etc/rimau/certs/sni/wildcard.tenant.test/fullchain.pem
/etc/rimau/certs/sni/wildcard.tenant.test/privkey.pem
```

If ACME tooling writes to another location, point SQLite config to that location or update atomically through a stable `current` symlink owned by the service operator.

## Ownership And Permissions

Recommended service account:

```text
user: rimau
group: rimau
```

Recommended permissions:

```text
/etc/rimau                         0750 root:rimau
/etc/rimau/certs                   0750 root:rimau
/etc/rimau/certs/*                 0750 root:rimau
fullchain.pem                      0644 root:rimau
privkey.pem                        0640 root:rimau
/var/lib/rimau                     0750 rimau:rimau
/var/lib/rimau/rimau.sqlite3       0640 rimau:rimau
/srv/rimau/public                  0755 rimau:rimau
```

Use `0600` for private keys if no renewal process needs group read. If an ACME client must write renewed keys, use a restricted renewal group and keep the service user read-only. Do not make private keys world-readable.

## SQLite TLS Configuration

Default certificate:

```bash
./build/rimau-server --database /var/lib/rimau/rimau.sqlite3 --set tls_enabled=true
./build/rimau-server --database /var/lib/rimau/rimau.sqlite3 --set tls_certificate_file=/etc/rimau/certs/default/fullchain.pem
./build/rimau-server --database /var/lib/rimau/rimau.sqlite3 --set tls_private_key_file=/etc/rimau/certs/default/privkey.pem
./build/rimau-server --database /var/lib/rimau/rimau.sqlite3 --set tls_min_version=TLSv1.2
./build/rimau-server --database /var/lib/rimau/rimau.sqlite3 --set tls_max_version=TLSv1.3
./build/rimau-server --database /var/lib/rimau/rimau.sqlite3 --set tls_alpn_protocols=http/1.1
```

SNI certificates:

```bash
./build/rimau-server --database /var/lib/rimau/rimau.sqlite3 --set "tls_sni_certificates=api.example.test=/etc/rimau/certs/sni/api.example.test/fullchain.pem|/etc/rimau/certs/sni/api.example.test/privkey.pem;*.tenant.test=/etc/rimau/certs/sni/wildcard.tenant.test/fullchain.pem|/etc/rimau/certs/sni/wildcard.tenant.test/privkey.pem"
```

Validate before serving:

```bash
./build/rimau-server --database /var/lib/rimau/rimau.sqlite3 --check-config
```

Rimau validates configured certificate/key files by loading them through bundled OpenSSL when TLS is enabled. It does not currently perform ACME issuance or renewal.

## Reload

SIGHUP reloads TLS certificate, key, SNI map, TLS version range, ciphers, and ALPN settings for new connections when the updated SQLite config is accepted.

Safe reload flow:

```bash
./build/rimau-server --database /var/lib/rimau/rimau.sqlite3 --check-config
kill -HUP $(pidof rimau-server)
```

Existing TLS connections keep using the context they already negotiated. New TLS connections use the reloaded context. Changes to `tls_enabled`, listener host/port, worker count, or listener-level settings still require restart.

If the deployment uses a supervisor other than a direct process, send SIGHUP through that supervisor. Needs verification for the final service manager.

## Rotation

Recommended rotation flow:

1. Write renewed certificate and key to a new versioned directory or temporary files in the same filesystem.
2. Set restrictive permissions before exposing the files to Rimau.
3. Verify the certificate and key pair with the bundled OpenSSL binary:

```bash
build/_deps/openssl/install/bin/openssl x509 -in /etc/rimau/certs/default/fullchain.pem -noout -subject -issuer -dates
build/_deps/openssl/install/bin/openssl x509 -in /etc/rimau/certs/default/fullchain.pem -pubkey -noout | build/_deps/openssl/install/bin/openssl sha256
build/_deps/openssl/install/bin/openssl pkey -in /etc/rimau/certs/default/privkey.pem -pubout | build/_deps/openssl/install/bin/openssl sha256
```

4. Update SQLite paths or switch an atomic `current` symlink.
5. Run `--check-config`.
6. Send SIGHUP.
7. Smoke test the served certificate:

```bash
build/_deps/openssl/install/bin/openssl s_client -connect 127.0.0.1:443 -servername api.example.test -brief </dev/null
```

8. Watch logs for TLS reload or handshake errors.

## Rollback

Keep the previous certificate/key version until the new version is verified under live traffic.

Rollback flow:

1. Restore SQLite `tls_certificate_file`, `tls_private_key_file`, or `tls_sni_certificates` paths to the previous known-good files, or move the stable `current` symlink back.
2. Run `--check-config`.
3. Send SIGHUP.
4. Repeat the `openssl s_client` smoke test.
5. Restart only if the failed change involved `tls_enabled`, listener settings, worker settings, or another non-reloadable key.

Do not delete the previous private key until rollback is no longer needed under the operator retention policy.

## OCSP Stapling

OCSP stapling is not supported in Rimau as of this plan. ADR-0039 records the decision to defer it instead of adding incomplete config keys.

If OCSP stapling is mandatory for a deployment, use a TLS terminator that supports it or keep certificates short-lived and monitored until Rimau accepts a future OCSP implementation plan. Needs verification.
