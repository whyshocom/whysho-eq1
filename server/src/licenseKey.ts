/**
 * Human-facing license key: WSHO-XXXXX-XXXXX-XXXXX-XXXXX
 *
 * - "WSHO" is a fixed product prefix.
 * - The 20 payload characters use the Crockford base32 alphabet
 *   (0-9 A-Z excluding I, L, O, U), grouped into four blocks of five.
 * - The final payload character is a check digit: a weighted mod-32 checksum
 *   over the preceding 19 symbols, mapped back into the Crockford alphabet.
 *   It stays inside the normal base32 alphabet so the key is uniformly
 *   groupable and the check character catches single-character typos and
 *   most transpositions.
 * - On input, Crockford's forgiving decode is applied (case-insensitive;
 *   I/L -> 1, O -> 0), hyphens optional, so keys are robust to manual entry.
 */
import { randomBytes } from 'node:crypto';

const ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ'; // 32 symbols, Crockford
const PREFIX = 'WSHO';
const DATA_LEN = 19; // random symbols
const TOTAL_LEN = 20; // + 1 check digit

const DECODE_MAP: Record<string, number> = (() => {
  const m: Record<string, number> = {};
  for (let i = 0; i < ALPHABET.length; i++) m[ALPHABET[i]] = i;
  // Crockford forgiving aliases
  m['I'] = 1;
  m['L'] = 1;
  m['O'] = 0;
  return m;
})();

function checkDigit(symbols: number[]): number {
  // Weighted sum mod 32. Weight grows with position so transpositions change it.
  let sum = 0;
  for (let i = 0; i < symbols.length; i++) {
    sum += symbols[i] * (i + 1);
  }
  return sum % 32;
}

function groupWithHyphens(payload: string): string {
  const groups: string[] = [];
  for (let i = 0; i < payload.length; i += 5) {
    groups.push(payload.slice(i, i + 5));
  }
  return [PREFIX, ...groups].join('-');
}

/** Generate a fresh, well-formed license key. */
export function generateLicenseKey(): string {
  const symbols: number[] = [];
  // Rejection-sample uniform symbols in [0,32).
  const buf = randomBytes(DATA_LEN * 2);
  let bi = 0;
  while (symbols.length < DATA_LEN) {
    if (bi >= buf.length) {
      const more = randomBytes(DATA_LEN);
      for (let i = 0; i < more.length; i++) buf[bi % buf.length] = more[i];
      bi = 0;
    }
    const b = buf[bi++];
    if (b < 248) {
      // 248 = floor(256/32)*32, reject the biased tail
      symbols.push(b % 32);
    }
  }
  const chk = checkDigit(symbols);
  const all = [...symbols, chk];
  const payload = all.map((n) => ALPHABET[n]).join('');
  return groupWithHyphens(payload);
}

/**
 * Normalize a user-entered key to canonical uppercase, hyphenated form.
 * Returns null if it cannot be parsed into the expected shape or the check
 * digit does not match.
 */
export function normalizeLicenseKey(input: string): string | null {
  const raw = input.trim().toUpperCase();
  // Strip hyphens/spaces and an optional leading prefix.
  let body = raw.replace(/[\s-]+/g, '');
  if (body.startsWith(PREFIX)) body = body.slice(PREFIX.length);
  if (body.length !== TOTAL_LEN) return null;

  const values: number[] = [];
  for (const ch of body) {
    const v = DECODE_MAP[ch];
    if (v === undefined) return null;
    values.push(v);
  }
  const data = values.slice(0, DATA_LEN);
  const providedCheck = values[DATA_LEN];
  if (checkDigit(data) !== providedCheck) return null;

  const payload = values.map((n) => ALPHABET[n]).join('');
  return groupWithHyphens(payload);
}

/** True if the key is structurally valid (shape + check digit). */
export function isValidLicenseKey(input: string): boolean {
  return normalizeLicenseKey(input) !== null;
}
