// Static development server for the SurrealEngine WebXR port.
//
// The repository root is served so /web/* and /build-emscripten/* remain
// reachable. COOP/COEP are required by the pthread build (SharedArrayBuffer).
//
// Backward-compatible usage:
//   node web/serve.mjs [port]
//
// Explicit HTTP/TLS usage:
//   node web/serve.mjs --port 8091 [--host 127.0.0.1]
//   node web/serve.mjs --port 8443 --host 0.0.0.0 --cert PATH --key PATH
import { createServer as createHttpServer } from 'node:http';
import { createServer as createHttpsServer } from 'node:https';
import { constants as fsConstants } from 'node:fs';
import { access, readFile, realpath, stat } from 'node:fs/promises';
import { isIP } from 'node:net';
import { extname, isAbsolute, relative, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = await realpath(fileURLToPath(new URL('..', import.meta.url)));

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.data': 'application/octet-stream',
  '.json': 'application/json',
  '.webmanifest': 'application/manifest+json; charset=utf-8',
  '.png': 'image/png',
  '.svg': 'image/svg+xml; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
};

const SECURITY_HEADERS = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
  'Cross-Origin-Resource-Policy': 'same-origin',
};

function usage() {
  return [
    'Usage: node web/serve.mjs [port]',
    '       node web/serve.mjs [--port PORT] [--host HOST] [--cert PATH --key PATH]',
    '',
    'Defaults: --port 8091 --host 127.0.0.1 (HTTP)',
    'HTTPS is enabled only when both --cert and --key are supplied.',
    'Certificate and key paths must resolve inside this repository.',
  ].join('\n');
}

function failCli(message) {
  console.error(`server configuration error: ${message}`);
  console.error(usage());
  process.exitCode = 2;
}

function parsePort(value) {
  if (!/^[0-9]+$/.test(value ?? '')) throw new Error('port must be an integer from 1 through 65535');
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed < 1 || parsed > 65535) {
    throw new Error('port must be an integer from 1 through 65535');
  }
  return parsed;
}

function parseHost(value) {
  if (!value || value.length > 253 || /[\s\0/?#\\]/.test(value)) {
    throw new Error('host must be an IP address or DNS host name');
  }
  if (isIP(value)) return value;
  const labels = value.split('.');
  if (labels.some((label) => !/^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?$/.test(label))) {
    throw new Error('host must be an IP address or DNS host name');
  }
  return value;
}

function takeOption(args, index, name) {
  const current = args[index];
  const prefix = `${name}=`;
  if (current.startsWith(prefix)) {
    const value = current.slice(prefix.length);
    if (!value) throw new Error(`${name} requires a value`);
    return { value, next: index + 1 };
  }
  if (current === name) {
    if (index + 1 >= args.length || args[index + 1].startsWith('--')) {
      throw new Error(`${name} requires a value`);
    }
    return { value: args[index + 1], next: index + 2 };
  }
  return null;
}

function parseArgs(args) {
  const options = { port: 8091, host: '127.0.0.1', cert: null, key: null };
  const seen = new Set();
  let positionalPort = null;

  for (let index = 0; index < args.length;) {
    const arg = args[index];
    if (arg === '--help' || arg === '-h') return { help: true };
    if (!arg.startsWith('--')) {
      if (positionalPort !== null || seen.has('--port')) throw new Error('only one port may be specified');
      positionalPort = parsePort(arg);
      index += 1;
      continue;
    }

    let match = null;
    for (const name of ['--port', '--host', '--cert', '--key']) {
      match = takeOption(args, index, name);
      if (!match) continue;
      if (seen.has(name)) throw new Error(`${name} may be specified only once`);
      if (name === '--port' && positionalPort !== null) throw new Error('only one port may be specified');
      seen.add(name);
      if (name === '--port') options.port = parsePort(match.value);
      if (name === '--host') options.host = parseHost(match.value);
      if (name === '--cert') options.cert = match.value;
      if (name === '--key') options.key = match.value;
      index = match.next;
      break;
    }
    if (!match) throw new Error(`unknown option: ${arg}`);
  }

  if (positionalPort !== null) options.port = positionalPort;
  if ((options.cert === null) !== (options.key === null)) {
    throw new Error('--cert and --key must be supplied together');
  }
  return options;
}

function isWithinRoot(candidate) {
  const rel = relative(root, candidate);
  return rel === '' || (!rel.startsWith(`..${sep}`) && rel !== '..' && !isAbsolute(rel));
}

async function validatedPrivateFile(input, label) {
  const candidate = resolve(input);
  if (!isWithinRoot(candidate)) throw new Error(`${label} path must be inside the repository`);
  const canonical = await realpath(candidate).catch(() => null);
  if (!canonical || !isWithinRoot(canonical)) throw new Error(`${label} path is missing or escapes the repository`);
  const details = await stat(canonical);
  if (!details.isFile()) throw new Error(`${label} path must name a regular file`);
  await access(canonical, fsConstants.R_OK);
  return canonical;
}

function requestPath(rawTarget) {
  if (typeof rawTarget !== 'string' || !rawTarget.startsWith('/')) {
    return { error: 400 };
  }
  const rawPath = rawTarget.split('?', 1)[0].split('#', 1)[0];
  if (/%2f|%5c/i.test(rawPath)) return { error: 403 };

  let decoded;
  try {
    decoded = decodeURIComponent(rawPath);
  } catch {
    return { error: 400 };
  }
  if (decoded.includes('\0') || decoded.includes('\\')) return { error: 403 };
  if (decoded.split('/').some((part) => part === '..')) return { error: 403 };
  if (decoded === '/') decoded = '/web/index.html';

  const candidate = resolve(root, `.${decoded}`);
  if (!isWithinRoot(candidate)) return { error: 403 };
  return { decoded, candidate };
}

function responseHeaders(contentType, cacheControl) {
  return {
    ...(contentType ? { 'Content-Type': contentType } : {}),
    ...SECURITY_HEADERS,
    'Cache-Control': cacheControl,
  };
}

function sendError(res, status, method) {
  const messages = { 400: 'bad request', 403: 'forbidden', 404: 'not found', 405: 'method not allowed' };
  const body = method === 'HEAD' ? '' : (messages[status] ?? 'server error');
  const headers = responseHeaders('text/plain; charset=utf-8', 'no-store');
  if (status === 405) headers.Allow = 'GET, HEAD';
  res.writeHead(status, headers);
  res.end(body);
}

function makeHandler(transport) {
  return async (req, res) => {
    const method = req.method ?? '';
    if (method !== 'GET' && method !== 'HEAD') {
      sendError(res, 405, method);
      return;
    }

    if (req.url === '/_health' || req.url?.startsWith('/_health?')) {
      const body = Buffer.from(JSON.stringify({ status: 'ok', transport }));
      res.writeHead(200, responseHeaders(MIME['.json'], 'no-store'));
      res.end(method === 'HEAD' ? undefined : body);
      return;
    }

    const requested = requestPath(req.url);
    if (requested.error) {
      sendError(res, requested.error, method);
      return;
    }

    try {
      const canonical = await realpath(requested.candidate);
      if (!isWithinRoot(canonical)) {
        sendError(res, 403, method);
        return;
      }
      const details = await stat(canonical);
      if (!details.isFile()) {
        sendError(res, 404, method);
        return;
      }
      const body = await readFile(canonical);
      const cache = requested.decoded === '/web/service-worker.js' ? 'no-cache' : 'no-store';
      res.writeHead(200, responseHeaders(MIME[extname(canonical).toLowerCase()] ?? 'application/octet-stream', cache));
      res.end(method === 'HEAD' ? undefined : body);
    } catch {
      sendError(res, 404, method);
    }
  };
}

let options;
try {
  options = parseArgs(process.argv.slice(2));
} catch (error) {
  failCli(error.message);
}

if (options?.help) {
  console.log(usage());
} else if (options) {
  try {
    let server;
    let transport = 'http';
    if (options.cert !== null) {
      const certPath = await validatedPrivateFile(options.cert, 'certificate');
      const keyPath = await validatedPrivateFile(options.key, 'private key');
      const [cert, key] = await Promise.all([readFile(certPath), readFile(keyPath)]);
      transport = 'https';
      server = createHttpsServer({ cert, key }, makeHandler(transport));
    } else {
      server = createHttpServer(makeHandler(transport));
    }

    server.on('error', (error) => {
      console.error(`server startup failed: ${error.code ?? error.message}`);
      process.exitCode = 1;
    });
    server.listen(options.port, options.host, () => {
      const displayHost = options.host.includes(':') ? `[${options.host}]` : options.host;
      console.log(`SurrealEngine WebXR server ready: ${transport}://${displayHost}:${options.port}/`);
      console.log(`Health check: ${transport}://${displayHost}:${options.port}/_health`);
      console.log(`Isolation headers: COOP=same-origin, COEP=require-corp, CORP=same-origin`);
      if (options.host === '0.0.0.0' || options.host === '::') {
        console.warn('WARNING: LAN serving is enabled. A self-signed or untrusted certificate does NOT create a Quest trusted secure context.');
        console.warn('WARNING: use a hostname-matching certificate trusted by the Quest, and keep this development server behind a trusted LAN/firewall.');
      }
    });
  } catch (error) {
    failCli(error.message);
  }
}
