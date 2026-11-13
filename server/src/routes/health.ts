import type { FastifyInstance } from 'fastify';
import { nowSeconds } from '../service.js';

export async function healthRoutes(app: FastifyInstance): Promise<void> {
  app.get('/api/health', async () => ({
    status: 'ok',
    product: 'whysho-eq1',
    time: nowSeconds(),
  }));
}
