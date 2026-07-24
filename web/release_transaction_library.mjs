import { createHash, randomUUID } from "node:crypto";
import { execFile as execFileCallback } from "node:child_process";
import {
	cp, link, lstat, mkdir, open, readFile, readdir, realpath, rename, rm, stat,
} from "node:fs/promises";
import { basename, dirname, isAbsolute, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";

const SHA256 = /^[0-9a-f]{64}$/;
const BUILD_ID = /^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$/;
const execFile = promisify(execFileCallback);
const exchangeHelper = fileURLToPath(new URL("rename_exchange.py", import.meta.url));

async function syncHandle(handle) {
	try { await handle.sync(); }
	catch (error) {
		if (process.platform !== "win32" || !["EPERM", "EISDIR", "EINVAL"].includes(error?.code))
			throw error;
	}
}

export async function sha256File(filename) {
	const hash = createHash("sha256");
	const contents = await readFile(filename);
	return hash.update(contents).digest("hex");
}

function portablePath(value) {
	return typeof value === "string" && value.length > 0 &&
		!value.includes("\\") && !value.startsWith("/") &&
		value.split("/").every(part => part && part !== "." && part !== "..");
}

function inside(root, target) {
	const path = relative(root, target);
	return path === "" || (!isAbsolute(path) && path !== ".." && !path.startsWith(`..${sep}`));
}

async function exists(path) {
	try { await lstat(path); return true; }
	catch (error) {
		if (error && error.code === "ENOENT") return false;
		throw error;
	}
}

async function releaseFiles(root, ignoredTopLevelDirectories = new Set()) {
	const files = [];
	async function visit(directory, topLevel = false) {
		for (const entry of await readdir(directory, { withFileTypes: true })) {
			const absolute = join(directory, entry.name);
			if (entry.isSymbolicLink())
				throw new Error(`Release contains a symbolic link: ${relative(root, absolute)}`);
			if (entry.isDirectory() && topLevel && ignoredTopLevelDirectories.has(entry.name)) continue;
			if (entry.isDirectory()) await visit(absolute);
			else if (entry.isFile()) files.push(relative(root, absolute).split(sep).join("/"));
			else throw new Error(`Release contains an unsupported entry: ${relative(root, absolute)}`);
		}
	}
	await visit(root, true);
	return files.sort();
}

async function syncTree(root) {
	async function visit(directory) {
		for (const entry of await readdir(directory, { withFileTypes: true })) {
			const absolute = join(directory, entry.name);
			if (entry.isDirectory()) await visit(absolute);
			else if (entry.isFile()) {
				const handle = await open(absolute, "r");
				try { await syncHandle(handle); }
				finally { await handle.close(); }
			} else throw new Error(`Cannot make unsupported staged entry durable: ${absolute}`);
		}
		await syncDirectory(directory);
	}
	await visit(root);
}

export async function verifyRelease(releaseRoot, options = {}) {
	const rootPath = resolve(releaseRoot);
	const rootInformation = await lstat(rootPath);
	if (rootInformation.isSymbolicLink() || !rootInformation.isDirectory())
		throw new Error(`Release root must be a real directory: ${rootPath}`);
	const root = await realpath(rootPath);
	const manifestPath = join(root, "release-manifest.json");
	const manifestBytes = await readFile(manifestPath);
	const manifestSha256 = createHash("sha256").update(manifestBytes).digest("hex");
	if (options.manifestSha256 && manifestSha256 !== options.manifestSha256)
		throw new Error("Release manifest does not match the required SHA-256.");

	let manifest;
	try { manifest = JSON.parse(manifestBytes.toString("utf8")); }
	catch { throw new Error("Release manifest is invalid JSON."); }
	if (manifest.schema !== "surrealengine-browser-release-v2" || manifest.version !== 2 ||
		!BUILD_ID.test(manifest.buildId || "") || !Array.isArray(manifest.files) || !manifest.files.length)
		throw new Error("Release manifest has an unsupported schema or identity.");
	if (options.intendedBasePath && manifest.intendedBasePath !== options.intendedBasePath)
		throw new Error("Release intended base path does not match the promotion target.");

	const records = new Map();
	for (const record of manifest.files) {
		if (!record || !portablePath(record.path) || records.has(record.path) ||
			!Number.isSafeInteger(record.bytes) || record.bytes < 0 || !SHA256.test(record.sha256 || ""))
			throw new Error("Release manifest contains an invalid file record.");
		records.set(record.path, record);
	}
	const ignoredTopLevelDirectories = new Set(options.ignoreTopLevelDirectories || []);
	if ([...ignoredTopLevelDirectories].some(name => !portablePath(name) || name.includes("/")))
		throw new Error("Ignored top-level release directory names must be safe single path segments.");
	const actual = (await releaseFiles(root, ignoredTopLevelDirectories))
		.filter(path => path !== "release-manifest.json");
	const expected = [...records.keys()].sort();
	if (actual.length !== expected.length || actual.some((path, index) => path !== expected[index]))
		throw new Error("Release file set does not match its manifest.");
	for (const path of expected) {
		const absolute = join(root, ...path.split("/"));
		const canonical = await realpath(absolute);
		if (!inside(root, canonical)) throw new Error(`Release file escaped its root: ${path}`);
		const information = await stat(canonical);
		const record = records.get(path);
		if (!information.isFile() || information.size !== record.bytes ||
			await sha256File(canonical) !== record.sha256)
			throw new Error(`Release file failed integrity verification: ${path}`);
	}
	return Object.freeze({
		root, buildId: manifest.buildId, manifestSha256, intendedBasePath: manifest.intendedBasePath,
		sourceCommit: manifest.sourceCompliance?.sourceCommit || null,
		sourceTree: manifest.sourceCompliance?.sourceTree || null,
		verifiedFiles: expected.length,
	});
}

function safeLiveRelative(value) {
	if (!portablePath(value)) throw new Error("Live path must be a safe relative path.");
	return value;
}

async function deploymentPaths(siteRoot, liveRelative) {
	const rootPath = resolve(siteRoot);
	const information = await lstat(rootPath);
	if (information.isSymbolicLink() || !information.isDirectory())
		throw new Error("Site root must be a real directory.");
	const root = await realpath(rootPath);
	const liveParts = safeLiveRelative(liveRelative).split("/");
	let parent = root;
	for (const part of liveParts.slice(0, -1)) {
		parent = join(parent, part);
		const parentInformation = await lstat(parent);
		if (parentInformation.isSymbolicLink() || !parentInformation.isDirectory() ||
			!inside(root, await realpath(parent)))
			throw new Error("Live path parents must be real directories inside the site root.");
	}
	const live = resolve(root, ...liveParts);
	if (!inside(root, live) || live === root) throw new Error("Live path escaped the site root.");
	const name = basename(live);
	const scope = createHash("sha256").update(liveParts.join("/")).digest("hex").slice(0, 12);
	const prefix = `.${name}-${scope}`;
	return {
		root, live, name, prefix, liveRelative: liveParts.join("/"),
		lock: join(root, `${prefix}.release-lock`),
		journal: join(root, `${prefix}.release-transaction.json`),
		completion: join(root, `${prefix}.release-last-completion.json`),
	};
}

async function syncDirectory(path) {
	let handle;
	try {
		handle = await open(path, "r");
		await syncHandle(handle);
	} catch (error) {
		if (process.platform !== "win32" || !["EPERM", "EISDIR", "EINVAL"].includes(error?.code))
			throw error;
	} finally { await handle?.close(); }
}

async function durableRename(from, to) {
	await rename(from, to);
	const directories = new Set([dirname(from), dirname(to)]);
	for (const directory of directories) await syncDirectory(directory);
}

async function durableRemoveFile(path) {
	await rm(path, { force: true });
	await syncDirectory(dirname(path));
}

async function atomicWriteJson(path, value) {
	await mkdir(dirname(path), { recursive: true });
	const temporary = `${path}.tmp-${process.pid}-${randomUUID()}`;
	let handle;
	try {
		handle = await open(temporary, "wx");
		await handle.writeFile(`${JSON.stringify(value, null, 2)}\n`, "utf8");
		await syncHandle(handle);
		await handle.close();
		handle = null;
		await durableRename(temporary, path);
	} finally {
		await handle?.close();
		await rm(temporary, { force: true });
	}
}

async function withLock(paths, callback) {
	const ownerTemporary = `${paths.lock}.owner-${process.pid}-${randomUUID()}`;
	let acquired = false;
	try {
		await atomicWriteJson(ownerTemporary, {
			pid: process.pid, startedAt: new Date().toISOString(),
			linuxIdentity: await linuxProcessIdentity(process.pid),
		});
		try { await link(ownerTemporary, paths.lock); }
		catch (error) {
			if (error && error.code === "EEXIST")
				throw new Error(`Another release transaction owns ${paths.lock}.`);
			throw error;
		}
		acquired = true;
		await syncDirectory(paths.root);
		await durableRemoveFile(ownerTemporary);
		return await callback();
	} finally {
		if (acquired) await durableRemoveFile(paths.lock);
		await durableRemoveFile(ownerTemporary);
	}
}

function processAlive(pid) {
	if (!Number.isSafeInteger(pid) || pid <= 0) return false;
	try { process.kill(pid, 0); return true; }
	catch (error) { return error && error.code === "EPERM"; }
}

async function linuxProcessIdentity(pid) {
	if (process.platform !== "linux") return null;
	try {
		const [bootId, processStat] = await Promise.all([
			readFile("/proc/sys/kernel/random/boot_id", "utf8"),
			readFile(`/proc/${pid}/stat`, "utf8"),
		]);
		const close = processStat.lastIndexOf(")");
		const fields = processStat.slice(close + 2).trim().split(/\s+/);
		const startTicks = fields[19];
		if (close < 0 || !/^[0-9]+$/.test(startTicks || "")) return null;
		return { bootId: bootId.trim(), startTicks };
	} catch { return null; }
}

async function preserveDeadLock(paths, allowWithoutJournal = false) {
	if (!(await exists(paths.lock))) return null;
	const lockInformation = await lstat(paths.lock);
	if (lockInformation.isSymbolicLink() || !lockInformation.isFile())
		throw new Error("Refusing to break a release lock that is not a real regular file.");
	if (!(await exists(paths.journal)) && !allowWithoutJournal)
		throw new Error("Refusing to break a release lock without an unfinished journal.");
	let owner;
	try { owner = JSON.parse(await readFile(paths.lock, "utf8")); }
	catch { throw new Error("Refusing to break a release lock without a readable owner record."); }
	if (processAlive(owner.pid)) {
		const currentIdentity = await linuxProcessIdentity(owner.pid);
		if (!owner.linuxIdentity || !currentIdentity ||
			(owner.linuxIdentity.bootId === currentIdentity.bootId &&
			 owner.linuxIdentity.startTicks === currentIdentity.startTicks))
			throw new Error(`Release transaction process ${owner.pid} is still alive.`);
	}
	const preservedName = `${paths.prefix}.stale-lock-${timestamp(new Date())}-${randomUUID()}`;
	await durableRename(paths.lock, join(paths.root, preservedName));
	return preservedName;
}

function filenameToken(value) {
	if (!BUILD_ID.test(value || "")) throw new Error("Release build ID is unsafe for a generation name.");
	return value;
}

function timestamp(date = new Date()) {
	return date.toISOString().replace(/[-:]/g, "").replace(".", "").replace(/Z$/, "Z");
}

async function exchangeDirectories(left, right, options) {
	if (process.platform !== "linux" && !options.exchangeCommand)
		throw new Error("Atomic directory exchange requires Linux renameat2.");
	await execFile(options.exchangeCommand || "python3", [exchangeHelper, left, right], {
		timeout: 30000,
	});
}

function managedName(paths, kind, identity, date) {
	return `${paths.prefix}.${kind}-${filenameToken(identity)}-${timestamp(date)}-${randomUUID()}`;
}

function directManagedPath(paths, name, kind) {
	if (typeof name !== "string" || name.includes("/") || name.includes("\\") ||
		!name.startsWith(`${paths.prefix}.${kind}-`))
		throw new Error(`Invalid ${kind} generation name.`);
	const target = join(paths.root, name);
	if (!inside(paths.root, target)) throw new Error("Managed generation escaped the site root.");
	return target;
}

async function writeRecord(recordPath, record) {
	if (recordPath) await atomicWriteJson(resolve(recordPath), record);
	return Object.freeze(record);
}

export async function stageRelease(options) {
	const paths = await deploymentPaths(options.siteRoot, options.liveRelative);
	return withLock(paths, async () => {
		if (await exists(paths.journal)) throw new Error("An unfinished release journal requires recovery.");
		const candidate = await verifyRelease(options.candidate, {
			manifestSha256: options.manifestSha256,
			intendedBasePath: options.intendedBasePath,
		});
		if (inside(paths.root, candidate.root))
			throw new Error("Candidate source must be outside the managed site root.");
		const stageName = `${paths.prefix}.stage-${filenameToken(candidate.buildId)}`;
		const stage = join(paths.root, stageName);
		if (await exists(stage)) {
			const existing = await verifyRelease(stage, {
				manifestSha256: candidate.manifestSha256,
				intendedBasePath: options.intendedBasePath,
			});
			return writeRecord(options.recordPath, {
				schema: "surrealengine-release-transaction-v1", operation: "stage",
				completedAt: new Date().toISOString(), alreadyStaged: true,
				stageName, candidate: existing,
			});
		}
		const partial = join(paths.root, `${paths.prefix}.partial-${filenameToken(candidate.buildId)}-${randomUUID()}`);
		try {
			await cp(candidate.root, partial, { recursive: true, errorOnExist: true, force: false });
			await verifyRelease(partial, {
				manifestSha256: candidate.manifestSha256,
				intendedBasePath: options.intendedBasePath,
			});
			await syncTree(partial);
			await durableRename(partial, stage);
		} finally {
			await rm(partial, { recursive: true, force: true });
		}
		return writeRecord(options.recordPath, {
			schema: "surrealengine-release-transaction-v1", operation: "stage",
			completedAt: new Date().toISOString(), alreadyStaged: false,
			stageName, candidate,
		});
	});
}

function validIntendedBasePath(value) {
	return typeof value === "string" && value.startsWith("/") && value.endsWith("/") &&
		!value.includes("\\") && !value.includes("?") && !value.includes("#") &&
		value.slice(1, -1).split("/").every(part => part && part !== "." && part !== "..");
}

async function readJournal(paths, options) {
	let journal;
	try {
		const information = await lstat(paths.journal);
		if (information.isSymbolicLink() || !information.isFile())
			throw new Error("Release journal must be a real regular file.");
		journal = JSON.parse(await readFile(paths.journal, "utf8"));
	}
	catch (error) {
		if (error && error.code === "ENOENT") throw new Error("No unfinished release journal exists.");
		throw new Error("Release journal is unreadable.");
	}
	if (journal?.schema !== "surrealengine-release-journal-v1" ||
		!["promote", "rollback"].includes(journal.operation) ||
		!["journaled", "exchange"].includes(journal.swapMode || "journaled") ||
		!["prepared", "live-moved", "incoming-moved", "exchanged", "incoming-archived"].includes(journal.state) ||
		journal.liveRelative !== paths.liveRelative ||
		journal.intendedBasePath !== options.intendedBasePath ||
		!validIntendedBasePath(journal.intendedBasePath) ||
		!BUILD_ID.test(journal.incomingBuildId || "") ||
		!SHA256.test(journal.incomingManifestSha256 || "") ||
		!BUILD_ID.test(journal.liveBefore?.buildId || "") ||
		!SHA256.test(journal.liveBefore?.manifestSha256 || ""))
		throw new Error("Release journal has an unsupported schema or state.");
	directManagedPath(paths, journal.incomingName,
		journal.operation === "promote" ? "stage" : "rollback");
	directManagedPath(paths, journal.displacedName,
		journal.operation === "promote" ? "rollback" : "failed");
	return journal;
}

async function verifiedOrNull(path, manifestSha256, intendedBasePath) {
	if (!(await exists(path))) return null;
	try { return await verifyRelease(path, { manifestSha256, intendedBasePath }); }
	catch (error) {
		if (error?.code) throw error;
		return null;
	}
}

async function recoverExchangeUnlocked(paths, journal, incoming, displaced, options) {
	const liveAsPrevious = await verifiedOrNull(paths.live,
		journal.liveBefore.manifestSha256, journal.intendedBasePath);
	const liveAsIncoming = await verifiedOrNull(paths.live,
		journal.incomingManifestSha256, journal.intendedBasePath);
	const incomingAsPrevious = await verifiedOrNull(incoming,
		journal.liveBefore.manifestSha256, journal.intendedBasePath);
	const incomingAsIncoming = await verifiedOrNull(incoming,
		journal.incomingManifestSha256, journal.intendedBasePath);
	const displacedAsPrevious = await verifiedOrNull(displaced,
		journal.liveBefore.manifestSha256, journal.intendedBasePath);
	let action;
	let liveManifestSha256;
	if (liveAsPrevious && incomingAsIncoming && !(await exists(displaced))) {
		action = "aborted-before-atomic-exchange";
		liveManifestSha256 = journal.liveBefore.manifestSha256;
	} else if (liveAsIncoming && incomingAsPrevious && !(await exists(displaced))) {
		await durableRename(incoming, displaced);
		action = "finalized-atomic-exchange";
		liveManifestSha256 = journal.incomingManifestSha256;
	} else if (liveAsIncoming && !(await exists(incoming)) && displacedAsPrevious) {
		action = "finalized-archived-atomic-exchange";
		liveManifestSha256 = journal.incomingManifestSha256;
	} else if (!liveAsPrevious && !liveAsIncoming && incomingAsPrevious && !(await exists(displaced))) {
		await exchangeDirectories(paths.live, incoming, options);
		const restored = await verifyRelease(paths.live, {
			manifestSha256: journal.liveBefore.manifestSha256,
			intendedBasePath: journal.intendedBasePath,
		});
		if (!restored) throw new Error("Atomic exchange recovery did not restore the previous release.");
		const rejectedName = managedName(paths, "rejected", journal.incomingBuildId, new Date());
		await durableRename(incoming, join(paths.root, rejectedName));
		journal.rejectedName = rejectedName;
		action = "rejected-incoming-and-reversed-atomic-exchange";
		liveManifestSha256 = journal.liveBefore.manifestSha256;
	} else throw new Error("Atomic release journal does not match a recoverable filesystem state.");
	return { action, liveManifestSha256 };
}

async function recoverJournaledUnlocked(paths, journal, incoming, displaced) {
	const live = paths.live;
	const liveExists = await exists(live);
	const incomingExists = await exists(incoming);
	const displacedExists = await exists(displaced);
	let action;
	let liveManifestSha256;
	if (liveExists && incomingExists && !displacedExists) {
		await verifyRelease(live, {
			manifestSha256: journal.liveBefore.manifestSha256,
			intendedBasePath: journal.intendedBasePath,
		});
		await verifyRelease(incoming, {
			manifestSha256: journal.incomingManifestSha256,
			intendedBasePath: journal.intendedBasePath,
		});
		action = "aborted-before-live-move";
		liveManifestSha256 = journal.liveBefore.manifestSha256;
	} else if (!liveExists && incomingExists && displacedExists) {
		await verifyRelease(incoming, {
			manifestSha256: journal.incomingManifestSha256,
			intendedBasePath: journal.intendedBasePath,
		});
		await verifyRelease(displaced, {
			manifestSha256: journal.liveBefore.manifestSha256,
			intendedBasePath: journal.intendedBasePath,
		});
		await durableRename(displaced, live);
		action = "restored-displaced-generation";
		liveManifestSha256 = journal.liveBefore.manifestSha256;
	} else if (liveExists && !incomingExists && displacedExists) {
		await verifyRelease(displaced, {
			manifestSha256: journal.liveBefore.manifestSha256,
			intendedBasePath: journal.intendedBasePath,
		});
		try {
			await verifyRelease(live, {
				manifestSha256: journal.incomingManifestSha256,
				intendedBasePath: journal.intendedBasePath,
			});
			action = "finalized-verified-generation";
			liveManifestSha256 = journal.incomingManifestSha256;
		} catch (verificationError) {
			const rejectedName = managedName(paths, "rejected", journal.incomingBuildId, new Date());
			await durableRename(live, join(paths.root, rejectedName));
			await durableRename(displaced, live);
			action = "rejected-incoming-and-restored-displaced-generation";
			journal.rejectedName = rejectedName;
			journal.verificationError = verificationError.message;
			liveManifestSha256 = journal.liveBefore.manifestSha256;
		}
	} else throw new Error("Release journal does not match a recoverable filesystem state.");
	return { action, liveManifestSha256 };
}

async function recoverUnlocked(paths, options = {}) {
	const journal = await readJournal(paths, options);
	const incoming = directManagedPath(paths, journal.incomingName,
		journal.operation === "promote" ? "stage" : "rollback");
	const displaced = directManagedPath(paths, journal.displacedName,
		journal.operation === "promote" ? "rollback" : "failed");
	const recovery = (journal.swapMode || "journaled") === "exchange" ?
		await recoverExchangeUnlocked(paths, journal, incoming, displaced, options) :
		await recoverJournaledUnlocked(paths, journal, incoming, displaced);
	const live = await verifyRelease(paths.live, {
		manifestSha256: recovery.liveManifestSha256,
		intendedBasePath: journal.intendedBasePath,
	});
	await atomicWriteJson(paths.completion, {
		schema: "surrealengine-release-completion-v1", completedAt: new Date().toISOString(),
		operation: "recover", action: recovery.action, journal, live,
	});
	await durableRemoveFile(paths.journal);
	return { journal, action: recovery.action, live };
}

export async function recoverRelease(options) {
	const paths = await deploymentPaths(options.siteRoot, options.liveRelative);
	if (options.breakStaleLock && await exists(paths.lock) && !(await exists(paths.journal))) {
		if (!options.liveManifestSha256)
			throw new Error("Recovering a dead staging lock requires the expected live manifest SHA-256.");
		await verifyRelease(paths.live, {
			manifestSha256: options.liveManifestSha256,
			intendedBasePath: options.intendedBasePath,
		});
	}
	const preservedStaleLock = options.breakStaleLock ? await preserveDeadLock(paths, true) : null;
	return withLock(paths, async () => {
		if (!(await exists(paths.journal)) && preservedStaleLock) {
			if (!options.liveManifestSha256)
				throw new Error("Recovering a dead staging lock requires the expected live manifest SHA-256.");
			const live = await verifyRelease(paths.live, {
				manifestSha256: options.liveManifestSha256,
				intendedBasePath: options.intendedBasePath,
			});
			return writeRecord(options.recordPath, {
				schema: "surrealengine-release-transaction-v1", operation: "recover",
				completedAt: new Date().toISOString(), action: "cleared-dead-staging-lock",
				journal: null, preservedStaleLock, live,
			});
		}
		const recovered = await recoverUnlocked(paths, options);
		return writeRecord(options.recordPath, {
			schema: "surrealengine-release-transaction-v1", operation: "recover",
			completedAt: new Date().toISOString(), action: recovered.action,
			journal: recovered.journal, preservedStaleLock, live: recovered.live,
		});
	});
}

async function moveGeneration(options, operation) {
	const paths = await deploymentPaths(options.siteRoot, options.liveRelative);
	return withLock(paths, async () => {
		const swapMode = options.swapMode || "journaled";
		if (!["journaled", "exchange"].includes(swapMode))
			throw new Error("Swap mode must be journaled or exchange.");
		if (await exists(paths.journal)) throw new Error("An unfinished release journal requires recovery.");
		if (!(await exists(paths.live))) throw new Error("The stable live release does not exist.");
		const liveBefore = await verifyRelease(paths.live, {
			manifestSha256: options.liveManifestSha256,
			intendedBasePath: options.intendedBasePath,
		});
		const incomingKind = operation === "promote" ? "stage" : "rollback";
		const incomingName = options.incomingName;
		const incoming = directManagedPath(paths, incomingName, incomingKind);
		const incomingIdentity = await verifyRelease(incoming, {
			manifestSha256: options.manifestSha256,
			intendedBasePath: options.intendedBasePath,
		});
		if (incomingIdentity.manifestSha256 === liveBefore.manifestSha256)
			throw new Error("Incoming and live releases are the same generation.");
		const displacedKind = operation === "promote" ? "rollback" : "failed";
		const displacedName = managedName(paths, displacedKind, liveBefore.buildId, options.date || new Date());
		const displaced = join(paths.root, displacedName);
		if (await exists(displaced)) throw new Error("The generated displaced path already exists.");
		const journal = {
			schema: "surrealengine-release-journal-v1", operation, swapMode, state: "prepared",
			createdAt: new Date().toISOString(), liveRelative: options.liveRelative,
			incomingName, displacedName, intendedBasePath: options.intendedBasePath,
			incomingBuildId: incomingIdentity.buildId,
			incomingManifestSha256: incomingIdentity.manifestSha256,
			liveBefore,
		};
		await atomicWriteJson(paths.journal, journal);
		try {
			if (swapMode === "exchange") {
				await exchangeDirectories(paths.live, incoming, options);
				if (options.transitionHook)
					await options.transitionHook("directories-exchanged", { paths, journal });
				journal.state = "exchanged";
				await atomicWriteJson(paths.journal, journal);
				await verifyRelease(paths.live, {
					manifestSha256: incomingIdentity.manifestSha256,
					intendedBasePath: options.intendedBasePath,
				});
				await verifyRelease(incoming, {
					manifestSha256: liveBefore.manifestSha256,
					intendedBasePath: options.intendedBasePath,
				});
				await durableRename(incoming, displaced);
				if (options.transitionHook)
					await options.transitionHook("incoming-archived", { paths, journal });
				journal.state = "incoming-archived";
				await atomicWriteJson(paths.journal, journal);
			} else {
				await durableRename(paths.live, displaced);
				if (options.transitionHook) await options.transitionHook("live-renamed", { paths, journal });
				journal.state = "live-moved";
				await atomicWriteJson(paths.journal, journal);
				if (options.transitionHook) await options.transitionHook("live-moved", { paths, journal });
				await durableRename(incoming, paths.live);
				if (options.transitionHook) await options.transitionHook("incoming-renamed", { paths, journal });
				journal.state = "incoming-moved";
				await atomicWriteJson(paths.journal, journal);
				if (options.transitionHook) await options.transitionHook("incoming-moved", { paths, journal });
			}
			const liveAfter = await verifyRelease(paths.live, {
				manifestSha256: incomingIdentity.manifestSha256,
				intendedBasePath: options.intendedBasePath,
			});
			const completion = {
				schema: "surrealengine-release-transaction-v1", operation,
				completedAt: new Date().toISOString(), swapMode, incomingName, displacedName,
				liveBefore, liveAfter,
			};
			await atomicWriteJson(paths.completion, {
				...completion, schema: "surrealengine-release-completion-v1",
			});
			await durableRemoveFile(paths.journal);
			return writeRecord(options.recordPath, completion);
		} catch (error) {
			if (options.recoverOnFailure !== false && await exists(paths.journal))
				await recoverUnlocked(paths, options);
			throw error;
		}
	});
}

export async function promoteRelease(options) {
	return moveGeneration({ ...options, incomingName: options.stageName }, "promote");
}

export async function rollbackRelease(options) {
	return moveGeneration({ ...options, incomingName: options.rollbackName }, "rollback");
}
