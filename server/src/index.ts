import { config, assertSigningConfig, assertAdminConfig } from './config.js';
import { buildServer } from './server.js';

async function main(): Promise<void> {
  // Fail fast if the signing key is missing or malformed.
  assertSigningConfig();
  if (!config.adminToken) {
    // Non-fatal, but admin endpoints will reject every request with 401.
    console.warn(
      '[whysho] WARNING: WHYSHO_ADMIN_TOKEN is not set — admin endpoints are locked.',
    );
  } else {
    assertAdminConfig();
  }

  const app = await buildServer();

  try {
    await app.listen({ host: config.host, port: config.port });
    app.log.info(
      `Whysho EQ1 license server listening on http://${config.host}:${config.port}`,
    );
  } catch (err) {
    app.log.error(err);
    process.exit(1);
  }
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
