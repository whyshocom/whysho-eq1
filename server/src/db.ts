import Database from 'better-sqlite3';
import { mkdirSync } from 'node:fs';
import { dirname } from 'node:path';
import { config } from './config.js';
import type { Edition } from './crypto.js';

export interface LicenseRow {
  licenseKey: string;
  licenseId: string;
  email: string;
  edition: Edition;
  features: string; // JSON-encoded string[]
  seats: number;
  expiresAt: number; // unix seconds, 0 == perpetual
  revoked: number; // 0 | 1
  createdAt: number; // unix seconds
}

export interface ActivationRow {
  activationId: string;
  licenseKey: string;
  machineId: string;
  createdAt: number;
  lastSeenAt: number;
}

mkdirSync(dirname(config.dbPath), { recursive: true });

export const db = new Database(config.dbPath);
db.pragma('journal_mode = WAL');
db.pragma('foreign_keys = ON');

db.exec(`
  CREATE TABLE IF NOT EXISTS licenses (
    licenseKey TEXT PRIMARY KEY,
    licenseId  TEXT NOT NULL UNIQUE,
    email      TEXT NOT NULL,
    edition    TEXT NOT NULL,
    features   TEXT NOT NULL,
    seats      INTEGER NOT NULL,
    expiresAt  INTEGER NOT NULL DEFAULT 0,
    revoked    INTEGER NOT NULL DEFAULT 0,
    createdAt  INTEGER NOT NULL
  );

  CREATE TABLE IF NOT EXISTS activations (
    activationId TEXT PRIMARY KEY,
    licenseKey   TEXT NOT NULL,
    machineId    TEXT NOT NULL,
    createdAt    INTEGER NOT NULL,
    lastSeenAt   INTEGER NOT NULL,
    FOREIGN KEY (licenseKey) REFERENCES licenses(licenseKey) ON DELETE CASCADE
  );

  CREATE UNIQUE INDEX IF NOT EXISTS idx_activation_seat
    ON activations(licenseKey, machineId);
  CREATE INDEX IF NOT EXISTS idx_activation_license
    ON activations(licenseKey);
`);

const stmts = {
  getLicenseByKey: db.prepare<[string], LicenseRow>(
    'SELECT * FROM licenses WHERE licenseKey = ?',
  ),
  getLicenseById: db.prepare<[string], LicenseRow>(
    'SELECT * FROM licenses WHERE licenseId = ?',
  ),
  insertLicense: db.prepare(
    `INSERT INTO licenses
       (licenseKey, licenseId, email, edition, features, seats, expiresAt, revoked, createdAt)
     VALUES
       (@licenseKey, @licenseId, @email, @edition, @features, @seats, @expiresAt, @revoked, @createdAt)`,
  ),
  revokeLicense: db.prepare('UPDATE licenses SET revoked = 1 WHERE licenseKey = ?'),

  getActivation: db.prepare<[string, string], ActivationRow>(
    'SELECT * FROM activations WHERE licenseKey = ? AND machineId = ?',
  ),
  countActivations: db.prepare<[string], { n: number }>(
    'SELECT COUNT(*) AS n FROM activations WHERE licenseKey = ?',
  ),
  insertActivation: db.prepare(
    `INSERT INTO activations (activationId, licenseKey, machineId, createdAt, lastSeenAt)
     VALUES (@activationId, @licenseKey, @machineId, @createdAt, @lastSeenAt)`,
  ),
  touchActivation: db.prepare(
    'UPDATE activations SET lastSeenAt = ? WHERE activationId = ?',
  ),
  deleteActivation: db.prepare(
    'DELETE FROM activations WHERE licenseKey = ? AND machineId = ?',
  ),
};

export function getLicenseByKey(licenseKey: string): LicenseRow | undefined {
  return stmts.getLicenseByKey.get(licenseKey);
}

export function getLicenseById(licenseId: string): LicenseRow | undefined {
  return stmts.getLicenseById.get(licenseId);
}

export function insertLicense(row: LicenseRow): void {
  stmts.insertLicense.run(row);
}

export function revokeLicense(licenseKey: string): void {
  stmts.revokeLicense.run(licenseKey);
}

export function getActivation(
  licenseKey: string,
  machineId: string,
): ActivationRow | undefined {
  return stmts.getActivation.get(licenseKey, machineId);
}

export function countActivations(licenseKey: string): number {
  return stmts.countActivations.get(licenseKey)?.n ?? 0;
}

export function insertActivation(row: ActivationRow): void {
  stmts.insertActivation.run(row);
}

export function touchActivation(activationId: string, when: number): void {
  stmts.touchActivation.run(when, activationId);
}

export function deleteActivation(
  licenseKey: string,
  machineId: string,
): boolean {
  return stmts.deleteActivation.run(licenseKey, machineId).changes > 0;
}
