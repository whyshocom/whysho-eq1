/**
 * Generate an Ed25519 keypair for signing Whysho EQ1 license tokens.
 *
 * Prints:
 *   - the PRIVATE key (hex)  -> keep secret, set as WHYSHO_LICENSE_PRIVATE_KEY
 *   - the PUBLIC key (hex)   -> written to keys/public_key.hex
 *   - a ready-to-paste C++ snippet for the plugin to embed.
 *
 * Usage:
 *   npm run keygen               # print only
 *   npm run keygen -- --write    # also write keys/ artifacts (dev keypair)
 */
import { writeFileSync, mkdirSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  generatePrivateKey,
  getPublicKey,
  bytesToHex,
} from '../src/crypto.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const serverRoot = resolve(__dirname, '..');
const keysDir = resolve(serverRoot, 'keys');

function cppSnippet(pub: Uint8Array): string {
  const rows: string[] = [];
  for (let i = 0; i < pub.length; i += 8) {
    const chunk = Array.from(pub.slice(i, i + 8))
      .map((b) => '0x' + b.toString(16).padStart(2, '0').toUpperCase())
      .join(', ');
    rows.push('    ' + chunk + ',');
  }
  // Trim trailing comma on the final value for cleanliness.
  const body = rows.join('\n').replace(/,(\s*)$/, '$1');
  return (
    '// Whysho EQ1 license verification public key (Ed25519, 32 bytes).\n' +
    '// Paste into the plugin and verify tokens offline against it.\n' +
    'static constexpr uint8_t kLicensePublicKey[32] = {\n' +
    body +
    '\n};'
  );
}

function main(): void {
  const write = process.argv.includes('--write');

  const priv = generatePrivateKey();
  const privHex = bytesToHex(priv);
  const pub = getPublicKey(privHex);
  const pubHex = bytesToHex(pub);
  const snippet = cppSnippet(pub);

  console.log('========================================================');
  console.log(' Whysho EQ1 license signing keypair (Ed25519)');
  console.log('========================================================');
  console.log('');
  console.log('PRIVATE KEY (hex) — KEEP SECRET, set WHYSHO_LICENSE_PRIVATE_KEY:');
  console.log('  ' + privHex);
  console.log('');
  console.log('PUBLIC KEY (hex):');
  console.log('  ' + pubHex);
  console.log('');
  console.log('C++ snippet for the plugin:');
  console.log('');
  console.log(snippet);
  console.log('');

  if (write) {
    mkdirSync(keysDir, { recursive: true });
    writeFileSync(resolve(keysDir, 'public_key.hex'), pubHex + '\n', 'utf8');
    writeFileSync(
      resolve(keysDir, 'public_key_cpp.txt'),
      snippet + '\n',
      'utf8',
    );
    writeFileSync(
      resolve(keysDir, 'DEV_PRIVATE_KEY.txt'),
      [
        '============================================================',
        ' DEV-ONLY Ed25519 PRIVATE KEY — DO NOT USE IN PRODUCTION',
        '============================================================',
        '',
        'This key exists only so the plugin team has a working public',
        'key to embed during development. ROTATE BEFORE PRODUCTION:',
        'generate a fresh keypair, keep the private key in a secret',
        'manager, and re-embed the new public key in the plugin.',
        '',
        'NEVER commit a production private key to version control.',
        '',
        'PRIVATE KEY (hex):',
        privHex,
        '',
        'PUBLIC KEY (hex):',
        pubHex,
        '',
      ].join('\n'),
      'utf8',
    );
    console.log('Wrote:');
    console.log('  ' + resolve(keysDir, 'public_key.hex'));
    console.log('  ' + resolve(keysDir, 'public_key_cpp.txt'));
    console.log('  ' + resolve(keysDir, 'DEV_PRIVATE_KEY.txt') + '  (dev only)');
    console.log('');
  }

  console.log('Next steps:');
  console.log('  export WHYSHO_LICENSE_PRIVATE_KEY=' + privHex);
  console.log('  (or put it in server/.env)');
}

main();
