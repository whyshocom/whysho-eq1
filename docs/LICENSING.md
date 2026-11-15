# whysho EQ1: licensing

How the 14-day trial and the account-based license work in 0.2.0, and an honest
account of what they do and don't protect against.

## The model

Licensing is account-based, the same as the other whysho plugins: **EQ1 is unlocked
when it is signed in to a whysho.com account that owns it.** There are no license
keys and no separate license server. The plugin talks to the website's plugin API
(`whysho.com/api/plugin/*`, documented in the whysho.com README).

| State | What the user gets |
|-------|--------------------|
| Trial (14 days) | Everything works. |
| Licensed | Everything works. Confirmed online, cached for offline use. |
| Trial over, not licensed | EQ1 is switched off on that computer: audio passes through unchanged and the editor shows only the account panel. |

"Switched off" is a dry passthrough on purpose. The plugin still loads and keeps its
settings, so an old project opens, plays and saves without losing anything; it just
isn't equalised. It never mutes, adds noise, or refuses to load.

## Signing in

"Sign in with browser" is a device-code flow, so Google / Apple accounts work too:

1. `POST /api/plugin/login/start` returns a short code. The plugin shows it and opens
   `whysho.com/activate?code=...` in the default browser.
2. The user signs in on the site and approves the code.
3. The plugin polls `POST /api/plugin/login/poll` until it gets a session token
   (valid for a year), then asks `GET /api/plugin/license?product=eq1`.

`{ owned: true }` unlocks. The answer is re-checked in the background once each time a
DAW starts using EQ1, and every 12 hours while it stays open. Merely scanning the
plugin never touches the network.

What happens with each answer:

| Server says | Plugin does |
|-------------|-------------|
| `owned: true` | Unlocked; remembered for 30 days of offline use. |
| `owned: false` (never bought, or refunded) | Not licensed. Falls back to the trial if any is left, otherwise switches off. |
| `401` (session ended: password reset, signed out on the site, a year passed) | Signed out, but an owner stays unlocked for 7 more days and is asked to sign in again. |
| No answer (offline, server error) | Nothing changes; the cached answer keeps working until its 30 days run out. |

All open EQ1 instances share one license state: signing in once unlocks all of them,
and a project with 60 instances does one network check, not 60.

## The trial

- 14 days, per computer, no account needed.
- The clock starts the first time EQ1 is really used (audio is prepared or the window
  is opened), not when a DAW merely scans the plugin folder.
- The start date is kept in three places per computer (see `LicenseStore.cpp`), each
  bound to that machine. Deleting or corrupting one doesn't reset the trial; the
  earliest valid date wins and the others are repaired.
- Time only runs forward: the latest moment ever seen is stored, so setting the
  system clock back doesn't add days.
- If the records exist but none is valid (edited by hand, copied from another
  computer), the trial counts as used up. Signing in still works.

## Where things are stored

| What | Windows | macOS |
|------|---------|-------|
| Account (session token, email, last answer) | `%LOCALAPPDATA%\whysho\EQ1\account.json` | `~/Library/Application Support/whysho/EQ1/account.json` (mode 600) |
| Trial clock | `%LOCALAPPDATA%\whysho\EQ1\state.dat`, `HKCU\Software\whysho\EQ1`, `%ProgramData%\whysho\eq1.dat` | `~/Library/Application Support/whysho/EQ1/state.dat`, `~/Library/Preferences/com.whysho.eq1.state`, `/Users/Shared/.whysho/eq1.dat` |

The machine id used to bind these records is a hash that never leaves the computer.
The only things sent to whysho.com are the session token and, at sign-in, a device
label ("whysho EQ1 on <computer name>") that the site shows on the approval page.

## Code map

| File | Role |
|------|------|
| `src/licensing/AccountClient.*` | HTTP calls to the whysho.com plugin API. |
| `src/licensing/LicenseStore.*` | The rules (`evaluate`) and local persistence. |
| `src/licensing/LicenseService.*` | The one shared authority per process: background thread, periodic re-check, lock-free flag for the audio thread. |
| `src/gui/LicenseDialog.*` | The account panel. |
| `tests/LicenseTests.cpp` | Tests for the rules; `--live` also exercises the real API. |

Debug builds honour a `WHYSHO_SITE` environment variable (e.g. `http://localhost:3000`)
for testing against a local copy of the site. Release builds ignore it.

## What this does not protect against

No copy protection that runs on the user's own computer is uncrackable, and this
repository is public. Specifically:

- **The server's answers are not signed.** Someone who redirects `whysho.com` on
  their own machine and installs their own root certificate can answer
  `owned: true`. So can a patched binary. Stopping the first needs the site to sign
  its license responses (see below).
- **The trial is local.** Someone who finds and deletes all three records (or
  restores a VM snapshot) gets a new trial. Only a server-side trial can stop that.
- **No seat limit.** One account can be signed in on any number of computers.
- **Refunds take effect at the next online check**, so an offline machine keeps
  working for up to 30 days.

The local records are bound to the machine with a keyed hash whose key is in the
binary: that stops casual editing and copying, nothing more.

### Website changes that would close these gaps

1. Sign the `/api/plugin/license` response (Ed25519 over product, account, machine
   id, a nonce from the plugin, and an expiry); the plugin then verifies it offline.
2. A server-side trial keyed on the machine id, returning a signed end date.
3. Record a machine id per plugin session; add a seat cap and a device list on
   `/account`.
4. Sliding session expiry instead of a hard 365 days.
