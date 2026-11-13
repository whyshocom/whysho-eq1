import { randomUUID } from 'node:crypto';
import { config } from './config.js';
import {
  type Edition,
  type License,
  PRODUCT_ID,
  signLicense,
  verifyTokenSignature,
  getPublicKey,
} from './crypto.js';
import { featuresForEdition } from './editions.js';
import {
  type LicenseRow,
  countActivations,
  deleteActivation,
  getActivation,
  getLicenseByKey,
  getLicenseById,
  insertActivation,
  insertLicense,
  revokeLicense,
  touchActivation,
} from './db.js';
import { generateLicenseKey, normalizeLicenseKey } from './licenseKey.js';

export function nowSeconds(): number {
  return Math.floor(Date.now() / 1000);
}

export function isExpired(expiresAt: number, at = nowSeconds()): boolean {
  return expiresAt !== 0 && expiresAt < at;
}

/** Cached public key derived from the configured private key. */
let cachedPublicKey: Uint8Array | null = null;
export function publicKey(): Uint8Array {
  if (!cachedPublicKey) cachedPublicKey = getPublicKey(config.privateKeyHex);
  return cachedPublicKey;
}

export class ActivationError extends Error {
  constructor(
    public status: number,
    public reason: string,
    message?: string,
  ) {
    super(message ?? reason);
  }
}

export interface ActivateResult {
  token: string;
  licenseId: string;
  activationId: string;
  edition: Edition;
  features: string[];
  expiresAt: number;
  reused: boolean;
}

/**
 * Activate a seat for (licenseKey, machineId) and return a signed token.
 * Idempotent for an already-activated machine (re-issues a fresh token).
 */
export function activate(
  rawLicenseKey: string,
  machineId: string,
  productId: string,
): ActivateResult {
  if (productId !== PRODUCT_ID) {
    throw new ActivationError(400, 'wrong-product', `Unknown product: ${productId}`);
  }

  const normalized = normalizeLicenseKey(rawLicenseKey);
  const license: LicenseRow | undefined = normalized
    ? getLicenseByKey(normalized)
    : undefined;
  if (!license) {
    throw new ActivationError(404, 'unknown-key', 'License key not found');
  }
  if (license.revoked) {
    throw new ActivationError(403, 'revoked', 'License has been revoked');
  }
  if (isExpired(license.expiresAt)) {
    throw new ActivationError(403, 'expired', 'License has expired');
  }

  const existing = getActivation(license.licenseKey, machineId);
  const now = nowSeconds();
  let activationId: string;
  let reused: boolean;

  if (existing) {
    activationId = existing.activationId;
    touchActivation(activationId, now);
    reused = true;
  } else {
    const used = countActivations(license.licenseKey);
    if (used >= license.seats) {
      throw new ActivationError(
        409,
        'seat-limit',
        `Seat limit reached (${used}/${license.seats})`,
      );
    }
    activationId = randomUUID();
    insertActivation({
      activationId,
      licenseKey: license.licenseKey,
      machineId,
      createdAt: now,
      lastSeenAt: now,
    });
    reused = false;
  }

  const features = JSON.parse(license.features) as string[];
  const licenseObj: License = {
    product: PRODUCT_ID,
    licenseId: license.licenseId,
    email: license.email,
    edition: license.edition,
    machineId,
    features,
    issuedAt: now,
    expiresAt: license.expiresAt,
    activationId,
  };

  const token = signLicense(licenseObj, config.privateKeyHex);

  return {
    token,
    licenseId: license.licenseId,
    activationId,
    edition: license.edition,
    features,
    expiresAt: license.expiresAt,
    reused,
  };
}

export function deactivate(
  rawLicenseKey: string,
  machineId: string,
): { freed: boolean } {
  const normalized = normalizeLicenseKey(rawLicenseKey);
  const license = normalized ? getLicenseByKey(normalized) : undefined;
  if (!license) {
    throw new ActivationError(404, 'unknown-key', 'License key not found');
  }
  const freed = deleteActivation(license.licenseKey, machineId);
  return { freed };
}

export interface ValidateResult {
  valid: boolean;
  reason: string;
  license?: Pick<
    License,
    'licenseId' | 'edition' | 'email' | 'expiresAt' | 'machineId' | 'activationId'
  >;
}

/**
 * Server-side validation: verify signature, then layer expiry + revocation
 * checks (the DB is consulted by licenseId so revoked licenses fail here even
 * though the token itself remains cryptographically valid).
 */
export function validateToken(token: string): ValidateResult {
  const res = verifyTokenSignature(token, publicKey());
  if (!res.valid || !res.license) {
    return { valid: false, reason: res.reason };
  }
  const lic = res.license;

  if (isExpired(lic.expiresAt)) {
    return { valid: false, reason: 'expired' };
  }

  // Optional online revocation check.
  const row = getLicenseById(lic.licenseId);
  if (row && row.revoked) {
    return { valid: false, reason: 'revoked' };
  }

  return {
    valid: true,
    reason: 'ok',
    license: {
      licenseId: lic.licenseId,
      edition: lic.edition,
      email: lic.email,
      expiresAt: lic.expiresAt,
      machineId: lic.machineId,
      activationId: lic.activationId,
    },
  };
}

export interface CreatedLicense {
  licenseKey: string;
  licenseId: string;
  email: string;
  edition: Edition;
  features: string[];
  seats: number;
  expiresAt: number;
}

export function createLicense(input: {
  email: string;
  edition: Edition;
  seats?: number;
  expiresAt?: number;
}): CreatedLicense {
  const licenseKey = generateLicenseKey();
  const licenseId = randomUUID();
  const features = featuresForEdition(input.edition);
  const seats = input.seats ?? config.defaultSeats;
  const expiresAt = input.expiresAt ?? 0;

  insertLicense({
    licenseKey,
    licenseId,
    email: input.email,
    edition: input.edition,
    features: JSON.stringify(features),
    seats,
    expiresAt,
    revoked: 0,
    createdAt: nowSeconds(),
  });

  return {
    licenseKey,
    licenseId,
    email: input.email,
    edition: input.edition,
    features,
    seats,
    expiresAt,
  };
}

export function revoke(rawLicenseKey: string): { revoked: boolean } {
  const normalized = normalizeLicenseKey(rawLicenseKey);
  const license = normalized ? getLicenseByKey(normalized) : undefined;
  if (!license) {
    throw new ActivationError(404, 'unknown-key', 'License key not found');
  }
  revokeLicense(license.licenseKey);
  return { revoked: true };
}
