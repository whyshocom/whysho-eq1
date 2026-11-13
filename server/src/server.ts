import Fastify, { type FastifyInstance } from 'fastify';
import rateLimit from '@fastify/rate-limit';
import { config } from './config.js';
import { healthRoutes } from './routes/health.js';
import { activationRoutes } from './routes/activation.js';
import { adminRoutes } from './routes/admin.js';

export async function buildServer(): Promise<FastifyInstance> {
  const app = Fastify({
    logger: {
      level: process.env.LOG_LEVEL ?? 'info',
    },
    bodyLimit: 64 * 1024,
    trustProxy: true,
  });

  // Rate limiting is registered globally but only enforced on routes that
  // opt in via `config.rateLimit` (activate + validate).
  await app.register(rateLimit, {
    global: false,
    max: config.rateLimitMax,
    timeWindow: config.rateLimitWindowMs,
  });

  await app.register(healthRoutes);
  await app.register(activationRoutes);
  await app.register(adminRoutes);

  app.setErrorHandler((err, _req, reply) => {
    app.log.error(err);
    if (!reply.sent) {
      reply.code(500).send({ error: 'internal-error' });
    }
  });

  return app;
}
