import assert from "node:assert/strict";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";
import { auditRelease, packageRelease, validateNoDataBuild } from "./package_browser_release.mjs";
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
		schema: "surrealengine-browser-build-provenance-v1",
		sourceCommit: identity.sourceCommit, sourceTree: identity.sourceTree,
		sourceDirty: false, toolchain: "Emscripten", emscriptenVersion: "6.0.2",
		emscriptenRevision: "7a2d97d627ff4945eae28847ce0387ac52b92c09",
		compilerId: "Clang", compilerVersion: "test",
		cmakeVersion: "test", surrealVideoLinkage: "static-wasm",
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
	assert.equal(result.manifest.schema, "surrealengine-browser-release-v1");
	assert.deepEqual(result.manifest.games, ["ut99", "unreal-gold"]);
	assert.deepEqual(result.manifest.presentations, ["flat", "webxr"]);
	assert.equal(result.manifest.intendedBasePath, "/webxr/Ports/SurrealEngine/");
	assert.equal(result.manifest.dependencies[0].sha, "6c614d56e66e6ea8882aada892b93bc6526e0a33");
	assert.equal(result.manifest.dependencies[1].sha, "d472068ad5894dc8cdddeefbb4f491bd118f2592");
	assert.ok(result.manifest.files.some(file => file.path === "engine/SurrealEngine.wasm" && file.expectedMime === "application/wasm"));
	assert.ok(result.manifest.files.some(file => file.path === "webxr_provider.js"));
	assert.ok(result.manifest.files.some(file => file.path === "webxr_webgl_bridge.js"));
	assert.ok(result.manifest.files.some(file => file.path === "webxr_diagnostics.js"));
	assert.ok(result.manifest.files.some(file => file.path === "licenses/SurrealVideo-LGPL-2.1.txt"));
	assert.ok(result.manifest.files.some(file => file.path === "licenses/SurrealVideo-README.md"));
	assert.ok(result.manifest.files.some(file => file.path === "licenses/SurrealVideo-Relinking.md"));
	assert.ok(result.manifest.files.some(file => file.path === "source/SurrealEngine-corresponding-source.tar.gz"));
	assert.ok(result.manifest.files.some(file => file.path === "source/SurrealEngine-corresponding-source.tar.gz" && file.expectedMime === "application/gzip"));
	assert.equal(result.manifest.sourceCompliance.archiveSha256, sourceMetadata.archiveSha256);
	assert.equal(result.manifest.sourceCompliance.sourceCommit, identity.sourceCommit);
	const index = await readFile(join(output, "index.html"), "utf8");
	assert.match(index, /data-engine-base="\.\/engine\/"/);
	assert.doesNotMatch(index, /\.\.\/build-emscripten\//);
	assert.match(index, /data-source-compliance/);
	assert.match(index, /source\/SurrealEngine-corresponding-source\.tar\.gz/);
	assert.match(index, /browser may call folder selection an .upload./i);
	assert.doesNotMatch(index, /Folder upload fallback/);
	assert.match(await readFile(join(output, "_headers"), "utf8"), /Cross-Origin-Embedder-Policy: require-corp/);
	assert.match(await readFile(join(output, "HOSTING.txt"), "utf8"), /Source archive SHA-256/);
	assert.match(await readFile(join(output, "licenses", "SurrealVideo-LGPL-2.1.txt"), "utf8"), /GNU LESSER GENERAL PUBLIC LICENSE/);
	const compliance = JSON.parse(await readFile(join(output, "source-compliance.json"), "utf8"));
	assert.equal(compliance.wasmSha256, await sha256File(join(engine, "SurrealEngine.wasm")));
	assert.equal(compliance.buildProvenance.surrealVideoLinkage, "static-wasm");
	assert.match(await readFile(join(output, "SOURCE-OFFER.txt"), "utf8"), new RegExp(sourceMetadata.archiveSha256));
	await auditRelease(output);

	await writeFile(join(output, "DM-Forbidden.unr"), "not game data");
	await assert.rejects(() => auditRelease(output), /game data is forbidden/);
	await rm(join(output, "DM-Forbidden.unr"));

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
