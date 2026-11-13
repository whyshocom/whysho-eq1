/**
 * Convenience: create a license directly in the DB and (optionally) activate it
 * for a test machine, printing the license key and a signed token.
 *
 * Requires WHYSHO_LICENSE_PRIVATE_KEY to be set (see .env.example).
 *
 * Usage:
 *   npm run issue-test-license
 *   npm run issue-test-license -- --email you@example.com --edition pro --seats 3
 *   npm run issue-test-license -- --no-activate
 */
import { createHash } from 'node:crypto';
import { assertSigningConfig } from '../src/config.js';
import { activate, createLicense } from '../src/service.js';
import type { Edition } from '../src/crypto.js';

function arg(name: string, fallback: string): string {
  const i = process.argv.indexOf('--' + name);
  return i >= 0 && process.argv[i + 1] ? process.argv[i + 1] : fallback;
}

function main(): void {
  assertSigningConfig();

  const email = arg('email', 'tester@whysho.audio');
  const edition = arg('edition', 'pro') as Edition;
  const seats = Number.parseInt(arg('seats', '2'), 10);
  const expiresAt = Number.parseInt(arg('expires', '0'), 10);
  const doActivate = !process.argv.includes('--no-activate');

  if (edition !== 'standard' && edition !== 'pro') {
    throw new Error('edition must be "standard" or "pro"');
  }

  const lic = createLicense({ email, edition, seats, expiresAt });
  console.log('Created license:');
  console.log(JSON.stringify(lic, null, 2));

  if (doActivate) {
    const machineId = createHash('sha256')
      .update('test-machine-fingerprint')
      .digest('hex');
    const result = activate(lic.licenseKey, machineId, 'whysho-eq1');
    console.log('');
    console.log('Activated for machineId=' + machineId);
    console.log('Token:');
    console.log(result.token);
  }
}

main();
