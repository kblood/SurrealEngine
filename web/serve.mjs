// Minimal static dev server for the SurrealEngine browser harness.
// Serves the repo root (so both /web/index.html and /build-emscripten/*
// are reachable) with COOP/COEP headers - required for the -pthread build
// (SharedArrayBuffer) to work at all, not just for a future SharedArrayBuffer
// feature.
//
// Usage: node web/serve.mjs [port]     (default 8091)
import { createServer } from 'node:http';
import { access, readFile } from 'node:fs/promises';
import { extname, isAbsolute, join, normalize, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repositoryRoot = fileURLToPath(new URL('..', import.meta.url));
const port = Number(process.argv[2] ?? 8091);
const requestedRoot = process.argv[3];
const root = requestedRoot ? resolve(requestedRoot) : repositoryRoot;

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js':   'text/javascript; charset=utf-8',
  '.mjs':  'text/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.data': 'application/octet-stream',
  '.json': 'application/json',
  '.png':  'image/png',
  '.css':  'text/css; charset=utf-8',
};

createServer(async (req, res) => {
  try {
    let path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
	if (path === '/') path = requestedRoot ? '/index.html' : '/web/index.html';
    const file = normalize(join(root, path));
	const relativePath = relative(root, file);
	if (isAbsolute(relativePath) || relativePath.startsWith('..')) { res.writeHead(403); res.end(); return; }
    let servedFile = file;
    let contentEncoding = null;
    const accepted = String(req.headers['accept-encoding'] || '');
    if (/\bbr\b/.test(accepted)) {
      try { await access(file + '.br'); servedFile = file + '.br'; contentEncoding = 'br'; } catch {}
    }
    if (!contentEncoding && /\bgzip\b/.test(accepted)) {
      try { await access(file + '.gz'); servedFile = file + '.gz'; contentEncoding = 'gzip'; } catch {}
    }
    const body = await readFile(servedFile);
    const portable = relative(root, file).split('\\').join('/');
    const immutable = /^(?:assets|engine)\/[^/]+\.[0-9a-f]{64}\.(?:css|js|wasm)$/.test(portable);
    const responseHeaders = {
      'Content-Type': MIME[extname(file)] ?? 'application/octet-stream',
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
      'Cross-Origin-Resource-Policy': 'same-origin',
      'Cache-Control': immutable ? 'public, max-age=31536000, immutable' : 'no-cache, must-revalidate',
      'Vary': 'Accept-Encoding',
    };
    if (contentEncoding) responseHeaders['Content-Encoding'] = contentEncoding;
    res.writeHead(200, {
      ...responseHeaders,
    });
    res.end(req.method === 'HEAD' ? undefined : body);
  } catch {
    res.writeHead(404); res.end('not found');
  }
}).listen(port, () => console.log(`serving ${root} at http://localhost:${port}/`));
