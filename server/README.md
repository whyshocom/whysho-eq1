# Whysho EQ1 — License Activation Server

License activation + signing service for the **Whysho EQ1** audio plugin
(VST3/AU equalizer). It issues **Ed25519-signed license tokens** that the plugin
verifies **offline** against an embedded public key, and provides online
activation / seat management / revocation.

- Runtime: **Node.js 22**, **TypeScript** (ESM), **Fastify 5**
- Storage: **better-sqlite3** (single file, WAL)
- Crypto: **@noble/ed25519** + **@noble/hashes** (pure JS, no native build)
- Validation: **zod**; rate limiting: **@fastify/rate-limit**

---

## Quick start

```bash
cd server
npm install

# 1) Generate a signing keypair (writes keys/ artifacts with --write)
npm run keygen -- --write

# 2) Configure env (copy the example and fill in real values)
cp .env.example .env
#   set WHYSHO_LICENSE_PRIVATE_KEY  (the hex private key keygen printed)
#   set WHYSHO_ADMIN_TOKEN          (any long random secret)

# 3) Run
npm run dev        # tsx watch (development)
# or
npm run build && npm start   # compiled (production)
```

Health check: `curl http://127.0.0.1:8787/api/health`

For local development you may reuse the disposable dev key in
`keys/DEV_PRIVATE_KEY.txt` (see **Keys** below). **Rotate before production.**

---

## Environment variables

| Variable | Required | Default | Purpose |
| --- | --- | --- | --- |
| `WHYSHO_LICENSE_PRIVATE_KEY` | **yes** | — | Ed25519 private key (hex, 64 chars) used to sign tokens. Keep secret. |
| `WHYSHO_ADMIN_TOKEN` | for admin | — | Shared secret for `/api/admin/*` (`x-admin-token` header). |
| `HOST` | no | `0.0.0.0` | Bind host. |
| `PORT` | no | `8787` | Bind port. |
| `WHYSHO_DB_PATH` | no | `./data/licenses.db` | SQLite file path. |
| `WHYSHO_DEFAULT_SEATS` | no | `2` | Default seat count for new licenses. |
| `WHYSHO_RATE_LIMIT_MAX` | no | `60` | Max requests per window on activate/validate. |
| `WHYSHO_RATE_LIMIT_WINDOW_MS` | no | `60000` | Rate-limit window (ms). |
| `LOG_LEVEL` | no | `info` | Fastify log level. |

`.env` is loaded automatically if present (Node 22 `process.loadEnvFile`).

---

## npm scripts

| Script | What it does |
| --- | --- |
| `npm run dev` | `tsx watch src/index.ts` — hot-reloading dev server. |
| `npm run build` | `tsc` → `dist/`. |
| `npm start` | `node dist/index.js` (run `build` first). |
| `npm run keygen [-- --write]` | Generate an Ed25519 keypair; `--write` saves the `keys/` artifacts. |
| `npm run issue-test-license` | Create (and activate) a license for manual testing. |
| `npm run typecheck` | Type-check `src/` **and** `scripts/` with no emit. |

---

## Token format spec (MUST match the C++ verifier byte-for-byte)

A **license** is this exact JSON object:

```jsonc
{
  "product":      "whysho-eq1",
  "licenseId":    "<uuid>",
  "email":        "<customer email>",
  "edition":      "standard" | "pro",
  "machineId":    "<hex sha256 of the client machine fingerprint>",
  "features":     ["dynamic-eq","linear-phase","mid-side","oversampling","sidechain"],
  "issuedAt":     <unix seconds int>,
  "expiresAt":    <unix seconds int, 0 == perpetual>,
  "activationId": "<uuid>"
}
```

### Canonicalization rules

To form the signing payload, the license is serialized **canonically**:

1. **Object keys sorted ascending** (byte/codepoint order), recursively at
   every object level.
2. **Compact** — no spaces, no newlines, no insignificant whitespace
   (equivalent to `JSON.stringify` over a key-sorted object).
3. **UTF-8** bytes. String escaping follows standard JSON (`JSON.stringify`
   semantics).
4. **Array element order is preserved** as emitted by the server. The server
   emits `features` sorted ascending, so it is deterministic.

For the license above, the canonical payload is (keys now in sorted order:
`activationId, edition, email, expiresAt, features, issuedAt, licenseId,
machineId, product`):

```
{"activationId":"<uuid>","edition":"pro","email":"a@b.com","expiresAt":0,"features":["dynamic-eq","linear-phase","mid-side","oversampling","sidechain"],"issuedAt":1794573701,"licenseId":"<uuid>","machineId":"<hex>","product":"whysho-eq1"}
```

### Token

```
token = base64url(canonicalJSON_bytes) + "." + base64url(signature)
```

- `signature` = Ed25519 signature (64 bytes) over the **canonical JSON bytes**.
- `base64url` is URL-safe (`-`/`_`) and **unpadded** (no trailing `=`).
- This is JWT-*like* but **not** a JWT: raw Ed25519, our own two-segment format,
  no header segment.

### Worked example

A real token issued by this server (dev key) for a `standard` license:

```
eyJhY3RpdmF0aW9uSWQiOiIzZmRiZDQwYS03NDBkLTQ5YjItODJmOC04MDI3ZTJiNjU5OTQiLCJlZGl0aW9uIjoic3RhbmRhcmQiLCJlbWFpbCI6ImJ1eWVyMkBleGFtcGxlLmNvbSIsImV4cGlyZXNBdCI6MCwiZmVhdHVyZXMiOlsiZHluYW1pYy1lcSIsIm1pZC1zaWRlIl0sImlzc3VlZEF0IjoxNzk0NTczNzc5LCJsaWNlbnNlSWQiOiI5M2QzM2U3OC1mMTYxLTRhMzQtODE1Yy1jM2E2ZjFmNzJlMzAiLCJtYWNoaW5lSWQiOiJlMDBjZmJhMTY0YjYyMjQ5N2U1OWJkZmEwNmVjNWRjZWUwZjJmZmFlZDRmOWFmOTk5NWQ2NTlhY2MwNDQzOGE0IiwicHJvZHVjdCI6IndoeXNoby1lcTEifQ.<base64url-signature>
```

Base64url-decoding the first segment yields exactly:

```
{"activationId":"3fdbd40a-740d-49b2-82f8-8027e2b65994","edition":"standard","email":"buyer2@example.com","expiresAt":0,"features":["dynamic-eq","mid-side"],"issuedAt":1794573779,"licenseId":"93d33e78-f161-4a34-815c-c3a6f1f72e30","machineId":"e00cfba164b622497e59bdfa06ec5dcee0f2ffaed4f9af9995d659acc04438a4","product":"whysho-eq1"}
```

---

## C++ verification steps (offline, in the plugin)

The plugin embeds the 32-byte public key (see `keys/public_key_cpp.txt`) and
verifies a token **without contacting the server**:

1. **Split** the token on the single `'.'` into `payloadB64` and `sigB64`.
   Reject if there is not exactly one `'.'` or either side is empty.
2. **base64url-decode** `payloadB64` → `payloadBytes` (the canonical JSON bytes).
   Use the URL-safe alphabet (`-`→`+`, `_`→`/`) and treat input as **unpadded**.
3. **base64url-decode** `sigB64` → `signature` (must be exactly **64 bytes**).
4. **Ed25519 verify**: `ed25519_verify(signature, payloadBytes, kLicensePublicKey)`
   — verify over the **raw decoded canonical-JSON bytes**. Do **not** re-parse
   and re-serialize before verifying; verify the bytes you decoded.
5. If the signature is valid, **parse** `payloadBytes` as JSON and enforce:
   - `product == "whysho-eq1"`,
   - `expiresAt == 0 || expiresAt >= now` (perpetual when `0`),
   - optionally `machineId == <this machine's fingerprint>`.
6. For an online re-check, POST the token to `/api/validate`; the server also
   applies **revocation** (a revoked license fails server-side even though the
   token stays cryptographically valid).

Reference C verifiers that interoperate with `@noble/ed25519`: **libsodium**
(`crypto_sign_verify_detached`) or **Monocypher** (`crypto_eddsa_check`), both
standard Ed25519 over SHA-512.

> **Round-trip is proven**: the server signs the canonical bytes with
> `@noble/ed25519`, and an independent decoder (base64url → bytes → Ed25519
> verify) using only `keys/public_key.hex` returns `true`; any single-byte
> change to the signature or payload returns `false`.

---

## Endpoints

All bodies are JSON. Validation errors return `400 {"error":"invalid-request"}`.

### `GET /api/health`
`200 → { "status": "ok", "product": "whysho-eq1", "time": <unix> }`

### `POST /api/activate`  *(rate-limited)*
Body: `{ "licenseKey", "machineId", "productId" }`

- Looks up the license, enforces max activations (seats, default 2), binds the
  `machineId`, returns a **signed token**.
- Idempotent for an already-bound machine (re-issues a token, `reused: true`).

| Result | Status |
| --- | --- |
| Success | `200 { token, licenseId, activationId, edition, features, expiresAt, reused }` |
| Unknown key / bad check digit | `404 { error: "unknown-key" }` |
| Seat limit reached | `409 { error: "seat-limit" }` |
| Revoked | `403 { error: "revoked" }` |
| Expired | `403 { error: "expired" }` |
| Wrong `productId` | `400 { error: "wrong-product" }` |

### `POST /api/deactivate`
Body: `{ "licenseKey", "machineId" }` → frees a seat.
`200 { "freed": true|false }`; `404` if the key is unknown.

### `POST /api/validate`  *(rate-limited)*
Body: `{ "token" }` → verifies signature + expiry (+ revocation) server-side.
Always `200 { "valid": boolean, "reason": string, "license"?: {...} }`.
`reason` ∈ `ok | bad-format | bad-signature | malformed-payload |
wrong-product | expired | revoked`.

### `POST /api/admin/licenses`  *(admin)*
Header: `x-admin-token: <WHYSHO_ADMIN_TOKEN>`
Body: `{ "email", "edition", "seats"?, "expiresAt"? }`
→ `201 { licenseKey, licenseId, email, edition, features, seats, expiresAt }`.
`licenseKey` format: `WSHO-XXXXX-XXXXX-XXXXX-XXXXX` (Crockford base32 with a
weighted mod-32 check digit; see `src/licenseKey.ts`). `expiresAt` of `0`
(or omitted) = perpetual.

### `POST /api/admin/revoke`  *(admin)*
Body: `{ "licenseKey" }` → `200 { "revoked": true }`; `404` if unknown.

Missing/invalid `x-admin-token` → `401 { "error": "unauthorized" }`.

---

## Example session (curl)

```bash
ADMIN=your-admin-token
BASE=http://127.0.0.1:8787

# Create a license
curl -s -X POST $BASE/api/admin/licenses \
  -H "content-type: application/json" -H "x-admin-token: $ADMIN" \
  -d '{"email":"buyer@example.com","edition":"pro","seats":2}'
# -> { "licenseKey": "WSHO-XXXXX-XXXXX-XXXXX-XXXXX", ... }

# Activate (machineId = hex sha256 of the machine fingerprint)
curl -s -X POST $BASE/api/activate \
  -H "content-type: application/json" \
  -d '{"licenseKey":"WSHO-...","machineId":"<64-hex>","productId":"whysho-eq1"}'
# -> { "token": "<base64url>.<base64url>", ... }

# Validate
curl -s -X POST $BASE/api/validate \
  -H "content-type: application/json" \
  -d '{"token":"<token>"}'
# -> { "valid": true, "reason": "ok", "license": {...} }
```

---

## Storage

SQLite at `WHYSHO_DB_PATH` (default `data/licenses.db`, gitignored; WAL mode).

**`licenses`**: `licenseKey` (PK), `licenseId` (unique), `email`, `edition`,
`features` (JSON), `seats`, `expiresAt`, `revoked`, `createdAt`.

**`activations`**: `activationId` (PK), `licenseKey` (FK → licenses,
`ON DELETE CASCADE`), `machineId`, `createdAt`, `lastSeenAt`.
Unique index on `(licenseKey, machineId)` makes re-activation of the same
machine idempotent.

---

## Keys

`npm run keygen -- --write` produces:

- `keys/public_key.hex` — the 32-byte public key (hex). **Committed.**
- `keys/public_key_cpp.txt` — the ready-to-paste `kLicensePublicKey[32]`
  C++ array. **Committed.**
- `keys/DEV_PRIVATE_KEY.txt` — a **dev-only** private key with a warning.
  **Gitignored — never committed.** It exists only so this working tree can run
  the dev server out of the box (it matches the committed `public_key.hex`).
  **Rotate before production** and keep real private keys in a secret manager.

> Security: **never commit any private key.** Only the public key and the C++
> snippet are committed. The private key lives only in
> `WHYSHO_LICENSE_PRIVATE_KEY` (secret manager / env) and the gitignored
> `keys/DEV_PRIVATE_KEY.txt` / `.env` for local dev.

---

## Docker

```bash
docker build -t whysho-eq1-license ./server
docker run --rm -p 8787:8787 \
  -e WHYSHO_LICENSE_PRIVATE_KEY=<hex> \
  -e WHYSHO_ADMIN_TOKEN=<secret> \
  -v whysho_data:/app/data \
  whysho-eq1-license
```

The SQLite database is persisted on the `/app/data` volume.

---

## Editions → features

| Edition | Features |
| --- | --- |
| `standard` | `dynamic-eq`, `mid-side` |
| `pro` | `dynamic-eq`, `linear-phase`, `mid-side`, `oversampling`, `sidechain` |

Feature arrays are sorted ascending before signing so both sides agree
byte-for-byte. Adjust the sets in `src/editions.ts`.
