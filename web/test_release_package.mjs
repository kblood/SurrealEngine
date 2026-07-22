import assert from "node:assert/strict";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";
import { auditRelease, packageRelease, validateNoDataBuild } from "./package_browser_release.mjs";

const temporaryRoot = await mkdtemp(join(tmpdir(), "surreal-browser-package-"));
const sourceRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
try {
	const engine = join(temporaryRoot, "engine-build");
	const output = join(temporaryRoot, "webxr", "Ports", "SurrealEngine");
	await mkdir(engine, { recursive: true });
	await writeFile(join(engine, "SurrealEngine.js"), "const wasm = 'SurrealEngine.wasm';\n");
	await writeFile(join(engine, "SurrealEngine.wasm"), new Uint8Array([0, 97, 115, 109]));
	await writeFile(join(engine, "CMakeCache.txt"), "SURREAL_GAMEDATA_DIR:PATH=\n");

	const result = await packageRelease({
		sourceRoot,
		engineDirectory: engine,
		outputDirectory: output,
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
	const index = await readFile(join(output, "index.html"), "utf8");
	assert.match(index, /data-engine-base="\.\/engine\/"/);
	assert.doesNotMatch(index, /\.\.\/build-emscripten\//);
	assert.match(await readFile(join(output, "_headers"), "utf8"), /Cross-Origin-Embedder-Policy: require-corp/);
	assert.match(await readFile(join(output, "HOSTING.txt"), "utf8"), /statically includes the SurrealVideo/);
	assert.match(await readFile(join(output, "licenses", "SurrealVideo-LGPL-2.1.txt"), "utf8"), /GNU LESSER GENERAL PUBLIC LICENSE/);
	await auditRelease(output);

	await writeFile(join(output, "DM-Forbidden.unr"), "not game data");
	await assert.rejects(() => auditRelease(output), /game data is forbidden/);
	await rm(join(output, "DM-Forbidden.unr"));

	const unsafeEngine = join(temporaryRoot, "unsafe-engine");
	await mkdir(unsafeEngine);
	await writeFile(join(unsafeEngine, "SurrealEngine.js"), "runtime");
	await writeFile(join(unsafeEngine, "SurrealEngine.wasm"), "runtime");
	await writeFile(join(unsafeEngine, "CMakeCache.txt"), "SURREAL_GAMEDATA_DIR:PATH=C:/Games/UT99\n");
	await assert.rejects(() => validateNoDataBuild(unsafeEngine), /not auditable as data-free/);

	console.log("Static browser release packaging and data audit tests passed");
} finally {
	await rm(temporaryRoot, { recursive: true, force: true });
}
