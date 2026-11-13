/**
 * Crypto core for Whysho EQ1 license tokens.
 *
 * Token format (our own, JWT-like but raw Ed25519):
 *   token = base64url(canonicalJSON) + "." + base64url(signature)
 *
 * The signing payload is the canonical JSON serialization of the license object:
 *   - object keys sorted ascending (recursively, at every level)
 *   - compact (no insignificant whitespace)
 *   - UTF-8 bytes
 *   - array element order preserved as emitted by the server
 *
 * The C++ plugin verifies by: splitting on '.', base64url-decoding the first
 * segment to recover the exact canonical JSON bytes, base64url-decoding the
 * second segment to recover the 64-byte signature, and running Ed25519 verify
 * over those canonical-JSON bytes with the embedded 32-byte public key.
 */
import * as ed from '@noble/ed25519';
import { sha512 } from '@noble/hashes/sha512';

// @noble/ed25519 v2 needs a synchronous SHA-512 implementation wired in before
// the synchronous sign/verify/getPublicKey helpers can be used.
ed.etc.sha512Sync = (...m: Uint8Array[]): Uint8Array =>
  sha512(ed.etc.concatBytes(...m));

export const PRODUCT_ID = 'whysho-eq1';

export type Edition = 'standard' | 'pro';

export interface License {
  product: 'whysho-eq1';
  licenseId: string;
  email: string;
  edition: Edition;
  machineId: string;
  features: string[];
  issuedAt: number;
  expiresAt: number; // 0 == perpetual
  activationId: string;
}

/**
 * Canonical JSON: recursively sort object keys ascending and emit compact JSON.
 * Strings/numbers/booleans/null use JSON.stringify so escaping and UTF-8 handling
 * match the standard JSON grammar. Array order is preserved.
 */
export function canonicalize(value: unknown): string {
  if (value === null || value === undefined) return 'null';
  if (Array.isArray(value)) {
    return '[' + value.map((v) => canonicalize(v)).join(',') + ']';
  }
  if (typeof value === 'object') {
    const obj = value as Record<string, unknown>;
    const keys = Object.keys(obj).sort();
    return (
      '{' +
      keys
        .map((k) => JSON.stringify(k) + ':' + canonicalize(obj[k]))
        .join(',') +
      '}'
    );
  }
  // string | number | boolean
  return JSON.stringify(value);
}

// ---- base64url helpers (no padding) ----

export function base64urlEncode(bytes: Uint8Array): string {
  return Buffer.from(bytes)
    .toString('base64')
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/g, '');
}

export function base64urlDecode(s: string): Uint8Array {
  const b64 = s.replace(/-/g, '+').replace(/_/g, '/');
  return new Uint8Array(Buffer.from(b64, 'base64'));
}

export function hexToBytes(hex: string): Uint8Array {
  const clean = hex.trim().toLowerCase().replace(/^0x/, '');
  if (clean.length % 2 !== 0 || /[^0-9a-f]/.test(clean)) {
    throw new Error('Invalid hex string');
  }
  const out = new Uint8Array(clean.length / 2);
  for (let i = 0; i < out.length; i++) {
    out[i] = parseInt(clean.slice(i * 2, i * 2 + 2), 16);
  }
  return out;
}

export function bytesToHex(bytes: Uint8Array): string {
  let s = '';
  for (const b of bytes) s += b.toString(16).padStart(2, '0');
  return s;
}

// ---- keypair ----

export function generatePrivateKey(): Uint8Array {
  return ed.utils.randomPrivateKey();
}

export function getPublicKey(privateKeyHex: string): Uint8Array {
  return ed.getPublicKey(hexToBytes(privateKeyHex));
}

// ---- sign / verify ----

/**
 * Produce a signed license token from a license object.
 * Signs over the canonical-JSON UTF-8 bytes of the license.
 */
export function signLicense(license: License, privateKeyHex: string): string {
  const canonical = canonicalize(license);
  const payloadBytes = new TextEncoder().encode(canonical);
  const sig = ed.sign(payloadBytes, hexToBytes(privateKeyHex));
  return base64urlEncode(payloadBytes) + '.' + base64urlEncode(sig);
}

export interface VerifyResult {
  valid: boolean;
  reason:
    | 'ok'
    | 'bad-format'
    | 'bad-signature'
    | 'malformed-payload'
    | 'wrong-product';
  license?: License;
}

/**
 * Verify a token's structure and Ed25519 signature over the canonical payload.
 * Does NOT check expiry or revocation — callers layer those checks on top.
 */
export function verifyTokenSignature(
  token: string,
  publicKey: Uint8Array,
): VerifyResult {
  const parts = token.split('.');
  if (parts.length !== 2 || !parts[0] || !parts[1]) {
    return { valid: false, reason: 'bad-format' };
  }
  let payloadBytes: Uint8Array;
  let sig: Uint8Array;
  try {
    payloadBytes = base64urlDecode(parts[0]);
    sig = base64urlDecode(parts[1]);
  } catch {
    return { valid: false, reason: 'bad-format' };
  }

  let okSig = false;
  try {
    okSig = ed.verify(sig, payloadBytes, publicKey);
  } catch {
    okSig = false;
  }
  if (!okSig) return { valid: false, reason: 'bad-signature' };

  let license: License;
  try {
    license = JSON.parse(new TextDecoder().decode(payloadBytes)) as License;
  } catch {
    return { valid: false, reason: 'malformed-payload' };
  }

  if (license.product !== PRODUCT_ID) {
    return { valid: false, reason: 'wrong-product', license };
  }

  return { valid: true, reason: 'ok', license };
}
