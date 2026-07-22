import { createHash } from "node:crypto";
import { execFile } from "node:child_process";
import { access, mkdir, readFile, stat, writeFile } from "node:fs/promises";
import { createReadStream } from "node:fs";
import { dirname, isAbsolute, relative, resolve } from "node:path";
import { promisify } from "node:util";
import { fileURLToPath, pathToFileURL } from "node:url";

const run = promisify(execFile);
const moduleRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const prohibitedGameData = /\.(?:data|u|unr|utx|uax|umx|uz|uz2|exe)$/i;

async function git(root, argumentsList) {
	const result = await run("git", ["-C", root, ...argumentsList], { windowsHide: true, maxBuffer: 16 * 1024 * 1024 });
	return result.stdout.trim();
}

async function sha256File(path) {
	const hash = createHash("sha256");
	for await (const chunk of createReadStream(path)) hash.update(chunk);
	return hash.digest("hex");
}

async function fileRecord(root, path) {
	const absolute = resolve(root, path);
	const information = await stat(absolute);
	return { path, bytes: information.size, sha256: await sha256File(absolute) };
}

async function sourceIdentity(sourceRoot, requireClean = false) {
	const root = resolve(sourceRoot);
	const statusText = await git(root, ["status", "--porcelain=v1", "--untracked-files=all"]);
	if (requireClean && statusText)
		throw new Error("Refusing to archive a dirty source tree; commit or remove all changes first.");
	return Object.freeze({
		sourceCommit: await git(root, ["rev-parse", "HEAD"]),
		sourceTree: await git(root, ["rev-parse", "HEAD^{tree}"]),
		sourceDirty: !!statusText,
	});
}

async function trackedSurrealVideoFiles(sourceRoot) {
	return (await git(resolve(sourceRoot), ["ls-files", "SurrealVideo"]))
		.split(/\r?\n/).filter(Boolean).sort();
}

async function createCorrespondingSource(options = {}) {
	const sourceRoot = resolve(options.sourceRoot || moduleRoot);
	const outputArchive = resolve(options.outputArchive || "SurrealEngine-corresponding-source.tar.gz");
	const inside = relative(sourceRoot, outputArchive);
	if (inside === "" || (!inside.startsWith("..") && !isAbsolute(inside)))
		throw new Error("Corresponding-source output must be outside the source tree.");
	const identity = await sourceIdentity(sourceRoot, true);
	const { sourceCommit, sourceTree } = identity;
	const commitTimestamp = await git(sourceRoot, ["show", "-s", "--format=%cI", "HEAD"]);
	let repositoryUrl = null;
	try { repositoryUrl = await git(sourceRoot, ["remote", "get-url", "origin"]); } catch (_) {}
	const trackedVideo = await trackedSurrealVideoFiles(sourceRoot);
	const trackedFiles = (await git(sourceRoot, ["ls-files"])).split(/\r?\n/).filter(Boolean).sort();
	const prohibited = trackedFiles.filter(path => prohibitedGameData.test(path));
	if (prohibited.length) throw new Error("Refusing to archive prohibited game/binary data: " + prohibited.join(", "));
	for (const required of ["SurrealVideo/COPYING.LGPLv2.1", "SurrealVideo/COPYING.LGPLv3", "SurrealVideo/README.md"])
		if (!trackedVideo.includes(required)) throw new Error("Missing tracked SurrealVideo notice: " + required);
	await mkdir(dirname(outputArchive), { recursive: true });
	const prefix = "SurrealEngine-source-" + sourceCommit.slice(0, 12) + "/";
	await run("git", ["-C", sourceRoot, "archive", "--format=tar.gz", "--prefix=" + prefix,
		"--output=" + outputArchive, sourceCommit], { windowsHide: true, maxBuffer: 16 * 1024 * 1024 });
	await access(outputArchive);
	const archiveInfo = await stat(outputArchive);
	const metadata = {
		schema: "surrealengine-corresponding-source-v1",
		version: 1,
		archiveFile: outputArchive.split(/[\\/]/).at(-1),
		archivePrefix: prefix,
		archiveBytes: archiveInfo.size,
		archiveSha256: await sha256File(outputArchive),
		sourceCommit,
		sourceTree,
		commitTimestamp,
		repositoryUrl,
		buildInstructions: "Docs/BROWSER_STATIC_RELINKING.md",
		licenseFiles: ["SurrealVideo/COPYING.LGPLv2.1", "SurrealVideo/COPYING.LGPLv3", "SurrealVideo/README.md"],
		archiveScope: "complete-tracked-source-tree",
		trackedFileCount: trackedFiles.length,
		surrealVideoFiles: await Promise.all(trackedVideo.map(path => fileRecord(sourceRoot, path))),
	};
	const metadataPath = outputArchive + ".json";
	await writeFile(metadataPath, JSON.stringify(metadata, null, 2) + "\n");
	return Object.freeze({ outputArchive, metadataPath, metadata });
}

function commandLine(argumentsList) {
	const options = {};
	for (let index = 0; index < argumentsList.length; index += 2) {
		const name = argumentsList[index], value = argumentsList[index + 1];
		if (!value) throw new Error("Missing value for " + name);
		if (name === "--source-root") options.sourceRoot = value;
		else if (name === "--output") options.outputArchive = value;
		else throw new Error("Unknown argument: " + name);
	}
	return options;
}

export { createCorrespondingSource, sha256File, sourceIdentity, trackedSurrealVideoFiles };

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
	createCorrespondingSource(commandLine(process.argv.slice(2))).then(result => {
		process.stdout.write("Created " + result.outputArchive + "\n" +
			"SHA-256 " + result.metadata.archiveSha256 + "\n" +
			"Metadata " + result.metadataPath + "\n");
	}).catch(error => { process.stderr.write((error.stack || String(error)) + "\n"); process.exitCode = 1; });
}
