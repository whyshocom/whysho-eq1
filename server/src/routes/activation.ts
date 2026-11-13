import type { FastifyInstance, FastifyReply, FastifyRequest } from 'fastify';
import { config } from '../config.js';
import {
  activateSchema,
  deactivateSchema,
  validateSchema,
} from '../schemas.js';
import {
  ActivationError,
  activate,
  deactivate,
  validateToken,
} from '../service.js';

function badRequest(reply: FastifyReply, issues: unknown) {
  return reply.code(400).send({ error: 'invalid-request', details: issues });
}

export async function activationRoutes(app: FastifyInstance): Promise<void> {
  const limited = {
    config: {
      rateLimit: {
        max: config.rateLimitMax,
        timeWindow: config.rateLimitWindowMs,
      },
    },
  };

  // POST /api/activate
  app.post(
    '/api/activate',
    limited,
    async (req: FastifyRequest, reply: FastifyReply) => {
      const parsed = activateSchema.safeParse(req.body);
      if (!parsed.success) return badRequest(reply, parsed.error.issues);
      try {
        const result = activate(
          parsed.data.licenseKey,
          parsed.data.machineId,
          parsed.data.productId,
        );
        return reply.code(200).send(result);
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

  // POST /api/deactivate
  app.post(
    '/api/deactivate',
    async (req: FastifyRequest, reply: FastifyReply) => {
      const parsed = deactivateSchema.safeParse(req.body);
      if (!parsed.success) return badRequest(reply, parsed.error.issues);
      try {
        const result = deactivate(
          parsed.data.licenseKey,
          parsed.data.machineId,
        );
        return reply.code(200).send(result);
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

  // POST /api/validate
  app.post(
    '/api/validate',
    limited,
    async (req: FastifyRequest, reply: FastifyReply) => {
      const parsed = validateSchema.safeParse(req.body);
      if (!parsed.success) return badRequest(reply, parsed.error.issues);
      const result = validateToken(parsed.data.token);
      // Always 200: the caller inspects { valid, reason }.
      return reply.code(200).send(result);
    },
  );
}
