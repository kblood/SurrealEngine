import assert from "node:assert/strict";
import { mkdir, mkdtemp, readFile, readdir, rm, writeFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";
import { auditRelease, packageRelease, validateIntendedBasePath, validateNoDataBuild } from "./package_browser_release.mjs";
import { sha256File, sourceIdentity, trackedSurrealVideoFiles } from "./package_corresponding_source.mjs";

const temporaryRoot = await mkdtemp(join(tmpdir(), "surreal-browser-package-"));
const sourceRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
try {
	const engine = join(temporaryRoot, "engine-build");
	const output = join(temporaryRoot, "webxr", "Ports", "SurrealEngine");
	await mkdir(engine, { recursive: true });
	await writeFile(join(engine, "SurrealEngine.js"), "const wasm = 'SurrealEngine.wasm';\n");
	await writeFile(join(engine, "SurrealEngine.wasm"), new Uint8Array([0, 97, 115, 109]));
	await writeFile(join(engine, "CMakeCache.txt"), "SURREAL_GAMEDATA_DIR:PATH=\n");
	const identity = await sourceIdentity(sourceRoot);
	const provenance = {
		schema: "surrealengine-browser-build-provenance-v2",
		sourceCommit: identity.sourceCommit, sourceTree: identity.sourceTree,
		sourceDirty: false, toolchain: "Emscripten", emscriptenVersion: "6.0.2",
		emscriptenRevision: "7a2d97d627ff4945eae28847ce0387ac52b92c09",
		compilerId: "Clang", compilerVersion: "test",
		cmakeVersion: "test", buildType: "Release", releaseProfile: "diagnostic",
		assertions: 2, stackOverflowCheck: 2, initialMemoryBytes: 268435456, allowMemoryGrowth: true,
		threads: true, pthreadPoolSize: 2, wasmfsOpfs: false, wasmfsOpfsAsyncify: false,
		proxyToPthread: false, webgl2Renderer: false, browserEntryPoint: "call-main", surrealVideoLinkage: "static-wasm",
	};
	await writeFile(join(engine, "build-compliance-provenance.json"), JSON.stringify(provenance));
	const sourceArchive = join(temporaryRoot, "SurrealEngine-corresponding-source.tar.gz");
	await writeFile(sourceArchive, "synthetic deterministic archive");
	const sourceFiles = await trackedSurrealVideoFiles(sourceRoot);
	const surrealVideoFiles = [];
	for (const path of sourceFiles) {
		const absolute = join(sourceRoot, ...path.split("/"));
		const contents = await readFile(absolute);
		surrealVideoFiles.push({ path, bytes: contents.byteLength, sha256: await sha256File(absolute) });
	}
	const archiveContents = await readFile(sourceArchive);
	const sourceMetadata = {
		schema: "surrealengine-corresponding-source-v1", version: 1,
		archiveFile: "SurrealEngine-corresponding-source.tar.gz",
		archivePrefix: "SurrealEngine-source-test/", archiveBytes: archiveContents.byteLength,
		archiveSha256: await sha256File(sourceArchive), sourceCommit: identity.sourceCommit,
		sourceTree: identity.sourceTree, commitTimestamp: "2026-07-22T00:00:00Z",
		repositoryUrl: "https://github.com/dpjudas/SurrealEngine.git",
		buildInstructions: "Docs/BROWSER_STATIC_RELINKING.md",
		licenseFiles: ["SurrealVideo/COPYING.LGPLv2.1", "SurrealVideo/COPYING.LGPLv3", "SurrealVideo/README.md"],
		archiveScope: "complete-tracked-source-tree", trackedFileCount: 1000, surrealVideoFiles,
	};
	const sourceMetadataPath = sourceArchive + ".json";
	await writeFile(sourceMetadataPath, JSON.stringify(sourceMetadata));

	const result = await packageRelease({
		sourceRoot,
		engineDirectory: engine,
		outputDirectory: output,
		correspondingSourceMetadata: sourceMetadataPath,
		allowDirtySourceTree: true,
	});
	assert.equal(result.manifest.schema, "surrealengine-browser-release-v2");
	assert.equal(result.manifest.version, 2);
	assert.equal(result.manifest.build.profile, "diagnostic");
	assert.equal(result.manifest.build.assertions, 2);
	assert.equal(result.manifest.build.threads, true);
	assert.deepEqual(result.manifest.games, ["ut99", "unreal-gold"]);
	assert.deepEqual(result.manifest.presentations, ["flat", "webxr"]);
	assert.equal(result.manifest.intendedBasePath, "/webxr/Ports/SurrealEngine/");
	assert.equal(result.manifest.dependencies[0].sha, "6c614d56e66e6ea8882aada892b93bc6526e0a33");
	assert.equal(result.manifest.dependencies[1].sha, "d472068ad5894dc8cdddeefbb4f491bd118f2592");
	const wasmRecord = result.manifest.files.find(file => /^engine\/SurrealEngine\.[0-9a-f]{64}\.wasm$/.test(file.path));
	const javascriptRecord = result.manifest.files.find(file => /^engine\/SurrealEngine\.[0-9a-f]{64}\.js$/.test(file.path));
	assert.ok(wasmRecord && wasmRecord.expectedMime === "application/wasm");
	assert.ok(javascriptRecord && javascriptRecord.expectedMime === "text/javascript; charset=utf-8");
	assert.equal(result.manifest.entrypoints.wasm, wasmRecord.path);
	assert.equal(result.manifest.entrypoints.javascript, javascriptRecord.path);
	assert.equal(wasmRecord.cacheControl, "public, max-age=31536000, immutable");
	assert.ok(result.manifest.files.some(file => /^assets\/pointer_lock_gesture\.[0-9a-f]{64}\.js$/.test(file.path)));
	assert.ok(result.manifest.files.some(file => /^assets\/webxr_provider\.[0-9a-f]{64}\.js$/.test(file.path)));
	assert.ok(result.manifest.files.some(file => /^assets\/webxr_webgl_bridge\.[0-9a-f]{64}\.js$/.test(file.path)));
	assert.ok(result.manifest.files.some(file => /^assets\/webxr_diagnostics\.[0-9a-f]{64}\.js$/.test(file.path)));
	for (const source of [wasmRecord, javascriptRecord]) {
		for (const encoding of ["br", "gzip"]) {
			const suffix = encoding === "br" ? ".br" : ".gz";
			const encoded = result.manifest.files.find(file => file.path === source.path + suffix);
			assert.ok(encoded, `missing ${encoding} representation for ${source.path}`);
			assert.equal(encoded.contentEncoding, encoding);
			assert.equal(encoded.sourcePath, source.path);
			assert.equal(encoded.expectedMime, source.expectedMime);
			assert.ok(encoded.bytes > 0);
			if (source.bytes > 1024) assert.ok(encoded.bytes < source.bytes, "compressed production artifacts should be smaller");
		}
	}
	assert.ok(result.manifest.files.some(file => file.path === "licenses/SurrealVideo-LGPL-2.1.txt"));
	assert.ok(result.manifest.files.some(file => file.path === "licenses/SurrealVideo-README.md"));
	assert.ok(result.manifest.files.some(file => file.path === "licenses/SurrealVideo-Relinking.md"));
	assert.ok(result.manifest.files.some(file => file.path === "source/SurrealEngine-corresponding-source.tar.gz"));
	assert.ok(result.manifest.files.some(file => file.path === "source/SurrealEngine-corresponding-source.tar.gz" && file.expectedMime === "application/gzip"));
	assert.equal(result.manifest.sourceCompliance.archiveSha256, sourceMetadata.archiveSha256);
	assert.equal(result.manifest.sourceCompliance.sourceCommit, identity.sourceCommit);
	assert.equal(result.manifest.sourceCompliance.wasmSha256, wasmRecord.sha256);
	assert.equal(result.manifest.sourceCompliance.javascriptSha256, javascriptRecord.sha256);
	assert.equal(result.manifest.buildId, `${identity.sourceCommit.slice(0, 12)}-${wasmRecord.sha256.slice(0, 16)}`);
	const index = await readFile(join(output, "index.html"), "utf8");
	assert.match(index, /data-engine-base="\.\/engine\/"/);
	assert.match(index, new RegExp(`data-engine-script="\\.\\/${javascriptRecord.path.replaceAll(".", "\\.")}"`));
	assert.match(index, new RegExp(`data-engine-wasm="\\.\\/${wasmRecord.path.replaceAll(".", "\\.")}"`));
	assert.doesNotMatch(index, /\.\.\/build-emscripten\//);
	assert.doesNotMatch(index, /engine\/SurrealEngine\.(?:js|wasm)/);
	assert.match(index, /data-source-compliance/);
	assert.match(index, /<details><summary>Open-source licenses and corresponding source<\/summary>/);
	assert.match(index, /source\/SurrealEngine-corresponding-source\.tar\.gz/);
	assert.match(index, /browser may call folder selection an .upload./i);
	assert.match(index, /data-pointer-lock-control/);
	assert.match(index, /data-pointer-lock-capture/);
	assert.doesNotMatch(index, /data-launcher-presentation/,
		"production release still required a pre-launch presentation choice");
	assert.match(index, /data-xr-session-backend/);
	assert.match(index, />Direct WebGL 2 \(recommended\)</);
	assert.match(index, />Diagnostic WebGPU-to-WebGL bridge</);
	assert.match(index, /WebGL 2 \(VR-ready\)/);
	assert.match(index, /data-xr-session-bridge-blocking-timing/);
	assert.match(index, /data-xr-enter/);
	assert.match(index, /data-xr-exit/);
	assert.match(index, /Temporary QA: blocking bridge timing \(slower\)/);
	assert.match(index, /Press Escape to release it and send Escape to the game/);
	assert.doesNotMatch(index, /Folder upload fallback/);
	assert.match(index, /https:\/\/www\.oldunreal\.com\/downloads\/unreal\/full-game-installers\//);
	assert.match(index, /https:\/\/www\.oldunreal\.com\/downloads\/unrealtournament\/full-game-installers\//);
	assert.match(index, /https:\/\/www\.epicgames\.com\/unrealtournament\//);
	assert.match(index, /does not mirror or repackage them/);
	assert.ok(index.indexOf('id="game-data-importer"') < index.indexOf('id="browser-status"'),
		"game selection must appear before capability and diagnostic details");
	assert.match(index, /assets\/ut99_importer\.[0-9a-f]{64}\.js/);
	assert.match(index, /assets\/browser_app\.[0-9a-f]{64}\.js/);
	assert.match(index, /assets\/browser_app\.[0-9a-f]{64}\.css/);
	assert.doesNotMatch(index, /\?v=/);
	const stylePath = result.manifest.files.find(file => /^assets\/browser_app\.[0-9a-f]{64}\.css$/.test(file.path)).path;
	const packagedStyles = await readFile(join(output, ...stylePath.split("/")), "utf8");
	assert.match(packagedStyles, /body \{[^}]*display: flex;[^}]*flex-direction: column;/,
		"release content must use a non-overlapping vertical document flow");
	assert.match(packagedStyles, /footer\[data-source-compliance\] \{[^}]*position: static;/,
		"source compliance must remain in normal document flow");
	assert.match(packagedStyles, /\.pointer-lock-control\[hidden\] \{[^}]*display: none;/,
		"mouse capture prompt must be removable from the active game view");
	assert.doesNotMatch(packagedStyles,
		/footer\[data-source-compliance\] \{[^}]*(?:position:\s*(?:fixed|absolute)|z-index:)/,
		"source compliance must never overlay launcher content");
	assert.match(await readFile(join(output, "_headers"), "utf8"), /Cross-Origin-Embedder-Policy: require-corp/);
	assert.match(await readFile(join(output, "_headers"), "utf8"), /Cache-Control: no-cache, must-revalidate/);
	assert.match(await readFile(join(output, "_headers"), "utf8"), /Cache-Control: public, max-age=31536000, immutable/);
	assert.match(await readFile(join(output, "_headers"), "utf8"), /Vary: Accept-Encoding/);
	assert.match(await readFile(join(output, ".htaccess"), "utf8"), /Cache-Control "no-cache, must-revalidate"/);
	assert.match(await readFile(join(output, ".htaccess"), "utf8"), /RewriteCond %\{HTTP:Accept-Encoding\} br/);
	assert.match(await readFile(join(output, ".htaccess"), "utf8"), /max-age=31536000, immutable/);
	assert.match(await readFile(join(output, ".htaccess"), "utf8"), /Content-Encoding "gzip"/);
	assert.match(await readFile(join(output, ".htaccess"), "utf8"),
		/\\\.js\\\.\(br\|gz\)\$">[\s\S]*?ForceType text\/javascript/,
		"Apache sidecars must retain the JavaScript MIME type after rewrite");
	assert.match(await readFile(join(output, ".htaccess"), "utf8"),
		/\\\.wasm\\\.\(br\|gz\)\$">[\s\S]*?ForceType application\/wasm/,
		"Apache sidecars must retain the WebAssembly MIME type after rewrite");
	assert.match(await readFile(join(output, ".htaccess"), "utf8"),
		/AddType application\/gzip \.gz/,
		"Apache must serve the corresponding-source archive as application/gzip");
	assert.doesNotMatch(await readFile(join(output, ".htaccess"), "utf8"), /AddEncoding gzip \.gz/,
		"the corresponding-source tar.gz must not be mislabeled as HTTP content encoding");
	const pointerPath = result.manifest.files.find(file => /^assets\/pointer_lock_gesture\.[0-9a-f]{64}\.js$/.test(file.path)).path;
	const pointerLockHelper = await readFile(join(output, ...pointerPath.split("/")), "utf8");
	assert.match(pointerLockHelper, /SurrealBrowserPointerLock/);
	assert.match(pointerLockHelper, /Surreal_ForwardBrowserEscape/);
	assert.match(pointerLockHelper, /Surreal_ForwardBrowserMouseMotion/);
	assert.match(pointerLockHelper, /Surreal_ResetBrowserMouseMotion/);
	assert.match(pointerLockHelper, /Surreal_SetBrowserMouseMotionActive/);
	assert.match(pointerLockHelper, /relativeMotionBridgeReady/);
	assert.match(pointerLockHelper, /surrealXRNativeCallsBlocked/);
	const defaultHosting = await readFile(join(output, "HOSTING.txt"), "utf8");
	assert.match(defaultHosting, /Intended base path: \/webxr\/Ports\/SurrealEngine\//);
	assert.match(defaultHosting, /Source archive SHA-256/);
	assert.match(await readFile(join(output, "licenses", "SurrealVideo-LGPL-2.1.txt"), "utf8"), /GNU LESSER GENERAL PUBLIC LICENSE/);
	const compliance = JSON.parse(await readFile(join(output, "source-compliance.json"), "utf8"));
	assert.equal(compliance.wasmSha256, await sha256File(join(engine, "SurrealEngine.wasm")));
	assert.equal(compliance.wasmPath, wasmRecord.path);
	assert.equal(compliance.javascriptPath, javascriptRecord.path);
	assert.equal(compliance.buildProvenance.surrealVideoLinkage, "static-wasm");
	assert.equal(compliance.buildProvenance.browserEntryPoint, "call-main");
	assert.equal(compliance.buildProvenance.releaseProfile, "diagnostic");
	assert.equal(compliance.buildProvenance.assertions, 2);
	assert.equal(compliance.buildProvenance.threads, true);
	assert.match(await readFile(join(output, "SOURCE-OFFER.txt"), "utf8"), new RegExp(sourceMetadata.archiveSha256));
	await auditRelease(output);
	assert.equal((await readdir(join(output, "engine")))
		.some(name => name === "SurrealEngine.js" || name === "SurrealEngine.wasm"), false,
		"mutable legacy engine names must not be packaged");
	const extraFile = join(output, "unexpected.txt");
	await writeFile(extraFile, "not recorded");
	await assert.rejects(() => auditRelease(output), /inventory does not exactly match/);
	await rm(extraFile);
	const manifestPath = join(output, "release-manifest.json");
	const originalManifest = await readFile(manifestPath, "utf8");
	const mixedManifest = JSON.parse(originalManifest);
	mixedManifest.entrypoints.wasm = mixedManifest.entrypoints.javascript;
	await writeFile(manifestPath, JSON.stringify(mixedManifest));
	await assert.rejects(() => auditRelease(output), /inconsistent identity/);
	await writeFile(manifestPath, originalManifest);
	const originalEngineScript = await readFile(join(output, ...javascriptRecord.path.split("/")));
	await writeFile(join(output, ...javascriptRecord.path.split("/")), Buffer.concat([originalEngineScript, Buffer.from("tampered") ]));
	await assert.rejects(() => auditRelease(output), /JavaScript does not match|SHA-256 mismatch/);
	await writeFile(join(output, ...javascriptRecord.path.split("/")), originalEngineScript);
	await auditRelease(output);

	const experimentalOutput = join(temporaryRoot, "webxr", "Ports", "SurrealEngine-Experimental");
	const experimentalBasePath = "/webxr/Ports/SurrealEngine-Experimental/";
	const experimentalResult = await packageRelease({
		sourceRoot,
		engineDirectory: engine,
		outputDirectory: experimentalOutput,
		correspondingSourceMetadata: sourceMetadataPath,
		allowDirtySourceTree: true,
		intendedBasePath: experimentalBasePath,
	});
	assert.equal(experimentalResult.manifest.intendedBasePath, experimentalBasePath);
	assert.equal(JSON.parse(await readFile(join(experimentalOutput, "release-manifest.json"), "utf8")).intendedBasePath,
		experimentalBasePath);
	const experimentalHosting = await readFile(join(experimentalOutput, "HOSTING.txt"), "utf8");
	assert.match(experimentalHosting, /Intended base path: \/webxr\/Ports\/SurrealEngine-Experimental\//);
	assert.doesNotMatch(experimentalHosting, /Intended base path: \/webxr\/Ports\/SurrealEngine\//);

	assert.equal(validateIntendedBasePath("/"), "/");
	assert.equal(validateIntendedBasePath(experimentalBasePath), experimentalBasePath);
	for (const unsafePath of [
		"webxr/Ports/SurrealEngine/",
		"/webxr/Ports/SurrealEngine",
		"/webxr/../SurrealEngine/",
		"/webxr/./SurrealEngine/",
		"/webxr//SurrealEngine/",
		"/webxr/SurrealEngine/?candidate=1",
		"/webxr/SurrealEngine/#candidate",
		"/webxr\\SurrealEngine/",
		"/webxr/%2e%2e/SurrealEngine/",
	]) assert.throws(() => validateIntendedBasePath(unsafePath), /Intended base path/);
	await assert.rejects(() => packageRelease({
		sourceRoot,
		engineDirectory: engine,
		outputDirectory: join(temporaryRoot, "unsafe-base-path-package"),
		correspondingSourceMetadata: sourceMetadataPath,
		allowDirtySourceTree: true,
		intendedBasePath: "/webxr/../SurrealEngine/",
	}), /Intended base path/);

	await writeFile(join(output, "DM-Forbidden.unr"), "not game data");
	await assert.rejects(() => auditRelease(output), /game data is forbidden/);
	await rm(join(output, "DM-Forbidden.unr"));
	const productionEngine = join(temporaryRoot, "production-engine");
	await mkdir(productionEngine);
	await writeFile(join(productionEngine, "SurrealEngine.js"), "runtime");
	await writeFile(join(productionEngine, "SurrealEngine.wasm"), "runtime");
	await writeFile(join(productionEngine, "CMakeCache.txt"), "SURREAL_GAMEDATA_DIR:PATH=\n");
	const productionProvenance = { ...provenance, releaseProfile: "production", assertions: 0, stackOverflowCheck: 0 };
	await writeFile(join(productionEngine, "build-compliance-provenance.json"), JSON.stringify(productionProvenance));
	assert.equal((await validateNoDataBuild(productionEngine)).provenance.releaseProfile, "production");
	await writeFile(join(productionEngine, "build-compliance-provenance.json"),
		JSON.stringify({ ...productionProvenance, buildType: "Debug" }));
	await assert.rejects(() => validateNoDataBuild(productionEngine), /provenance is incomplete/);

	const unsafeEngine = join(temporaryRoot, "unsafe-engine");
	await mkdir(unsafeEngine);
	await writeFile(join(unsafeEngine, "SurrealEngine.js"), "runtime");
	await writeFile(join(unsafeEngine, "SurrealEngine.wasm"), "runtime");
	await writeFile(join(unsafeEngine, "CMakeCache.txt"), "SURREAL_GAMEDATA_DIR:PATH=C:/Games/UT99\n");
	await writeFile(join(unsafeEngine, "build-compliance-provenance.json"), JSON.stringify(provenance));
	await assert.rejects(() => validateNoDataBuild(unsafeEngine), /not auditable as data-free/);

	await writeFile(join(output, "source", sourceMetadata.archiveFile), "tampered");
	await assert.rejects(() => auditRelease(output), /archive does not match/);

	console.log("Static browser release packaging and data audit tests passed");
} finally {
	await rm(temporaryRoot, { recursive: true, force: true });
}
