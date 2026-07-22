import { createHash, randomUUID } from "node:crypto";
import { access, cp, mkdir, readFile, readdir, rename, rm, stat, writeFile } from "node:fs/promises";
import { dirname, join, relative, resolve, sep } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const moduleDirectory = dirname(fileURLToPath(import.meta.url));
const defaultSourceRoot = resolve(moduleDirectory, "..");
const prohibitedExtensions = /\.(?:data|u|unr|utx|uax|umx|uz|uz2|exe)$/i;

const headers = `/*
  Cross-Origin-Opener-Policy: same-origin
  Cross-Origin-Embedder-Policy: require-corp
  Cross-Origin-Resource-Policy: same-origin
  X-Content-Type-Options: nosniff

/engine/*.wasm
  Content-Type: application/wasm
`;

const htaccess = `Options -Indexes
<IfModule mod_headers.c>
  Header always set Cross-Origin-Opener-Policy "same-origin"
  Header always set Cross-Origin-Embedder-Policy "require-corp"
  Header always set Cross-Origin-Resource-Policy "same-origin"
  Header always set X-Content-Type-Options "nosniff"
</IfModule>
<IfModule mod_mime.c>
  AddType application/wasm .wasm
</IfModule>
`;

const hostingReadme = `SurrealEngine browser release

This directory is intended to be hosted at /webxr/Ports/SurrealEngine/ or any
other HTTPS path without rewriting its relative URLs. Serve index.html as the
directory index and .wasm as application/wasm.

The engine uses WebAssembly threads. The host must send the COOP/COEP headers
listed in _headers or .htaccess. Those files cover common static hosts; configure
equivalent headers explicitly when the host ignores them. WebXR additionally
requires HTTPS (localhost is sufficient for development).

No game data is included. Users select their own UT99 or Unreal Gold folder and
the launcher copies validated files into origin-private OPFS/IndexedDB storage.
Do not add .data files or UE1 packages to this directory.

The WebAssembly binary statically includes the SurrealVideo Indeo 5 decoder,
which is licensed under LGPL 2.1 or later. Its license and project notice are in
licenses/. A release publisher must also make the exact corresponding source
and relinkable build materials available under the LGPL; see the project
documentation before redistribution.
`;

function mimeType(path) {
	if (path.endsWith(".html")) return "text/html; charset=utf-8";
	if (path.endsWith(".css")) return "text/css; charset=utf-8";
	if (path.endsWith(".js") || path.endsWith(".mjs")) return "text/javascript; charset=utf-8";
	if (path.endsWith(".json")) return "application/json; charset=utf-8";
	if (path.endsWith(".wasm")) return "application/wasm";
	return "text/plain; charset=utf-8";
}

async function exists(path) {
	try { await access(path); return true; } catch (_) { return false; }
}

async function filesBelow(root) {
	const result = [];
	async function visit(directory) {
		for (const entry of await readdir(directory, { withFileTypes: true })) {
			const path = join(directory, entry.name);
			if (entry.isDirectory()) await visit(path);
			else if (entry.isFile()) result.push(path);
		}
	}
	await visit(root);
	return result;
}

function portablePath(root, path) {
	return relative(root, path).split(sep).join("/");
}

async function validateNoDataBuild(engineDirectory) {
	const javascript = join(engineDirectory, "SurrealEngine.js");
	const wasm = join(engineDirectory, "SurrealEngine.wasm");
	const cache = join(engineDirectory, "CMakeCache.txt");
	for (const required of [javascript, wasm, cache]) {
		if (!await exists(required)) throw new Error("Missing no-data build artifact: " + required);
	}
	const cacheText = await readFile(cache, "utf8");
	const gameData = cacheText.match(/^SURREAL_GAMEDATA_DIR:[^=]*=(.*)$/m);
	if (!gameData || gameData[1].trim()) {
		throw new Error("The Emscripten build is not auditable as data-free: SURREAL_GAMEDATA_DIR must be present and empty.");
	}
	for (const path of await filesBelow(engineDirectory)) {
		if (portablePath(engineDirectory, path).toLowerCase().endsWith(".data"))
			throw new Error("Refusing to package an Emscripten .data payload: " + path);
	}
	return Object.freeze({ javascript, wasm });
}

async function auditRelease(root) {
	const violations = [];
	for (const path of await filesBelow(root)) {
		const name = portablePath(root, path);
		if (prohibitedExtensions.test(name)) violations.push(name);
	}
	if (violations.length) throw new Error("Commercial or user game data is forbidden in the release: " + violations.join(", "));
	return true;
}

async function releaseManifest(root, config) {
	const files = [];
	for (const path of await filesBelow(root)) {
		const name = portablePath(root, path);
		if (name === "release-manifest.json") continue;
		const contents = await readFile(path);
		files.push({
			path: name,
			bytes: contents.byteLength,
			sha256: createHash("sha256").update(contents).digest("hex"),
			expectedMime: mimeType(name),
		});
	}
	files.sort((left, right) => left.path.localeCompare(right.path));
	return {
		schema: "surrealengine-browser-release-v1",
		version: 1,
		intendedBasePath: config.intendedBasePath,
		dependencies: config.dependencies,
		games: ["ut99", "unreal-gold"],
		presentations: ["flat", "webxr"],
		files,
	};
}

async function packageRelease(options) {
	const sourceRoot = resolve(options && options.sourceRoot || defaultSourceRoot);
	const engineDirectory = resolve(options && options.engineDirectory || join(sourceRoot, "build-emscripten"));
	const outputDirectory = resolve(options && options.outputDirectory || join(sourceRoot, "dist", "webxr", "Ports", "SurrealEngine"));
	if (await exists(outputDirectory)) throw new Error("Output directory already exists; choose an empty release destination: " + outputDirectory);
	const config = JSON.parse(await readFile(join(sourceRoot, "web", "release-package.json"), "utf8"));
	if (config.schema !== "surrealengine-browser-release-config-v1") throw new Error("Unsupported browser release configuration.");
	const engine = await validateNoDataBuild(engineDirectory);
	await mkdir(dirname(outputDirectory), { recursive: true });
	const staging = outputDirectory + ".staging-" + randomUUID();
	try {
		await mkdir(join(staging, "engine"), { recursive: true });
		for (const asset of config.webAssets) await cp(join(sourceRoot, "web", asset), join(staging, asset));
		let index = await readFile(join(sourceRoot, "web", "surreal_app.html"), "utf8");
		const developmentBase = 'data-engine-base="../build-emscripten/"';
		if (!index.includes(developmentBase)) throw new Error("The shared launcher is missing its packageable engine-base marker.");
		index = index.replace(developmentBase, 'data-engine-base="./engine/"');
		await writeFile(join(staging, "index.html"), index);
		await cp(engine.javascript, join(staging, "engine", "SurrealEngine.js"));
		await cp(engine.wasm, join(staging, "engine", "SurrealEngine.wasm"));
		await mkdir(join(staging, "licenses"), { recursive: true });
		await cp(join(sourceRoot, "SurrealVideo", "COPYING.LGPLv2.1"), join(staging, "licenses", "SurrealVideo-LGPL-2.1.txt"));
		await cp(join(sourceRoot, "SurrealVideo", "README.md"), join(staging, "licenses", "SurrealVideo-README.md"));
		await writeFile(join(staging, "_headers"), headers);
		await writeFile(join(staging, ".htaccess"), htaccess);
		await writeFile(join(staging, "HOSTING.txt"), hostingReadme);
		await auditRelease(staging);
		const manifest = await releaseManifest(staging, config);
		await writeFile(join(staging, "release-manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
		await rename(staging, outputDirectory);
		return Object.freeze({ outputDirectory, manifest });
	} catch (error) {
		await rm(staging, { recursive: true, force: true });
		throw error;
	}
}

function commandLine(argumentsList) {
	const options = {};
	for (let index = 0; index < argumentsList.length; index++) {
		const name = argumentsList[index];
		const value = argumentsList[index + 1];
		if (name === "--source-root") options.sourceRoot = value;
		else if (name === "--engine-dir") options.engineDirectory = value;
		else if (name === "--output") options.outputDirectory = value;
		else throw new Error("Unknown or incomplete argument: " + name);
		index++;
	}
	return options;
}

export { auditRelease, packageRelease, releaseManifest, validateNoDataBuild };

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
	packageRelease(commandLine(process.argv.slice(2))).then(result => {
		process.stdout.write("Packaged " + result.manifest.files.length + " data-free files at " + result.outputDirectory + "\n");
	}).catch(error => {
		process.stderr.write((error && error.stack ? error.stack : String(error)) + "\n");
		process.exitCode = 1;
	});
}
