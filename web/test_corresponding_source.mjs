import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { promisify } from "node:util";
import { createCorrespondingSource } from "./package_corresponding_source.mjs";

const run = promisify(execFile);
const temporaryRoot = await mkdtemp(join(tmpdir(), "surreal-source-package-"));
try {
	const repository = join(temporaryRoot, "repository");
	const output = join(temporaryRoot, "output");
	await mkdir(join(repository, "SurrealVideo"), { recursive: true });
	await mkdir(join(repository, "Docs"), { recursive: true });
	await writeFile(join(repository, "SurrealVideo", "COPYING.LGPLv2.1"), "LGPL 2.1\n");
	await writeFile(join(repository, "SurrealVideo", "COPYING.LGPLv3"), "LGPL 3\n");
	await writeFile(join(repository, "SurrealVideo", "README.md"), "FFmpeg-derived source\n");
	await writeFile(join(repository, "SurrealVideo", "decoder.c"), "int decoder(void) { return 1; }\n");
	await writeFile(join(repository, "Docs", "BROWSER_STATIC_RELINKING.md"), "build instructions\n");
	await run("git", ["init", "-q"], { cwd: repository, windowsHide: true });
	await run("git", ["config", "user.email", "test@example.invalid"], { cwd: repository, windowsHide: true });
	await run("git", ["config", "user.name", "Package Test"], { cwd: repository, windowsHide: true });
	await run("git", ["add", "."], { cwd: repository, windowsHide: true });
	await run("git", ["commit", "-q", "-m", "fixture"], { cwd: repository, windowsHide: true });
	await mkdir(output);
	const first = await createCorrespondingSource({ sourceRoot: repository,
		outputArchive: join(output, "first.tar.gz") });
	const second = await createCorrespondingSource({ sourceRoot: repository,
		outputArchive: join(output, "second.tar.gz") });
	assert.equal(first.metadata.archiveSha256, second.metadata.archiveSha256);
	assert.equal(first.metadata.sourceCommit, second.metadata.sourceCommit);
	assert.equal(first.metadata.sourceTree, second.metadata.sourceTree);
	assert.equal(first.metadata.archiveScope, "complete-tracked-source-tree");
	assert.equal(first.metadata.trackedFileCount, 5);
	assert.equal(first.metadata.surrealVideoFiles.length, 4);
	assert.ok(first.metadata.surrealVideoFiles.every(file => /^[0-9a-f]{64}$/.test(file.sha256)));
	assert.deepEqual(JSON.parse(await readFile(first.metadataPath, "utf8")), first.metadata);
	await writeFile(join(repository, "SurrealVideo", "decoder.c"), "dirty\n");
	await assert.rejects(() => createCorrespondingSource({ sourceRoot: repository,
		outputArchive: join(output, "dirty.tar.gz") }), /dirty source tree/);
	console.log("Deterministic corresponding-source archive tests passed");
} finally {
	await rm(temporaryRoot, { recursive: true, force: true });
}
