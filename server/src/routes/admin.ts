import type { FastifyInstance, FastifyReply, FastifyRequest } from 'fastify';
import { timingSafeEqual } from 'node:crypto';
import { config } from '../config.js';
import { createLicenseSchema, revokeSchema } from '../schemas.js';
import { ActivationError, createLicense, revoke } from '../service.js';

function adminAuthorized(req: FastifyRequest): boolean {
  const provided = req.headers['x-admin-token'];
  if (typeof provided !== 'string' || !config.adminToken) return false;
  const a = Buffer.from(provided);
  const b = Buffer.from(config.adminToken);
  if (a.length !== b.length) return false;
  return timingSafeEqual(a, b);
}

export async function adminRoutes(app: FastifyInstance): Promise<void> {
  app.addHook('preHandler', async (req, reply) => {
    if (!req.url.startsWith('/api/admin/')) return;
    if (!adminAuthorized(req)) {
      return reply.code(401).send({ error: 'unauthorized' });
    }
  });

  // POST /api/admin/licenses
  app.post(
    '/api/admin/licenses',
    async (req: FastifyRequest, reply: FastifyReply) => {
      const parsed = createLicenseSchema.safeParse(req.body);
      if (!parsed.success) {
        return reply
          .code(400)
          .send({ error: 'invalid-request', details: parsed.error.issues });
      }
      const created = createLicense(parsed.data);
      return reply.code(201).send(created);
    },
  );

  // POST /api/admin/revoke
  app.post(
    '/api/admin/revoke',
    async (req: FastifyRequest, reply: FastifyReply) => {
      const parsed = revokeSchema.safeParse(req.body);
      if (!parsed.success) {
        return reply
          .code(400)
          .send({ error: 'invalid-request', details: parsed.error.issues });
      }
      try {
        return reply.code(200).send(revoke(parsed.data.licenseKey));
      } catch (err) {
        if (err instanceof ActivationError) {
          return reply
            .code(err.status)
            .send({ error: err.reason, message: err.message });
        }
        throw err;
      }
    },
  );
}
