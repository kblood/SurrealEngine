import { randomUUID } from "node:crypto";
import { access, cp, mkdir, readFile, readdir, rename, rm, stat, writeFile } from "node:fs/promises";
import { dirname, join, relative, resolve, sep } from "node:path";
import { promisify } from "node:util";
import { fileURLToPath, pathToFileURL } from "node:url";
import { brotliCompress, brotliDecompress, constants as zlibConstants, gzip, gunzip } from "node:zlib";
import { sha256File, sourceIdentity, trackedSurrealVideoFiles } from "./package_corresponding_source.mjs";

const moduleDirectory = dirname(fileURLToPath(import.meta.url));
const defaultSourceRoot = resolve(moduleDirectory, "..");
const prohibitedExtensions = /\.(?:data|u|unr|utx|uax|umx|uz|uz2|exe)$/i;
const hashPattern = "[0-9a-f]{64}";
const immutablePathPattern = new RegExp(`^(?:assets|engine)\\/[^/]+\\.${hashPattern}\\.(?:css|js|wasm)$`);
const compressiblePathPattern = /\.(?:css|js|wasm)$/;
const compressBrotli = promisify(brotliCompress);
const decompressBrotli = promisify(brotliDecompress);
const compressGzip = promisify(gzip);
const decompressGzip = promisify(gunzip);

const headers = `/*
  Cross-Origin-Opener-Policy: same-origin
  Cross-Origin-Embedder-Policy: require-corp
  Cross-Origin-Resource-Policy: same-origin
  X-Content-Type-Options: nosniff
  Cache-Control: no-cache, must-revalidate
  Vary: Accept-Encoding

/assets/*
  Cache-Control: public, max-age=31536000, immutable

/engine/*
  Cache-Control: public, max-age=31536000, immutable

/engine/*.wasm
  Content-Type: application/wasm
`;

const htaccess = `<IfModule mod_rewrite.c>
  RewriteEngine On
  RewriteCond %{HTTP:Accept-Encoding} br [NC]
  RewriteCond %{REQUEST_FILENAME}.br -f
  RewriteRule ^(.+\\.(?:css|js|wasm))$ $1.br [L]
  RewriteCond %{HTTP:Accept-Encoding} gzip [NC]
  RewriteCond %{REQUEST_FILENAME}.gz -f
  RewriteRule ^(.+\\.(?:css|js|wasm))$ $1.gz [L]
</IfModule>
<IfModule mod_headers.c>
  Header always set Cross-Origin-Opener-Policy "same-origin"
  Header always set Cross-Origin-Embedder-Policy "require-corp"
  Header always set Cross-Origin-Resource-Policy "same-origin"
  Header always set X-Content-Type-Options "nosniff"
  Header always set Cache-Control "no-cache, must-revalidate"
  Header always merge Vary "Accept-Encoding"
  <FilesMatch "\\.${hashPattern}\\.(css|js|wasm)(\\.(br|gz))?$">
    Header always set Cache-Control "public, max-age=31536000, immutable"
  </FilesMatch>
  <FilesMatch "\\.${hashPattern}\\.(css|js|wasm)\\.br$">
    Header always set Content-Encoding "br"
  </FilesMatch>
  <FilesMatch "\\.${hashPattern}\\.(css|js|wasm)\\.gz$">
    Header always set Content-Encoding "gzip"
  </FilesMatch>
</IfModule>
<IfModule mod_mime.c>
  AddType application/wasm .wasm
</IfModule>
`;

function validateIntendedBasePath(value) {
	if (typeof value !== "string" || !value.startsWith("/") || !value.endsWith("/") ||
		value.includes("\\") || value.includes("?") || value.includes("#"))
		throw new Error("Intended base path must be an absolute URL path with leading and trailing slashes.");
	const segments = value === "/" ? [] : value.slice(1, -1).split("/");
	if (segments.some(segment => !segment || segment === "." || segment === ".." ||
		!/^[A-Za-z0-9._~-]+$/.test(segment)))
		throw new Error("Intended base path contains an unsafe or unsupported path segment.");
	return value;
}

function hostingReadme(compliance, intendedBasePath) { return `SurrealEngine browser release

Intended base path: ${intendedBasePath}

This directory can also be hosted at another HTTPS path without rewriting its
relative URLs. Serve index.html as the directory index and .wasm as
application/wasm.

Every executable web asset has a full SHA-256 in its filename. Keep index.html
and release-manifest.json revalidated, but serve assets/ and engine/ with
Cache-Control: public, max-age=31536000, immutable. Precompressed Brotli and gzip
sidecars are included. The server must negotiate them from Accept-Encoding,
retain the original Content-Type, set Content-Encoding, and send
Vary: Accept-Encoding. The bundled Apache configuration implements this policy.

This package records ${compliance.buildProvenance.threads ? "a threaded" : "a single-threaded"}
${compliance.buildProvenance.releaseProfile} build. The host sends the COOP/COEP
headers listed in _headers or .htaccess so isolation is explicit and future
threaded variants remain eligible. Configure equivalent headers when the host
ignores those files. WebXR additionally requires HTTPS (localhost is sufficient
for development).

No game data is included. Users select their own UT99 or Unreal Gold folder and
the launcher copies validated files into origin-private OPFS/IndexedDB storage.
Do not add .data files or UE1 packages to this directory.

The WebAssembly binary statically includes the SurrealVideo Indeo 5 decoder,
which is licensed under LGPL 2.1 or later. Its license, notice, corresponding
source location, and whole-program rebuild instructions are included here.

Corresponding source: ${compliance.sourceUrl}
Source archive SHA-256: ${compliance.archiveSha256}
Source commit: ${compliance.sourceCommit}

See SOURCE-OFFER.txt, source-compliance.json, and licenses/SurrealVideo-Relinking.md.
This packaging mechanism still requires human review for the release's actual
hosting duration, terms, notices, and jurisdiction.
`; }

function mimeType(path) {
	if (path.endsWith(".br")) path = path.slice(0, -3);
	else if (path.endsWith(".gz") && !path.endsWith(".tar.gz")) path = path.slice(0, -3);
	if (path.endsWith(".html")) return "text/html; charset=utf-8";
	if (path.endsWith(".css")) return "text/css; charset=utf-8";
	if (path.endsWith(".js") || path.endsWith(".mjs")) return "text/javascript; charset=utf-8";
	if (path.endsWith(".json")) return "application/json; charset=utf-8";
	if (path.endsWith(".wasm")) return "application/wasm";
	if (path.endsWith(".tar.gz")) return "application/gzip";
	return "text/plain; charset=utf-8";
}

function representation(path) {
	if (path.endsWith(".br")) return { contentEncoding: "br", sourcePath: path.slice(0, -3) };
	if (path.endsWith(".gz") && !path.endsWith(".tar.gz"))
		return { contentEncoding: "gzip", sourcePath: path.slice(0, -3) };
	return null;
}

function buildManifestInfo(provenance) {
	return {
		profile: provenance.releaseProfile,
		buildType: provenance.buildType,
		emscriptenVersion: provenance.emscriptenVersion,
		emscriptenRevision: provenance.emscriptenRevision,
		assertions: provenance.assertions,
		stackOverflowCheck: provenance.stackOverflowCheck,
		threads: provenance.threads,
		pthreadPoolSize: provenance.pthreadPoolSize,
		browserEntryPoint: provenance.browserEntryPoint,
		wasmfsOpfs: provenance.wasmfsOpfs,
		wasmfsOpfsAsyncify: provenance.wasmfsOpfsAsyncify,
		proxyToPthread: provenance.proxyToPthread,
		webgl2Renderer: provenance.webgl2Renderer,
	};
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

function revisionWebAssetReferences(index, revisions) {
	let revised = index;
	for (const [asset, packagedPath] of revisions) {
		const reference = `"${asset}"`;
		if (!revised.includes(reference)) throw new Error("Shared launcher does not reference configured web asset: " + asset);
		revised = revised.replaceAll(reference, `"${packagedPath}"`);
	}
	return revised;
}

function contentAddressedPath(directory, name, sha256) {
	if (!/^\p{ASCII_Hex_Digit}{64}$/u.test(sha256 || "")) throw new Error("Content hash is not SHA-256.");
	if (!/^[A-Za-z0-9_-]+\.[A-Za-z0-9]+$/.test(name)) throw new Error("Release asset name is not portable: " + name);
	const dot = name.lastIndexOf(".");
	return `${directory}/${name.slice(0, dot)}.${sha256.toLowerCase()}${name.slice(dot)}`;
}

async function createCompressedVariants(root, paths) {
	for (const portable of paths) {
		if (!compressiblePathPattern.test(portable)) continue;
		const source = await readFile(join(root, ...portable.split("/")));
		const brotli = await compressBrotli(source, {
			params: { [zlibConstants.BROTLI_PARAM_QUALITY]: 11 },
		});
		const gzipped = await compressGzip(source, { level: 9, mtime: 0 });
		await writeFile(join(root, ...`${portable}.br`.split("/")), brotli);
		await writeFile(join(root, ...`${portable}.gz`.split("/")), gzipped);
	}
}

async function validateNoDataBuild(engineDirectory) {
	const javascript = join(engineDirectory, "SurrealEngine.js");
	const wasm = join(engineDirectory, "SurrealEngine.wasm");
	const cache = join(engineDirectory, "CMakeCache.txt");
	const provenancePath = join(engineDirectory, "build-compliance-provenance.json");
	for (const required of [javascript, wasm, cache, provenancePath]) {
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
	let provenance;
	try { provenance = JSON.parse(await readFile(provenancePath, "utf8")); }
	catch (_) { throw new Error("Browser build provenance is invalid JSON."); }
	if (provenance.schema !== "surrealengine-browser-build-provenance-v2" ||
		!/^\p{ASCII_Hex_Digit}{40}$/u.test(provenance.sourceCommit || "") ||
		!/^\p{ASCII_Hex_Digit}{40}$/u.test(provenance.sourceTree || "") ||
		provenance.sourceDirty !== false || provenance.toolchain !== "Emscripten" ||
		!/^\d+\.\d+\.\d+$/.test(provenance.emscriptenVersion || "") ||
		!/^[0-9a-f]{7,40}$/.test(provenance.emscriptenRevision || "") ||
		!(["call-main", "asyncify-opfs"].includes(provenance.browserEntryPoint)) ||
		!(["diagnostic", "production"].includes(provenance.releaseProfile)) ||
		typeof provenance.buildType !== "string" ||
		provenance.assertions !== (provenance.releaseProfile === "production" ? 0 : 2) ||
		provenance.stackOverflowCheck !== (provenance.releaseProfile === "production" ? 0 : 2) ||
		(provenance.releaseProfile === "production" && provenance.buildType !== "Release") ||
		provenance.initialMemoryBytes !== 268435456 || provenance.allowMemoryGrowth !== true ||
		typeof provenance.threads !== "boolean" ||
		provenance.pthreadPoolSize !== (provenance.threads ? 2 : 0) ||
		typeof provenance.wasmfsOpfs !== "boolean" || typeof provenance.wasmfsOpfsAsyncify !== "boolean" ||
		typeof provenance.proxyToPthread !== "boolean" ||
		typeof provenance.webgl2Renderer !== "boolean" ||
		(provenance.browserEntryPoint === "asyncify-opfs") !== provenance.wasmfsOpfsAsyncify ||
		(provenance.wasmfsOpfsAsyncify && provenance.threads) ||
		(provenance.proxyToPthread && !provenance.threads) ||
		provenance.surrealVideoLinkage !== "static-wasm")
		throw new Error("Browser build provenance is incomplete or records a dirty/non-static build.");
	return Object.freeze({ javascript, wasm, provenance });
}

async function validateCorrespondingSource(sourceRoot, metadataPath, allowDirtySourceTree = false) {
	if (!metadataPath) throw new Error("A generated corresponding-source metadata file is required.");
	let metadata;
	try { metadata = JSON.parse(await readFile(resolve(metadataPath), "utf8")); }
	catch (_) { throw new Error("Corresponding-source metadata is missing or invalid JSON."); }
	if (metadata.schema !== "surrealengine-corresponding-source-v1" || metadata.version !== 1 ||
		typeof metadata.archiveFile !== "string" || !/^[A-Za-z0-9._-]+\.tar\.gz$/.test(metadata.archiveFile) ||
		!/^\p{ASCII_Hex_Digit}{64}$/u.test(metadata.archiveSha256 || "") ||
		!Array.isArray(metadata.surrealVideoFiles) || !metadata.surrealVideoFiles.length ||
		!Number.isSafeInteger(metadata.trackedFileCount) || metadata.trackedFileCount < metadata.surrealVideoFiles.length ||
		metadata.archiveScope !== "complete-tracked-source-tree")
		throw new Error("Corresponding-source metadata has an unsupported or incomplete schema.");
	const archivePath = resolve(dirname(resolve(metadataPath)), metadata.archiveFile);
	const archiveInformation = await stat(archivePath);
	if (archiveInformation.size !== metadata.archiveBytes || await sha256File(archivePath) !== metadata.archiveSha256)
		throw new Error("Corresponding-source archive size or SHA-256 does not match its metadata.");
	const identity = await sourceIdentity(sourceRoot, false);
	if ((!allowDirtySourceTree && identity.sourceDirty) || identity.sourceCommit !== metadata.sourceCommit ||
		identity.sourceTree !== metadata.sourceTree)
		throw new Error("Corresponding source does not match the current clean source commit/tree.");
	const trackedVideo = await trackedSurrealVideoFiles(sourceRoot);
	const recordedVideo = metadata.surrealVideoFiles.map(record => record && record.path).sort();
	if (trackedVideo.length !== recordedVideo.length || trackedVideo.some((path, index) => path !== recordedVideo[index]))
		throw new Error("Corresponding-source inventory does not cover every tracked SurrealVideo file.");
	for (const record of metadata.surrealVideoFiles) {
		if (!record || typeof record.path !== "string" || !/^SurrealVideo\/[A-Za-z0-9_.\/-]+$/.test(record.path) ||
			record.path.split("/").includes("..") ||
			!Number.isSafeInteger(record.bytes) || !/^\p{ASCII_Hex_Digit}{64}$/u.test(record.sha256 || ""))
			throw new Error("Corresponding-source SurrealVideo inventory is invalid.");
		const path = resolve(sourceRoot, record.path);
		const information = await stat(path);
		if (information.size !== record.bytes || await sha256File(path) !== record.sha256)
			throw new Error("Corresponding-source inventory mismatch: " + record.path);
	}
	return Object.freeze({ archivePath, metadata });
}

async function auditRelease(root) {
	const violations = [];
	for (const path of await filesBelow(root)) {
		const name = portablePath(root, path);
		if (prohibitedExtensions.test(name)) violations.push(name);
	}
	if (violations.length) throw new Error("Commercial or user game data is forbidden in the release: " + violations.join(", "));
	const required = ["SOURCE-OFFER.txt", "source-compliance.json", "licenses/SurrealVideo-LGPL-2.1.txt",
		"licenses/SurrealVideo-README.md",
		"licenses/SurrealVideo-Relinking.md"];
	for (const name of required)
		if (!await exists(join(root, name))) throw new Error("Release is missing LGPL material: " + name);
	const compliance = JSON.parse(await readFile(join(root, "source-compliance.json"), "utf8"));
	if (compliance.schema !== "surrealengine-browser-source-compliance-v1" ||
		!/^\p{ASCII_Hex_Digit}{64}$/u.test(compliance.archiveSha256 || "") ||
		!/^\p{ASCII_Hex_Digit}{64}$/u.test(compliance.wasmSha256 || "") ||
		!/^\p{ASCII_Hex_Digit}{64}$/u.test(compliance.javascriptSha256 || "") ||
		!immutablePathPattern.test(compliance.wasmPath || "") ||
		!immutablePathPattern.test(compliance.javascriptPath || "") ||
		!compliance.wasmPath.endsWith(`.${compliance.wasmSha256}.wasm`) ||
		!compliance.javascriptPath.endsWith(`.${compliance.javascriptSha256}.js`))
		throw new Error("Release source-compliance metadata is invalid.");
	if (compliance.sourceUrl.startsWith("source/")) {
		const archive = join(root, ...compliance.sourceUrl.split("/"));
		if (!await exists(archive) || (await stat(archive)).size !== compliance.archiveBytes ||
			await sha256File(archive) !== compliance.archiveSha256)
			throw new Error("Bundled corresponding-source archive does not match source-compliance metadata.");
	} else if (new URL(compliance.sourceUrl).protocol !== "https:") {
		throw new Error("External corresponding-source location must use HTTPS.");
	}
	if (await sha256File(join(root, ...compliance.wasmPath.split("/"))) !== compliance.wasmSha256)
		throw new Error("Packaged WASM does not match source-compliance metadata.");
	if (await sha256File(join(root, ...compliance.javascriptPath.split("/"))) !== compliance.javascriptSha256)
		throw new Error("Packaged JavaScript does not match source-compliance metadata.");
	const index = await readFile(join(root, "index.html"), "utf8");
	const offer = await readFile(join(root, "SOURCE-OFFER.txt"), "utf8");
	if (!index.includes("data-source-compliance") || !index.includes(compliance.sourceUrl) ||
		!index.includes(`data-engine-script="./${compliance.javascriptPath}"`) ||
		!index.includes(`data-engine-wasm="./${compliance.wasmPath}"`) ||
		!offer.includes(compliance.sourceUrl) || !offer.includes(compliance.archiveSha256))
		throw new Error("Corresponding-source notice is not visible and internally consistent.");
	if (await exists(join(root, "release-manifest.json"))) await validateReleaseManifest(root, compliance);
	return true;
}

async function validateReleaseManifest(root, compliance) {
	let manifest;
	try { manifest = JSON.parse(await readFile(join(root, "release-manifest.json"), "utf8")); }
	catch (_) { throw new Error("Release manifest is missing or invalid JSON."); }
	if (manifest.schema !== "surrealengine-browser-release-v2" || manifest.version !== 2 ||
		!Array.isArray(manifest.files) || !manifest.files.length ||
		!manifest.entrypoints || manifest.entrypoints.html !== "index.html" ||
		manifest.entrypoints.javascript !== compliance.javascriptPath ||
		manifest.entrypoints.wasm !== compliance.wasmPath ||
		JSON.stringify(manifest.build) !== JSON.stringify(buildManifestInfo(compliance.buildProvenance)) ||
		manifest.buildId !== `${compliance.sourceCommit.slice(0, 12)}-${compliance.wasmSha256.slice(0, 16)}`)
		throw new Error("Release manifest has an unsupported or inconsistent identity.");
	const actual = (await filesBelow(root)).map(path => portablePath(root, path))
		.filter(name => name !== "release-manifest.json").sort();
	const recorded = manifest.files.map(record => record && record.path).sort();
	if (recorded.length !== new Set(recorded).size || actual.length !== recorded.length ||
		actual.some((name, index) => name !== recorded[index]))
		throw new Error("Release manifest inventory does not exactly match the package.");
	for (const record of manifest.files) {
		if (!record || typeof record.path !== "string" || record.path.startsWith("/") ||
			record.path.split("/").some(segment => !segment || segment === "." || segment === "..") ||
			!Number.isSafeInteger(record.bytes) || record.bytes < 0 ||
			!/^\p{ASCII_Hex_Digit}{64}$/u.test(record.sha256 || "") ||
			record.expectedMime !== mimeType(record.path))
			throw new Error("Release manifest contains an invalid file record.");
		const encoded = representation(record.path);
		const immutableSource = encoded ? encoded.sourcePath : record.path;
		const immutable = immutablePathPattern.test(immutableSource);
		if (record.cacheControl !== (immutable ? "public, max-age=31536000, immutable" : "no-cache, must-revalidate") ||
			(encoded && (record.contentEncoding !== encoded.contentEncoding || record.sourcePath !== encoded.sourcePath)) ||
			(!encoded && (record.contentEncoding !== undefined || record.sourcePath !== undefined)))
			throw new Error("Release manifest cache or representation metadata is invalid: " + record.path);
		const absolute = join(root, ...record.path.split("/"));
		const information = await stat(absolute);
		if (information.size !== record.bytes || await sha256File(absolute) !== record.sha256)
			throw new Error("Release manifest size or SHA-256 mismatch: " + record.path);
		if (encoded) {
			if (!immutablePathPattern.test(encoded.sourcePath))
				throw new Error("Compressed representation is not attached to an immutable asset: " + record.path);
			const compressed = await readFile(absolute);
			const expanded = encoded.contentEncoding === "br" ?
				await decompressBrotli(compressed) : await decompressGzip(compressed);
			const source = await readFile(join(root, ...encoded.sourcePath.split("/")));
			if (!expanded.equals(source)) throw new Error("Compressed representation does not expand to its source asset: " + record.path);
		}
	}
	return true;
}

async function releaseManifest(root, config, compliance) {
	const files = [];
	for (const path of await filesBelow(root)) {
		const name = portablePath(root, path);
		if (name === "release-manifest.json") continue;
		const information = await stat(path);
		const encoded = representation(name);
		const immutable = immutablePathPattern.test(encoded ? encoded.sourcePath : name);
		const record = {
			path: name,
			bytes: information.size,
			sha256: await sha256File(path),
			expectedMime: mimeType(name),
			cacheControl: immutable ? "public, max-age=31536000, immutable" : "no-cache, must-revalidate",
		};
		if (encoded) Object.assign(record, encoded);
		files.push(record);
	}
	files.sort((left, right) => left.path.localeCompare(right.path));
	return {
		schema: "surrealengine-browser-release-v2",
		version: 2,
		buildId: `${compliance.sourceCommit.slice(0, 12)}-${compliance.wasmSha256.slice(0, 16)}`,
		intendedBasePath: config.intendedBasePath,
		entrypoints: {
			html: "index.html",
			javascript: compliance.javascriptPath,
			wasm: compliance.wasmPath,
		},
		build: buildManifestInfo(compliance.buildProvenance),
		dependencies: config.dependencies,
		games: ["ut99", "unreal-gold"],
		presentations: ["flat", "webxr"],
		sourceCompliance: {
			sourceUrl: compliance.sourceUrl,
			archiveSha256: compliance.archiveSha256,
			sourceCommit: compliance.sourceCommit,
			sourceTree: compliance.sourceTree,
			wasmSha256: compliance.wasmSha256,
			javascriptSha256: compliance.javascriptSha256,
		},
		files,
	};
}

async function packageRelease(options) {
	const sourceRoot = resolve(options && options.sourceRoot || defaultSourceRoot);
	const engineDirectory = resolve(options && options.engineDirectory || join(sourceRoot, "build-emscripten"));
	const outputDirectory = resolve(options && options.outputDirectory || join(sourceRoot, "dist", "webxr", "Ports", "SurrealEngine"));
	if (await exists(outputDirectory)) throw new Error("Output directory already exists; choose an empty release destination: " + outputDirectory);
	const config = JSON.parse(await readFile(join(sourceRoot, "web", "release-package.json"), "utf8"));
	if (config.schema !== "surrealengine-browser-release-config-v2" || config.version !== 2)
		throw new Error("Unsupported browser release configuration.");
	const intendedBasePath = validateIntendedBasePath(options && Object.hasOwn(options, "intendedBasePath") ?
		options.intendedBasePath : config.intendedBasePath);
	const releaseConfig = { ...config, intendedBasePath };
	const engine = await validateNoDataBuild(engineDirectory);
	const corresponding = await validateCorrespondingSource(sourceRoot,
		options && options.correspondingSourceMetadata, options && options.allowDirtySourceTree === true);
	if (engine.provenance.sourceCommit !== corresponding.metadata.sourceCommit ||
		engine.provenance.sourceTree !== corresponding.metadata.sourceTree)
		throw new Error("WASM build provenance and corresponding-source archive identify different source trees.");
	await mkdir(dirname(outputDirectory), { recursive: true });
	const staging = outputDirectory + ".staging-" + randomUUID();
	try {
		await mkdir(join(staging, "engine"), { recursive: true });
		await mkdir(join(staging, "assets"), { recursive: true });
		const webAssetRevisions = [];
		const immutableAssets = [];
		for (const asset of config.webAssets) {
			const source = join(sourceRoot, "web", asset);
			const packagedPath = contentAddressedPath("assets", asset, await sha256File(source));
			await cp(source, join(staging, ...packagedPath.split("/")));
			webAssetRevisions.push([asset, packagedPath]);
			immutableAssets.push(packagedPath);
		}
		const wasmSha256 = await sha256File(engine.wasm);
		const javascriptSha256 = await sha256File(engine.javascript);
		const wasmPath = contentAddressedPath("engine", "SurrealEngine.wasm", wasmSha256);
		const javascriptPath = contentAddressedPath("engine", "SurrealEngine.js", javascriptSha256);
		let index = await readFile(join(sourceRoot, "web", "surreal_app.html"), "utf8");
		const developmentBase = 'data-engine-base="../build-emscripten/"';
		if (!index.includes(developmentBase)) throw new Error("The shared launcher is missing its packageable engine-base marker.");
		index = index.replace(developmentBase, `data-engine-base="./engine/" ` +
			`data-engine-script="./${javascriptPath}" data-engine-wasm="./${wasmPath}"`);
		index = revisionWebAssetReferences(index, webAssetRevisions);
		await cp(engine.javascript, join(staging, ...javascriptPath.split("/")));
		await cp(engine.wasm, join(staging, ...wasmPath.split("/")));
		immutableAssets.push(javascriptPath, wasmPath);
		let sourceUrl = options && options.sourceUrl ? String(options.sourceUrl) :
			"source/" + corresponding.metadata.archiveFile;
		if (options && options.sourceUrl) {
			const parsed = new URL(sourceUrl);
			if (parsed.protocol !== "https:") throw new Error("External corresponding-source URL must use HTTPS.");
			sourceUrl = parsed.href;
		} else {
			await mkdir(join(staging, "source"), { recursive: true });
			await cp(corresponding.archivePath, join(staging, "source", corresponding.metadata.archiveFile));
		}
		const compliance = {
			schema: "surrealengine-browser-source-compliance-v1",
			sourceUrl,
			archiveFile: corresponding.metadata.archiveFile,
			archiveBytes: corresponding.metadata.archiveBytes,
			archiveSha256: corresponding.metadata.archiveSha256,
			sourceCommit: corresponding.metadata.sourceCommit,
			sourceTree: corresponding.metadata.sourceTree,
			commitTimestamp: corresponding.metadata.commitTimestamp,
			repositoryUrl: corresponding.metadata.repositoryUrl,
			archiveScope: corresponding.metadata.archiveScope,
			buildInstructions: "licenses/SurrealVideo-Relinking.md",
			buildProvenance: engine.provenance,
			wasmPath,
			wasmSha256,
			javascriptPath,
			javascriptSha256,
			surrealVideoFiles: corresponding.metadata.surrealVideoFiles,
		};
		await writeFile(join(staging, "source-compliance.json"), JSON.stringify(compliance, null, 2) + "\n");
		await mkdir(join(staging, "licenses"), { recursive: true });
		await cp(join(sourceRoot, "SurrealVideo", "COPYING.LGPLv2.1"), join(staging, "licenses", "SurrealVideo-LGPL-2.1.txt"));
		await cp(join(sourceRoot, "SurrealVideo", "README.md"), join(staging, "licenses", "SurrealVideo-README.md"));
		await cp(join(sourceRoot, "Docs", "BROWSER_STATIC_RELINKING.md"), join(staging, "licenses", "SurrealVideo-Relinking.md"));
		const offer = `SurrealVideo corresponding source and relinking materials\n\n` +
			`This WebAssembly release uses code from the FFmpeg project through the bundled\n` +
			`SurrealVideo fork, licensed under LGPL version 2.1 or later.\n\n` +
			`Corresponding source: ${sourceUrl}\nSHA-256: ${compliance.archiveSha256}\n` +
			`Source commit: ${compliance.sourceCommit}\nSource tree: ${compliance.sourceTree}\n\n` +
			`See licenses/SurrealVideo-Relinking.md and source-compliance.json.\n`;
		await writeFile(join(staging, "SOURCE-OFFER.txt"), offer);
		index = index.replace("</body>", `<footer data-source-compliance><details>` +
			`<summary>Open-source licenses and corresponding source</summary>` +
			`<p>This build uses FFmpeg-derived SurrealVideo under LGPL 2.1 or later. ` +
			`<a href="${sourceUrl}">Download corresponding source and relinking materials</a> ` +
			`(SHA-256: <code>${compliance.archiveSha256}</code>).</p>` +
			`</details></footer>\n</body>`);
		await writeFile(join(staging, "index.html"), index);
		await writeFile(join(staging, "_headers"), headers);
		await writeFile(join(staging, ".htaccess"), htaccess);
		await writeFile(join(staging, "HOSTING.txt"), hostingReadme(compliance, intendedBasePath));
		await createCompressedVariants(staging, immutableAssets);
		await auditRelease(staging);
		const manifest = await releaseManifest(staging, releaseConfig, compliance);
		await writeFile(join(staging, "release-manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
		await auditRelease(staging);
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
		else if (name === "--corresponding-source") options.correspondingSourceMetadata = value;
		else if (name === "--source-url") options.sourceUrl = value;
		else if (name === "--intended-base-path") options.intendedBasePath = value;
		else throw new Error("Unknown or incomplete argument: " + name);
		index++;
	}
	return options;
}

export { auditRelease, packageRelease, releaseManifest, validateCorrespondingSource, validateIntendedBasePath, validateNoDataBuild };

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
	packageRelease(commandLine(process.argv.slice(2))).then(result => {
		process.stdout.write("Packaged " + result.manifest.files.length + " data-free files at " + result.outputDirectory + "\n");
	}).catch(error => {
		process.stderr.write((error && error.stack ? error.stack : String(error)) + "\n");
		process.exitCode = 1;
	});
}
