import { existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

// Load a local .env if present (Node 22 built-in; no dependency needed).
try {
  if (existsSync('.env')) {
    // @ts-ignore - available on Node >= 20.6
    process.loadEnvFile('.env');
  }
} catch {
  // ignore: env may already be provided by the runtime
}

const __dirname = dirname(fileURLToPath(import.meta.url));
// src/ -> server/
const serverRoot = resolve(__dirname, '..');

function int(name: string, fallback: number): number {
  const v = process.env[name];
  if (v === undefined || v === '') return fallback;
  const n = Number.parseInt(v, 10);
  return Number.isFinite(n) ? n : fallback;
}

export const config = {
  host: process.env.HOST ?? '0.0.0.0',
  port: int('PORT', 8787),

  /** Ed25519 private key (hex). Required to sign tokens. */
  privateKeyHex: process.env.WHYSHO_LICENSE_PRIVATE_KEY ?? '',

  /** Shared secret for admin endpoints (x-admin-token header). */
  adminToken: process.env.WHYSHO_ADMIN_TOKEN ?? '',

  /** SQLite file path. */
  dbPath:
    process.env.WHYSHO_DB_PATH ?? resolve(serverRoot, 'data', 'licenses.db'),

  /** Default seat count for newly created licenses. */
  defaultSeats: int('WHYSHO_DEFAULT_SEATS', 2),

  /** Rate limiting. */
  rateLimitMax: int('WHYSHO_RATE_LIMIT_MAX', 60),
  rateLimitWindowMs: int('WHYSHO_RATE_LIMIT_WINDOW_MS', 60_000),

  serverRoot,
} as const;

export function assertSigningConfig(): void {
  if (!config.privateKeyHex) {
    throw new Error(
      'WHYSHO_LICENSE_PRIVATE_KEY is not set. Run `npm run keygen` and set it ' +
        '(see .env.example).',
    );
  }
  if (!/^[0-9a-fA-F]{64}$/.test(config.privateKeyHex.replace(/^0x/, ''))) {
    throw new Error(
      'WHYSHO_LICENSE_PRIVATE_KEY must be a 32-byte (64 hex char) Ed25519 ' +
        'private key.',
    );
  }
}

export function assertAdminConfig(): void {
  if (!config.adminToken) {
    throw new Error(
      'WHYSHO_ADMIN_TOKEN is not set. Admin endpoints are disabled until it is.',
    );
  }
}
