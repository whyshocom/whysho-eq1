# ---- build stage ----
FROM node:22-bookworm-slim AS build
WORKDIR /app

# better-sqlite3 ships prebuilt binaries for common platforms, but keep the
# toolchain available so a source build can fall back cleanly if needed.
RUN apt-get update \
  && apt-get install -y --no-install-recommends python3 make g++ \
  && rm -rf /var/lib/apt/lists/*

COPY package.json package-lock.json* ./
RUN npm install

COPY tsconfig.json ./
COPY src ./src
COPY scripts ./scripts
RUN npm run build

# Drop dev dependencies for the runtime image.
RUN npm prune --omit=dev

# ---- runtime stage ----
FROM node:22-bookworm-slim AS runtime
WORKDIR /app
ENV NODE_ENV=production

COPY --from=build /app/node_modules ./node_modules
COPY --from=build /app/dist ./dist
COPY package.json ./

# Persist the SQLite database on a volume.
RUN mkdir -p /app/data
VOLUME ["/app/data"]
ENV WHYSHO_DB_PATH=/app/data/licenses.db

EXPOSE 8787
# WHYSHO_LICENSE_PRIVATE_KEY and WHYSHO_ADMIN_TOKEN must be supplied at runtime.
CMD ["node", "dist/index.js"]
