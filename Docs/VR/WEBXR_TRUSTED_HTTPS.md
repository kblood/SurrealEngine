# Trusted HTTPS for WebXR development

Status: development tooling implemented; physical Quest trust and native WebGPU/WebXR presentation remain qualification gates.

WebXR requires a secure context. `http://localhost` is treated specially by browsers on the same machine, but `http://192.168.x.x` is not. When a Quest or another LAN device opens the page directly, use HTTPS with a certificate that the browser trusts and whose subject-alt-name matches the URL. Merely enabling TLS or clicking through a certificate warning does not prove that WebXR sees a trusted secure context.

## Server interface

The existing command remains supported and binds only to the loopback interface:

```powershell
node web/serve.mjs 8091
```

Equivalent explicit HTTP form:

```powershell
node web/serve.mjs --port 8091 --host 127.0.0.1
```

HTTPS is enabled only when both files are supplied:

```powershell
node web/serve.mjs `
  --port 8443 `
  --host 0.0.0.0 `
  --cert .local-webxr-tls/dev-machine.pem `
  --key .local-webxr-tls/dev-machine-key.pem
```

The certificate and key must resolve to regular files inside this repository. This is a deliberate boundary: the development server cannot be pointed at arbitrary private files elsewhere on the host. Symlinks that escape the repository are rejected. LAN binding is opt-in via `--host 0.0.0.0` (or `::`) and prints a prominent trust warning.

The readiness endpoint is:

```text
https://HOST:8443/_health
```

It returns only a status and transport mode; it does not disclose repository or certificate paths. Static responses retain the required headers:

- `Cross-Origin-Opener-Policy: same-origin`
- `Cross-Origin-Embedder-Policy: require-corp`
- `Cross-Origin-Resource-Policy: same-origin`
- `Cache-Control: no-cache` for `web/service-worker.js`
- `Cache-Control: no-store` for other content

Only `GET` and `HEAD` are accepted. Encoded traversal, path-separator smuggling, malformed encodings, and symlink escapes are refused.

## Local trusted-CA workflow

Do not generate or commit a private key in a tracked location. Create a local directory and exclude it in this clone before generating anything:

```powershell
New-Item -ItemType Directory -Force .local-webxr-tls | Out-Null
Add-Content .git/info/exclude "/.local-webxr-tls/"
```

One convenient development option is [mkcert](https://github.com/FiloSottile/mkcert). It creates a local CA and installs that CA into supported host trust stores:

```powershell
mkcert -install
mkcert `
  -cert-file .local-webxr-tls/dev-machine.pem `
  -key-file .local-webxr-tls/dev-machine-key.pem `
  localhost 127.0.0.1 ::1 dev-machine.local 192.168.50.8
```

Replace the machine name and IP with stable values for the development host. Open exactly a name or address included in the certificate, for example:

```text
https://dev-machine.local:8443/web/index_webxr.html?build=build-emscripten&native-webgpu-xr=1
```

`mkcert -install` trusts its CA on the machine where it runs. It does **not** automatically trust that CA on a Quest. Keep the generated root CA and private keys out of Git and do not distribute the root private key. If testing in Windows Brave streamed through Virtual Desktop, Windows/Brave is the relying browser and must trust the CA. If opening the URL in Quest Browser, the Quest browser/OS must trust the issuing CA; importing a user CA may depend on device policy and browser behavior and must be verified on the actual headset.

Required trust checks on the actual browser/device:

1. Open the HTTPS URL without bypassing a certificate interstitial.
2. Confirm the certificate name matches the URL and the chain is trusted.
3. In the page console, confirm `window.isSecureContext === true`.
4. Confirm the WebXR readiness panel reports secure context and `immersive-vr` support.
5. Start a physical session and record the native presentation/frame results. API presence alone is not qualification.

Deleting `.local-webxr-tls` removes the leaf certificate and key from the clone, but it does not remove the mkcert CA from an OS trust store. Use `mkcert -uninstall` when that local CA is no longer needed.

## Production and remote-device boundary

`web/serve.mjs` is a development server, not a production origin. It has no authentication, rate limiting, access log policy, certificate rotation, or hardened request/resource controls beyond its narrow static-file boundary. `--host 0.0.0.0` exposes the repository's served files to reachable LAN clients; use a trusted private network and firewall rules.

For repeatable headset testing and deployment, prefer a dedicated HTTPS origin behind a maintained web server or reverse proxy with:

- a stable DNS name and a certificate chaining to a CA already trusted by the target browser/device;
- only staged release artifacts, rather than the repository root;
- the same COOP, COEP, CORP, MIME, and service-worker cache headers;
- an explicit access policy, certificate renewal, and deployment rollback process.

No current local certificate test proves that Quest Browser, Brave through Virtual Desktop, VDXR, or a compositor accepts the native WebGPU/WebXR path. Those remain physical-device release gates.

## Automated verification

The isolated server suite exercises backward-compatible HTTP, explicit HTTP, ephemeral HTTPS (when OpenSSL is installed), MIME/cache/isolation headers, health output, CLI validation, traversal, methods, and symlink escape handling:

```powershell
python -B -m unittest -v web/test_serve_https.py
```

Its one-day self-signed certificate exists only inside a temporary repository directory and is deleted after the test. The HTTPS client deliberately disables trust verification for transport testing; this does not constitute browser or Quest trust qualification.
