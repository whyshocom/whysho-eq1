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
   (valid for a year), then asks
   `GET /api/plugin/license?product=eq1&machine=<id>&nonce=<random>`.

The site answers with a **signed token**: the answer (`owned`, the account's email),
the machine id and nonce the plugin sent, when it was issued and when it expires
(30 days later), signed with Ed25519. The plugin checks the signature against the
public key compiled into it (`src/licensing/LicensePublicKey.h`), and that the token
is for EQ1, for this computer, and echoes the nonce it just sent. Only then does
`owned: true` unlock. An answer without a valid signature is ignored.

The signed token is what gets saved for offline use, so the cached copy can't be
edited or copied to another computer either. The answer is re-checked in the background once each time a
DAW starts using EQ1, and every 12 hours while it stays open. Merely scanning the
plugin never touches the network.

What happens with each answer:

| Server says | Plugin does |
|-------------|-------------|
| Signed `owned: true` | Unlocked; the token is kept and stands for 30 days of offline use. |
| Signed `owned: false` (never bought, or refunded) | Not licensed. Falls back to the trial if any is left, otherwise switches off. |
| `401` (session ended: password reset, signed out on the site, a year passed) | Signed out, but an owner stays unlocked for 7 more days and is asked to sign in again. |
| No answer (offline, server error) | Nothing changes; the saved token keeps working until it expires. |
| An answer that isn't signed, or doesn't verify | Ignored; nothing changes, and the panel says the answer couldn't be verified. |

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
  system clock back doesn't add days. Every signed answer from the site also carries
  the site's time, which the plugin follows.
- If the records exist but none is valid (edited by hand, copied from another
  computer), the trial counts as used up. Signing in still works.

## Where things are stored

| What | Windows | macOS |
|------|---------|-------|
| Account (session token, email, the signed license token) | `%LOCALAPPDATA%\whysho\EQ1\account.json` | `~/Library/Application Support/whysho/EQ1/account.json` (mode 600) |
| Trial clock | `%LOCALAPPDATA%\whysho\EQ1\state.dat`, `HKCU\Software\whysho\EQ1`, `%ProgramData%\whysho\eq1.dat` | `~/Library/Application Support/whysho/EQ1/state.dat`, `~/Library/Preferences/com.whysho.eq1.state`, `/Users/Shared/.whysho/eq1.dat` |

What is sent to whysho.com: the session token; at sign-in, a device label ("whysho
EQ1 on <computer name>") that the site shows on the approval page; and with each
license check, an anonymous machine id (a salted hash of a hardware id) so the signed
answer is only valid on this computer. The site does not store the machine id.

## Code map

| File | Role |
|------|------|
| `src/licensing/AccountClient.*` | HTTP calls to the whysho.com plugin API. |
| `src/licensing/LicenseToken.*`, `LicensePublicKey.h` | Verifying the site's signed answer (Ed25519, Monocypher). |
| `src/licensing/LicenseStore.*` | The rules (`evaluate`) and local persistence. |
| `src/licensing/LicenseService.*` | The one shared authority per process: background thread, periodic re-check, lock-free flag for the audio thread. |
| `src/gui/LicenseDialog.*` | The account panel. |
| `tests/LicenseTests.cpp` | Tests for the rules; `--live` also exercises the real API. |

Debug builds honour a `WHYSHO_SITE` environment variable (e.g. `http://localhost:3000`)
for testing against a local copy of the site. Release builds ignore it.

## The signing key

The site signs with a private key that exists only in its environment
(`LICENSE_SIGNING_KEY`); the plugin carries the matching public key. The two must
match, and **the site has to be deployed with the key before a plugin build that
checks signatures is released**: until then the site's answers are unsigned and this
build won't unlock for anyone (the trial still works).

To rotate the key: `npm run license:keygen` in the whysho.com repo, set the new
`LICENSE_SIGNING_KEY` on the host, paste the printed array into
`src/licensing/LicensePublicKey.h`, release a new build. Older builds stop accepting
answers when the site switches keys (owners keep working until their saved token
expires).

## What this does not protect against

No copy protection that runs on the user's own computer is uncrackable, and this
repository is public. Specifically:

- **A patched binary.** Someone who modifies the plugin itself can remove the check.
  The signature stops everything short of that: a fake server, a redirected
  `whysho.com`, an edited or copied license file.
- **The trial is local.** Someone who finds and deletes all three records (or
  restores a VM snapshot) gets a new trial. Only a server-side trial can stop that.
- **No seat limit.** One account can be signed in on any number of computers.
- **Refunds take effect at the next online check**, so an offline machine keeps
  working for up to 30 days.

The trial records are bound to the machine with a keyed hash whose key is in the
binary: that stops casual editing and copying, nothing more.

### Website changes that would close the remaining gaps

1. A server-side trial keyed on the machine id, returning a signed end date.
2. Record a machine id per plugin session; add a seat cap and a device list on
   `/account`.
3. Sliding session expiry instead of a hard 365 days.
