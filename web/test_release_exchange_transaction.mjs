import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { cp, mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import {
	promoteRelease, recoverRelease, rollbackRelease, stageRelease, verifyRelease,
} from "./release_transaction_library.mjs";

if (process.platform !== "linux") {
	console.log("Atomic renameat2 release exchange test skipped outside Linux");
	process.exit(0);
}

const BASE_PATH = "/webxr/Ports/SurrealEngine/";

async function createRelease(root, buildId, marker) {
	await mkdir(join(root, "assets"), { recursive: true });
	const index = `<!doctype html><title>${marker}</title>\n`;
	const script = `globalThis.release = ${JSON.stringify(marker)};\n`;
	await writeFile(join(root, "index.html"), index, "utf8");
	await writeFile(join(root, "assets", "app.js"), script, "utf8");
	const record = (path, value) => ({
		path, bytes: Buffer.byteLength(value),
		sha256: createHash("sha256").update(value).digest("hex"),
	});
	const manifest = {
		schema: "surrealengine-browser-release-v2", version: 2, buildId,
		intendedBasePath: BASE_PATH,
		files: [record("assets/app.js", script), record("index.html", index)],
		sourceCompliance: {
			sourceCommit: marker.repeat(40).slice(0, 40),
			sourceTree: marker.repeat(40).slice(0, 40),
		},
	};
	await writeFile(join(root, "release-manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
	return verifyRelease(root, { intendedBasePath: BASE_PATH });
}

const temporaryRoot = await mkdtemp(join(tmpdir(), "surreal-release-exchange-"));
try {
	const siteRoot = join(temporaryRoot, "site");
	const candidateA = join(temporaryRoot, "candidate-a");
	const candidateB = join(temporaryRoot, "candidate-b");
	const live = join(siteRoot, "Ports", "SurrealEngine");
	await mkdir(join(siteRoot, "Ports"), { recursive: true });
	const identityA = await createRelease(candidateA, "release-a", "a");
	const identityB = await createRelease(candidateB, "release-b", "b");
	await cp(candidateA, live, { recursive: true });
	const common = {
		siteRoot, liveRelative: "Ports/SurrealEngine", intendedBasePath: BASE_PATH,
		swapMode: "exchange",
	};

	let stage = await stageRelease({ ...common, candidate: candidateB });
	let promoted = await promoteRelease({
		...common, stageName: stage.stageName,
		manifestSha256: identityB.manifestSha256,
		liveManifestSha256: identityA.manifestSha256,
	});
	assert.equal(promoted.swapMode, "exchange");
	assert.equal((await verifyRelease(live)).manifestSha256, identityB.manifestSha256);
	assert.equal((await verifyRelease(join(siteRoot, promoted.displacedName))).manifestSha256,
		identityA.manifestSha256);
	await rollbackRelease({
		...common, rollbackName: promoted.displacedName,
		manifestSha256: identityA.manifestSha256,
		liveManifestSha256: identityB.manifestSha256,
	});
	assert.equal((await verifyRelease(live)).manifestSha256, identityA.manifestSha256);

	stage = await stageRelease({ ...common, candidate: candidateB });
	await assert.rejects(promoteRelease({
		...common, stageName: stage.stageName,
		manifestSha256: identityB.manifestSha256,
		liveManifestSha256: identityA.manifestSha256,
		recoverOnFailure: false,
		transitionHook: state => {
			if (state === "directories-exchanged") throw new Error("crash after exchange");
		},
	}), /crash after exchange/);
	assert.equal((await verifyRelease(live)).manifestSha256, identityB.manifestSha256);
	assert.equal((await verifyRelease(join(siteRoot, stage.stageName))).manifestSha256,
		identityA.manifestSha256);
	let recovered = await recoverRelease(common);
	assert.equal(recovered.action, "finalized-atomic-exchange");
	assert.equal(recovered.live.manifestSha256, identityB.manifestSha256);
	await rollbackRelease({
		...common, rollbackName: recovered.journal.displacedName,
		manifestSha256: identityA.manifestSha256,
		liveManifestSha256: identityB.manifestSha256,
	});

	stage = await stageRelease({ ...common, candidate: candidateB });
	await assert.rejects(promoteRelease({
		...common, stageName: stage.stageName,
		manifestSha256: identityB.manifestSha256,
		liveManifestSha256: identityA.manifestSha256,
		recoverOnFailure: false,
		transitionHook: state => {
			if (state === "directories-exchanged") throw new Error("tamper after exchange");
		},
	}), /tamper after exchange/);
	await writeFile(join(live, "assets", "app.js"), "tampered promoted generation\n", "utf8");
	recovered = await recoverRelease(common);
	assert.equal(recovered.action, "rejected-incoming-and-reversed-atomic-exchange");
	assert.equal(recovered.live.manifestSha256, identityA.manifestSha256);
	assert.ok(recovered.journal.rejectedName.startsWith(".SurrealEngine-"));

	stage = await stageRelease({ ...common, candidate: candidateB });
	await assert.rejects(promoteRelease({
		...common, stageName: stage.stageName,
		manifestSha256: identityB.manifestSha256,
		liveManifestSha256: identityA.manifestSha256,
		recoverOnFailure: false,
		transitionHook: state => {
			if (state === "incoming-archived") throw new Error("crash after archive");
		},
	}), /crash after archive/);
	recovered = await recoverRelease(common);
	assert.equal(recovered.action, "finalized-archived-atomic-exchange");
	assert.equal(recovered.live.manifestSha256, identityB.manifestSha256);
	await rollbackRelease({
		...common, rollbackName: recovered.journal.displacedName,
		manifestSha256: identityA.manifestSha256,
		liveManifestSha256: identityB.manifestSha256,
	});
	assert.equal((await verifyRelease(live)).manifestSha256, identityA.manifestSha256);

	console.log("Linux renameat2 atomic promotion, recovery, rejection, and rollback tests passed");
} finally {
	await rm(temporaryRoot, { recursive: true, force: true });
}
