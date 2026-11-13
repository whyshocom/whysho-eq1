import type { Edition } from './crypto.js';

/**
 * Feature sets per edition. Kept sorted ascending so the signed `features`
 * array is deterministic on both the server and the C++ verifier.
 *
 * The full feature universe (as documented in the crypto contract) is:
 *   dynamic-eq, linear-phase, mid-side, oversampling, sidechain
 */
export const EDITION_FEATURES: Record<Edition, string[]> = {
  standard: ['dynamic-eq', 'mid-side'],
  pro: ['dynamic-eq', 'linear-phase', 'mid-side', 'oversampling', 'sidechain'],
};

export function featuresForEdition(edition: Edition): string[] {
  return [...EDITION_FEATURES[edition]].sort();
}
