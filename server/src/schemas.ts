import { z } from 'zod';

// A machine fingerprint is a hex SHA-256 (64 hex chars). We accept any
// non-empty hex-ish string of reasonable length to stay forgiving of client
// implementations, but prefer the canonical 64-char form.
const machineId = z
  .string()
  .trim()
  .min(8, 'machineId too short')
  .max(128, 'machineId too long')
  .regex(/^[0-9a-fA-F]+$/, 'machineId must be hex');

const licenseKey = z.string().trim().min(8).max(64);
const email = z.email().trim().max(254);

export const activateSchema = z.object({
  licenseKey,
  machineId,
  productId: z.string().trim().min(1).max(64),
});
export type ActivateBody = z.infer<typeof activateSchema>;

export const deactivateSchema = z.object({
  licenseKey,
  machineId,
});
export type DeactivateBody = z.infer<typeof deactivateSchema>;

export const validateSchema = z.object({
  token: z.string().trim().min(1).max(8192),
});
export type ValidateBody = z.infer<typeof validateSchema>;

export const createLicenseSchema = z.object({
  email,
  edition: z.enum(['standard', 'pro']),
  // Optional; default applied from config in the handler.
  seats: z.number().int().positive().max(1000).optional(),
  // Unix seconds; 0 (or omitted) == perpetual.
  expiresAt: z.number().int().nonnegative().optional(),
});
export type CreateLicenseBody = z.infer<typeof createLicenseSchema>;

export const revokeSchema = z.object({
  licenseKey,
});
export type RevokeBody = z.infer<typeof revokeSchema>;
