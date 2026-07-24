import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { cp, mkdir, mkdtemp, readFile, rm, stat, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import {
	promoteRelease, recoverRelease, rollbackRelease, stageRelease, verifyRelease,
} from "./release_transaction_library.mjs";

const BASE_PATH = "/webxr/Ports/SurrealEngine/";

function sha256(contents) {
	return createHash("sha256").update(contents).digest("hex");
}

async function createRelease(root, buildId, marker) {
	await mkdir(join(root, "assets"), { recursive: true });
	const contents = new Map([
		["index.html", `<!doctype html><title>${marker}</title>\n`],
		["assets/app.js", `globalThis.release = ${JSON.stringify(marker)};\n`],
	]);
	for (const [path, value] of contents)
		await writeFile(join(root, ...path.split("/")), value, "utf8");
	const files = [];
	for (const [path, value] of contents) {
		const bytes = Buffer.byteLength(value);
		files.push({ path, bytes, sha256: sha256(Buffer.from(value)) });
	}
	files.sort((left, right) => left.path.localeCompare(right.path));
	const manifest = {
		schema: "surrealengine-browser-release-v2", version: 2, buildId,
		intendedBasePath: BASE_PATH, files,
		sourceCompliance: {
			sourceCommit: marker.repeat(40).slice(0, 40),
			sourceTree: marker.repeat(40).slice(0, 40),
		},
	};
	await writeFile(join(root, "release-manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
	return verifyRelease(root, { intendedBasePath: BASE_PATH });
}

const temporaryRoot = await mkdtemp(join(tmpdir(), "surreal-release-transaction-"));
try {
	const siteRoot = join(temporaryRoot, "site");
	const candidateA = join(temporaryRoot, "candidate-a");
	const candidateB = join(temporaryRoot, "candidate-b");
	const candidateBad = join(temporaryRoot, "candidate-bad");
	const live = join(siteRoot, "Ports", "SurrealEngine");
	await mkdir(join(siteRoot, "Ports"), { recursive: true });
	const identityA = await createRelease(candidateA, "release-a", "a");
	const identityB = await createRelease(candidateB, "release-b", "b");
	await createRelease(candidateBad, "release-bad", "c");
	await writeFile(join(candidateBad, "assets", "app.js"), "tampered\n", "utf8");
	await cp(candidateA, live, { recursive: true });
	assert.equal((await verifyRelease(live, { intendedBasePath: BASE_PATH })).buildId, "release-a");

	const common = { siteRoot, liveRelative: "Ports/SurrealEngine", intendedBasePath: BASE_PATH };
	const namespace = `.SurrealEngine-${sha256("Ports/SurrealEngine").slice(0, 12)}`;
	const cli = join(import.meta.dirname, "release_transaction.mjs");
	const missingStageHash = spawnSync(process.execPath, [cli, "stage",
		`--site-root=${siteRoot}`, `--candidate=${candidateB}`], { encoding: "utf8" });
	assert.notEqual(missingStageHash.status, 0);
	assert.match(missingStageHash.stderr, /manifest-sha256/);
	await assert.rejects(stageRelease({ ...common, liveRelative: "../escape", candidate: candidateB }),
		/safe relative path/);
	await assert.rejects(stageRelease({ ...common, candidate: candidateBad }), /integrity verification/);
	assert.equal((await verifyRelease(live)).manifestSha256, identityA.manifestSha256);

	const stageRecordPath = join(temporaryRoot, "stage-record.json");
	const staged = await stageRelease({ ...common, candidate: candidateB, recordPath: stageRecordPath });
	assert.equal(staged.operation, "stage");
	assert.equal(staged.alreadyStaged, false);
	assert.equal(staged.candidate.manifestSha256, identityB.manifestSha256);
	assert.equal(JSON.parse(await readFile(stageRecordPath, "utf8")).stageName, staged.stageName);
	const restaged = await stageRelease({ ...common, candidate: candidateB });
	assert.equal(restaged.alreadyStaged, true);
	const missingExecute = spawnSync(process.execPath, [cli, "promote",
		`--site-root=${siteRoot}`, `--stage-name=${staged.stageName}`,
		`--manifest-sha256=${identityB.manifestSha256}`,
		`--live-manifest-sha256=${identityA.manifestSha256}`], { encoding: "utf8" });
	assert.notEqual(missingExecute.status, 0);
	assert.match(missingExecute.stderr, /execute=yes/);

	const stagePath = join(siteRoot, staged.stageName);
	await writeFile(join(stagePath, "assets", "app.js"), "tampered stage\n", "utf8");
	await assert.rejects(promoteRelease({
		...common, stageName: staged.stageName, manifestSha256: identityB.manifestSha256,
	}), /integrity verification/);
	assert.equal((await verifyRelease(live)).manifestSha256, identityA.manifestSha256);
	await rm(stagePath, { recursive: true });
	await stageRelease({ ...common, candidate: candidateB });
	await assert.rejects(promoteRelease({
		...common, stageName: staged.stageName, manifestSha256: identityB.manifestSha256,
		liveManifestSha256: "0".repeat(64),
	}), /required SHA-256/);
	assert.equal((await verifyRelease(live)).manifestSha256, identityA.manifestSha256);

	await assert.rejects(promoteRelease({
		...common, stageName: staged.stageName, manifestSha256: identityB.manifestSha256,
		recoverOnFailure: false,
		transitionHook: state => { if (state === "live-renamed") throw new Error("simulated process loss"); },
	}), /simulated process loss/);
	await assert.rejects(stat(live), error => error.code === "ENOENT");
	const journalPath = join(siteRoot, `${namespace}.release-transaction.json`);
	const validJournalText = await readFile(journalPath, "utf8");
	const wrongTargetJournal = JSON.parse(validJournalText);
	wrongTargetJournal.liveRelative = "Preview/SurrealEngine";
	await writeFile(journalPath, `${JSON.stringify(wrongTargetJournal, null, 2)}\n`, "utf8");
	await assert.rejects(recoverRelease(common), /unsupported schema or state/);
	await writeFile(journalPath, validJournalText, "utf8");
	const abandonedLock = join(siteRoot, `${namespace}.release-lock`);
	await writeFile(abandonedLock,
		`${JSON.stringify({ pid: 2147483647, startedAt: "2026-07-24T05:00:00.000Z" })}\n`, "utf8");
	await assert.rejects(recoverRelease(common), /Another release transaction owns/);
	const recoveredBeforeMove = await recoverRelease({ ...common, breakStaleLock: true });
	assert.equal(recoveredBeforeMove.action, "restored-displaced-generation");
	assert.ok(recoveredBeforeMove.preservedStaleLock.startsWith(`${namespace}.stale-lock-`));
	assert.ok((await stat(join(siteRoot, recoveredBeforeMove.preservedStaleLock))).isFile());
	assert.equal((await verifyRelease(live)).manifestSha256, identityA.manifestSha256);
	assert.equal((await verifyRelease(stagePath)).manifestSha256, identityB.manifestSha256);

	const promotionRecordPath = join(temporaryRoot, "promotion-record.json");
	const promotion = await promoteRelease({
		...common, stageName: staged.stageName, manifestSha256: identityB.manifestSha256,
		liveManifestSha256: identityA.manifestSha256,
		date: new Date("2026-07-24T05:00:00.000Z"), recordPath: promotionRecordPath,
	});
	assert.equal(promotion.liveBefore.buildId, "release-a");
	assert.equal(promotion.liveAfter.buildId, "release-b");
	assert.equal((await verifyRelease(live)).manifestSha256, identityB.manifestSha256);
	assert.equal(JSON.parse(await readFile(promotionRecordPath, "utf8")).displacedName,
		promotion.displacedName);
	assert.equal((await verifyRelease(join(siteRoot, promotion.displacedName))).manifestSha256,
		identityA.manifestSha256);

	const rollback = await rollbackRelease({
		...common, rollbackName: promotion.displacedName,
		manifestSha256: identityA.manifestSha256,
		liveManifestSha256: identityB.manifestSha256,
		date: new Date("2026-07-24T05:01:00.000Z"),
	});
	assert.equal(rollback.liveBefore.buildId, "release-b");
	assert.equal(rollback.liveAfter.buildId, "release-a");
	assert.equal((await verifyRelease(live)).manifestSha256, identityA.manifestSha256);
	assert.equal((await verifyRelease(join(siteRoot, rollback.displacedName))).manifestSha256,
		identityB.manifestSha256);

	await stageRelease({ ...common, candidate: candidateB });
	await assert.rejects(promoteRelease({
		...common, stageName: staged.stageName, manifestSha256: identityB.manifestSha256,
		liveManifestSha256: identityA.manifestSha256,
		transitionHook: state => { if (state === "live-moved") throw new Error("recover automatically"); },
	}), /recover automatically/);
	assert.equal((await verifyRelease(live)).manifestSha256, identityA.manifestSha256);
	assert.equal((await verifyRelease(stagePath)).manifestSha256, identityB.manifestSha256);

	await assert.rejects(promoteRelease({
		...common, stageName: staged.stageName, manifestSha256: identityB.manifestSha256,
		liveManifestSha256: identityA.manifestSha256,
		recoverOnFailure: false,
		transitionHook: state => { if (state === "incoming-renamed") throw new Error("loss after incoming move"); },
	}), /loss after incoming move/);
	assert.equal((await verifyRelease(live)).manifestSha256, identityB.manifestSha256);
	const finalized = await recoverRelease(common);
	assert.equal(finalized.action, "finalized-verified-generation");
	assert.equal(finalized.live.manifestSha256, identityB.manifestSha256);

	const rollbackGeneration = finalized.journal.displacedName;
	await rollbackRelease({
		...common, rollbackName: rollbackGeneration, manifestSha256: identityA.manifestSha256,
		liveManifestSha256: identityB.manifestSha256,
		date: new Date("2026-07-24T05:02:00.000Z"),
	});
	await stageRelease({ ...common, candidate: candidateB });
	await assert.rejects(promoteRelease({
		...common, stageName: staged.stageName, manifestSha256: identityB.manifestSha256,
		liveManifestSha256: identityA.manifestSha256,
		recoverOnFailure: false,
		transitionHook: state => { if (state === "incoming-moved") throw new Error("tamper before recovery"); },
	}), /tamper before recovery/);
	await writeFile(join(live, "assets", "app.js"), "tampered promoted release\n", "utf8");
	const rejected = await recoverRelease(common);
	assert.equal(rejected.action, "rejected-incoming-and-restored-displaced-generation");
	assert.equal(rejected.live.manifestSha256, identityA.manifestSha256);
	assert.ok(rejected.journal.rejectedName.startsWith(`${namespace}.rejected-release-b-`));

	await writeFile(join(siteRoot, `${namespace}.release-lock`),
		`${JSON.stringify({ pid: process.pid, startedAt: new Date().toISOString() })}\n`, "utf8");
	await assert.rejects(stageRelease({ ...common, candidate: candidateB }), /Another release transaction owns/);
	await rm(join(siteRoot, `${namespace}.release-lock`));

	await writeFile(join(siteRoot, `${namespace}.release-lock`),
		`${JSON.stringify({ pid: 2147483647, startedAt: "2026-07-24T05:03:00.000Z" })}\n`, "utf8");
	await assert.rejects(recoverRelease({ ...common, breakStaleLock: true }),
		/expected live manifest SHA-256/);
	assert.ok((await stat(join(siteRoot, `${namespace}.release-lock`))).isFile());
	await assert.rejects(recoverRelease({
		...common, breakStaleLock: true, liveManifestSha256: identityB.manifestSha256,
	}), /required SHA-256/);
	assert.ok((await stat(join(siteRoot, `${namespace}.release-lock`))).isFile());
	const stageLockRecovery = await recoverRelease({
		...common, breakStaleLock: true, liveManifestSha256: identityA.manifestSha256,
	});
	assert.equal(stageLockRecovery.action, "cleared-dead-staging-lock");
	assert.equal(stageLockRecovery.live.manifestSha256, identityA.manifestSha256);
	assert.ok((await stat(join(siteRoot, stageLockRecovery.preservedStaleLock))).isFile());

	console.log("Atomic browser release stage, promotion, crash recovery, and rollback tests passed");
} finally {
	await rm(temporaryRoot, { recursive: true, force: true });
}
