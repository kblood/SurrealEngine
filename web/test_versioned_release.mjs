import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { createServer } from "node:http";
import { chmod, cp, mkdir, mkdtemp, readFile, rm, stat, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join, normalize, relative } from "node:path";
import {
	initializeVersionedRelease, parseVersionPointer, promoteVersionedRelease,
	publishVersionedRelease, recoverVersionedRelease, rollbackVersionedRelease,
} from "./versioned_release_library.mjs";
import { verifyRelease } from "./release_transaction_library.mjs";

const BASE_PATH = "/webxr/Ports/SurrealEngine/";

function sha256(contents) {
	return createHash("sha256").update(contents).digest("hex");
}

async function createRelease(root, buildId, marker) {
	const files = new Map([
		[".htaccess", `<IfModule mod_headers.c>\n  Header always set X-Test-Generation "${marker}"\n</IfModule>\n`],
		["index.html", `<!doctype html><script src="assets/app.${marker}.js"></script><title>${marker}</title>\n`],
		[`assets/app.${marker}.js`, `globalThis.release = ${JSON.stringify(marker)};\n`],
		[`engine/SurrealEngine.${marker}.wasm`, Buffer.from(`wasm-${marker}\n`)],
	]);
	for (const [path, contents] of files) {
		const destination = join(root, ...path.split("/"));
		await mkdir(dirname(destination), { recursive: true });
		await writeFile(destination, contents);
	}
	const records = [...files].map(([path, contents]) => {
		const bytes = Buffer.isBuffer(contents) ? contents : Buffer.from(contents);
		return { path, bytes: bytes.length, sha256: sha256(bytes) };
	}).sort((left, right) => left.path.localeCompare(right.path));
	const manifest = {
		schema: "surrealengine-browser-release-v2", version: 2, buildId,
		intendedBasePath: BASE_PATH, files: records,
		sourceCompliance: { sourceCommit: marker.repeat(40).slice(0, 40), sourceTree: marker.repeat(40).slice(0, 40) },
	};
	await writeFile(join(root, "release-manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
	return verifyRelease(root, { intendedBasePath: BASE_PATH });
}

function startPointerAwareServer(stableRoot) {
	const server = createServer(async (request, response) => {
		try {
			const url = new URL(request.url, "http://127.0.0.1");
			if (!url.pathname.startsWith(BASE_PATH)) { response.writeHead(404).end(); return; }
			const requested = decodeURIComponent(url.pathname.slice(BASE_PATH.length));
			if (requested === "" || requested === "index.html") {
				const pointer = parseVersionPointer(await readFile(join(stableRoot, ".htaccess"), "utf8"), BASE_PATH);
				if (pointer) {
					response.writeHead(302, { Location: pointer.target, "Cache-Control": "no-store" }).end();
					return;
				}
			}
			const fileRequest = !requested ? "index.html" :
				(requested.endsWith("/") ? `${requested}index.html` : requested);
			const path = normalize(join(stableRoot, ...fileRequest.split("/")));
			if (relative(stableRoot, path).startsWith("..")) { response.writeHead(400).end(); return; }
			const contents = await readFile(path);
			response.writeHead(200, { "Content-Type": path.endsWith(".wasm") ? "application/wasm" : "text/plain" });
			if (request.method === "HEAD") response.end();
			else response.end(contents);
		} catch (error) {
			response.writeHead(error?.code === "ENOENT" ? 404 : 500).end();
		}
	});
	return new Promise((resolve, reject) => {
		server.once("error", reject);
		server.listen(0, "127.0.0.1", () => resolve(server));
	});
}

async function verifyHttpGeneration(origin, location) {
	const manifestSha256 = location.split("/").filter(Boolean).at(-1);
	assert.match(manifestSha256, /^[0-9a-f]{64}$/);
	const manifestResponse = await fetch(`${origin}${location}release-manifest.json`);
	assert.equal(manifestResponse.status, 200);
	const manifestBytes = Buffer.from(await manifestResponse.arrayBuffer());
	assert.equal(sha256(manifestBytes), manifestSha256);
	const manifest = JSON.parse(manifestBytes.toString("utf8"));
	for (const record of manifest.files.filter(item => /\.(?:js|wasm)$/.test(item.path))) {
		const response = await fetch(`${origin}${location}${record.path}`);
		assert.equal(response.status, 200);
		const bytes = Buffer.from(await response.arrayBuffer());
		assert.equal(bytes.length, record.bytes);
		assert.equal(sha256(bytes), record.sha256);
	}
}

const temporaryRoot = await mkdtemp(join(tmpdir(), "surreal-versioned-release-"));
let server;
try {
	const siteRoot = join(temporaryRoot, "site");
	const stable = join(siteRoot, "Ports", "SurrealEngine");
	const candidateA = join(temporaryRoot, "candidate-a");
	const candidateB = join(temporaryRoot, "candidate-b");
	await mkdir(join(siteRoot, "Ports"), { recursive: true });
	const identityA = await createRelease(candidateA, "release-a", "a");
	const identityB = await createRelease(candidateB, "release-b", "b");
	await cp(candidateA, stable, { recursive: true });
	if (process.platform !== "win32") await chmod(join(stable, ".htaccess"), 0o640);
	const originalHtaccessMode = (await stat(join(stable, ".htaccess"))).mode & 0o777;
	const common = { siteRoot, liveRelative: "Ports/SurrealEngine", intendedBasePath: BASE_PATH };
	const namespace = `.SurrealEngine-${sha256("Ports/SurrealEngine").slice(0, 12)}`;
	const cli = join(import.meta.dirname, "versioned_release.mjs");
	const missingExecute = spawnSync(process.execPath, [cli, "initialize",
		`--site-root=${siteRoot}`, `--live-manifest-sha256=${identityA.manifestSha256}`], { encoding: "utf8" });
	assert.notEqual(missingExecute.status, 0);
	assert.match(missingExecute.stderr, /execute=yes/);

	server = await startPointerAwareServer(stable);
	const address = server.address();
	const origin = `http://127.0.0.1:${address.port}`;
	const stableUrl = `${origin}${BASE_PATH}`;
	const legacyHtml = await (await fetch(stableUrl)).text();
	assert.match(legacyHtml, /<title>a<\/title>/);

	const initialized = await initializeVersionedRelease({
		...common, liveManifestSha256: identityA.manifestSha256,
	});
	assert.equal(initialized.currentManifestSha256, identityA.manifestSha256);
	assert.equal((await stat(join(stable, ".htaccess"))).mode & 0o777, originalHtaccessMode);
	const initializedAgain = await initializeVersionedRelease({
		...common, liveManifestSha256: identityA.manifestSha256,
	});
	assert.equal(initializedAgain.alreadyInitialized, true);
	const pointerA = parseVersionPointer(await readFile(join(stable, ".htaccess"), "utf8"), BASE_PATH);
	assert.equal(pointerA.manifestSha256, identityA.manifestSha256);
	assert.match(pointerA.contents, /Cache-Control "no-store"/);
	assert.equal(await (await fetch(`${stableUrl}assets/app.a.js`)).text(), "globalThis.release = \"a\";\n");
	assert.deepEqual(new Uint8Array(await (await fetch(`${stableUrl}engine/SurrealEngine.a.wasm`)).arrayBuffer()),
		new Uint8Array(Buffer.from("wasm-a\n")));
	assert.equal((await (await fetch(`${stableUrl}release-manifest.json`)).json()).buildId, "release-a");
	const freshA = await fetch(stableUrl, { redirect: "manual" });
	assert.equal(freshA.status, 302);
	assert.equal(freshA.headers.get("cache-control"), "no-store");
	assert.equal(freshA.headers.get("location"), `${BASE_PATH}releases/${identityA.manifestSha256}/`);
	assert.match(await (await fetch(`${origin}${freshA.headers.get("location")}`)).text(), /<title>a<\/title>/);

	const publishedB = await publishVersionedRelease({
		...common, candidate: candidateB, manifestSha256: identityB.manifestSha256,
	});
	assert.equal(publishedB.published.manifestSha256, identityB.manifestSha256);
	assert.equal((await publishVersionedRelease({
		...common, candidate: candidateB, manifestSha256: identityB.manifestSha256,
	})).alreadyPublished, true);

	const versionAIndex = `${origin}${BASE_PATH}releases/${identityA.manifestSha256}/index.html`;
	assert.match(await (await fetch(versionAIndex)).text(), /<title>a<\/title>/);
	await assert.rejects(promoteVersionedRelease({
		...common, currentManifestSha256: identityA.manifestSha256,
		targetManifestSha256: identityB.manifestSha256, recoverOnFailure: false,
		transitionHook: state => { if (state === "pointer-replaced") throw new Error("simulated pointer process loss"); },
	}), /simulated pointer process loss/);
	assert.equal(parseVersionPointer(await readFile(join(stable, ".htaccess"), "utf8"), BASE_PATH).manifestSha256,
		identityB.manifestSha256);
	const recovered = await recoverVersionedRelease(common);
	assert.equal(recovered.action, "finalized-pointer-commit");
	assert.equal(recovered.currentManifestSha256, identityB.manifestSha256);
	assert.equal(await (await fetch(`${origin}${BASE_PATH}releases/${identityA.manifestSha256}/assets/app.a.js`)).text(),
		"globalThis.release = \"a\";\n");
	assert.equal((await fetch(stableUrl, { redirect: "manual" })).headers.get("location"),
		`${BASE_PATH}releases/${identityB.manifestSha256}/`);

	await rollbackVersionedRelease({
		...common, currentManifestSha256: identityB.manifestSha256,
		targetManifestSha256: identityA.manifestSha256,
	});
	assert.equal(parseVersionPointer(await readFile(join(stable, ".htaccess"), "utf8"), BASE_PATH).manifestSha256,
		identityA.manifestSha256);

	await assert.rejects(promoteVersionedRelease({
		...common, currentManifestSha256: identityA.manifestSha256,
		targetManifestSha256: identityB.manifestSha256, recoverOnFailure: false,
		transitionHook: state => { if (state === "pointer-replaced") throw new Error("ambiguous pointer state"); },
	}), /ambiguous pointer state/);
	await writeFile(join(stable, ".htaccess"), "corrupt pointer file\n");
	const restored = await recoverVersionedRelease(common);
	assert.equal(restored.action, "restored-pointer-after-ambiguous-file-state");
	assert.equal(restored.currentManifestSha256, identityA.manifestSha256);

	const observedLocations = [];
	let reading = true;
	const reader = (async () => {
		while (reading) {
			const response = await fetch(stableUrl, { method: "HEAD", redirect: "manual" });
			assert.equal(response.status, 302);
			const location = response.headers.get("location");
			observedLocations.push(location);
			await verifyHttpGeneration(origin, location);
			await new Promise(resolve => setTimeout(resolve, 0));
		}
	})();
	for (let index = 0; index < 4; index++) {
		await promoteVersionedRelease({
			...common, currentManifestSha256: identityA.manifestSha256,
			targetManifestSha256: identityB.manifestSha256,
		});
		await rollbackVersionedRelease({
			...common, currentManifestSha256: identityB.manifestSha256,
			targetManifestSha256: identityA.manifestSha256,
		});
	}
	reading = false;
	await reader;
	assert.ok(observedLocations.length > 0);
	const allowed = new Set([
		`${BASE_PATH}releases/${identityA.manifestSha256}/`,
		`${BASE_PATH}releases/${identityB.manifestSha256}/`,
	]);
	assert.ok(observedLocations.every(location => allowed.has(location)));
	for (const location of allowed)
		assert.equal((await fetch(`${origin}${location}`, { redirect: "manual" })).status, 200);

	const lockPath = join(siteRoot, `${namespace}.release-lock`);
	await writeFile(lockPath,
		`${JSON.stringify({ pid: 2147483647, startedAt: "2026-07-24T07:00:00.000Z" })}\n`);
	await assert.rejects(recoverVersionedRelease(common), /Another release transaction owns/);
	await assert.rejects(recoverVersionedRelease({ ...common, breakStaleLock: true }),
		/Expected current manifest hash/);
	assert.ok((await stat(lockPath)).isFile());
	await assert.rejects(recoverVersionedRelease({
		...common, breakStaleLock: true, currentManifestSha256: identityB.manifestSha256,
	}), /does not match/);
	assert.ok((await stat(lockPath)).isFile());
	const unlocked = await recoverVersionedRelease({
		...common, breakStaleLock: true, currentManifestSha256: identityA.manifestSha256,
	});
	assert.equal(unlocked.action, "cleared-dead-nonpointer-lock");
	assert.ok((await stat(join(siteRoot, unlocked.preservedStaleLock))).isFile());
	assert.equal((await stat(join(stable, ".htaccess"))).mode & 0o777, originalHtaccessMode);

	const legacyJournal = join(siteRoot, `${namespace}.release-transaction.json`);
	await writeFile(legacyJournal, "{}\n");
	await assert.rejects(publishVersionedRelease({
		...common, candidate: candidateB, manifestSha256: identityB.manifestSha256,
	}), /unfinished directory-generation journal/);
	await rm(legacyJournal);

	console.log("Immutable version-root publication, atomic pointer, recovery, rollback, and HTTP boundary tests passed");
} finally {
	if (server) await new Promise(resolve => server.close(resolve));
	await rm(temporaryRoot, { recursive: true, force: true });
}
