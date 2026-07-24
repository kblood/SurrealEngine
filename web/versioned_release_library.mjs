import { createHash, randomUUID } from "node:crypto";
import {
	cp, link, lstat, mkdir, open, readFile, readdir, realpath, rename, rm,
} from "node:fs/promises";
import { basename, dirname, isAbsolute, join, relative, resolve, sep } from "node:path";
import { verifyRelease } from "./release_transaction_library.mjs";

const SHA256 = /^[0-9a-f]{64}$/;
const BUILD_ID = /^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$/;
const POINTER_BEGIN = "# BEGIN SURREALENGINE VERSION POINTER";
const POINTER_END = "# END SURREALENGINE VERSION POINTER";

function sha256(value) {
	return createHash("sha256").update(value).digest("hex");
}

async function exists(path) {
	try { await lstat(path); return true; }
	catch (error) {
		if (error?.code === "ENOENT") return false;
		throw error;
	}
}

async function requireRegularFile(path, label) {
	const information = await lstat(path);
	if (information.isSymbolicLink() || !information.isFile())
		throw new Error(`${label} must be a real regular file.`);
}

function inside(root, target) {
	const path = relative(root, target);
	return path === "" || (!isAbsolute(path) && path !== ".." && !path.startsWith(`..${sep}`));
}

function safeRelative(value) {
	if (typeof value !== "string" || !value || value.includes("\\") || value.startsWith("/") ||
		value.split("/").some(part => !part || part === "." || part === ".."))
		throw new Error("Live path must be a safe relative path.");
	return value;
}

function safeBasePath(value) {
	if (typeof value !== "string" || !value.startsWith("/") || !value.endsWith("/") ||
		value.includes("\\") || value.includes("?") || value.includes("#") ||
		value.slice(1, -1).split("/").some(part => !part || part === "." || part === ".." ||
			!/^[A-Za-z0-9._~-]+$/.test(part)))
		throw new Error("Intended base path must be a safe absolute URL directory path.");
	return value;
}

function requireSha(value, label) {
	if (!SHA256.test(value || "")) throw new Error(`${label} must be a lowercase SHA-256.`);
	return value;
}

async function pathsFor(options) {
	const rootPath = resolve(options.siteRoot);
	const rootInfo = await lstat(rootPath);
	if (rootInfo.isSymbolicLink() || !rootInfo.isDirectory())
		throw new Error("Site root must be a real directory.");
	const root = await realpath(rootPath);
	const liveRelative = safeRelative(options.liveRelative || "Ports/SurrealEngine");
	let parent = root;
	for (const part of liveRelative.split("/").slice(0, -1)) {
		parent = join(parent, part);
		const info = await lstat(parent);
		if (info.isSymbolicLink() || !info.isDirectory() || !inside(root, await realpath(parent)))
			throw new Error("Stable path parents must be real directories inside the site root.");
	}
	const live = resolve(root, ...liveRelative.split("/"));
	const liveInfo = await lstat(live);
	if (!inside(root, live) || live === root || liveInfo.isSymbolicLink() || !liveInfo.isDirectory())
		throw new Error("Stable path must be a real directory inside the site root.");
	const name = basename(live);
	const scope = sha256(liveRelative).slice(0, 12);
	const prefix = `.${name}-${scope}`;
	return {
		root, live, liveRelative, prefix,
		releases: join(live, "releases"),
		htaccess: join(live, ".htaccess"),
		lock: join(root, `${prefix}.release-lock`),
		legacyJournal: join(root, `${prefix}.release-transaction.json`),
		journal: join(root, `${prefix}.version-pointer-transaction.json`),
		completion: join(root, `${prefix}.version-pointer-last-completion.json`),
	};
}

async function syncHandle(handle) {
	try { await handle.sync(); }
	catch (error) {
		if (process.platform !== "win32" || !["EPERM", "EISDIR", "EINVAL"].includes(error?.code))
			throw error;
	}
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

async function syncTree(root) {
	for (const entry of await readdir(root, { withFileTypes: true })) {
		const path = join(root, entry.name);
		if (entry.isSymbolicLink()) throw new Error(`Cannot publish a symbolic link: ${path}`);
		if (entry.isDirectory()) await syncTree(path);
		else if (entry.isFile()) {
			const handle = await open(path, "r");
			try { await syncHandle(handle); }
			finally { await handle.close(); }
		} else throw new Error(`Cannot publish an unsupported entry: ${path}`);
	}
	await syncDirectory(root);
}

async function durableRename(from, to) {
	for (let attempt = 0; ; attempt++) {
		try { await rename(from, to); break; }
		catch (error) {
			if (process.platform !== "win32" || !["EPERM", "EBUSY"].includes(error?.code) || attempt >= 19)
				throw error;
			await new Promise(resolvePromise => setTimeout(resolvePromise, 5 * (attempt + 1)));
		}
	}
	for (const directory of new Set([dirname(from), dirname(to)])) await syncDirectory(directory);
}

async function durableRemove(path) {
	await rm(path, { force: true, recursive: true });
	await syncDirectory(dirname(path));
}

async function atomicWrite(path, contents, preserveMode = false) {
	await mkdir(dirname(path), { recursive: true });
	const temporary = `${path}.tmp-${process.pid}-${randomUUID()}`;
	let handle;
	let mode = null;
	if (preserveMode && await exists(path)) {
		const information = await lstat(path);
		if (information.isSymbolicLink() || !information.isFile())
			throw new Error("Refusing to replace a hosting pointer that is not a real regular file.");
		mode = information.mode & 0o777;
	}
	try {
		handle = await open(temporary, "wx");
		await handle.writeFile(contents);
		if (mode !== null) await handle.chmod(mode);
		await syncHandle(handle);
		await handle.close();
		handle = null;
		await durableRename(temporary, path);
	} finally {
		await handle?.close();
		await rm(temporary, { force: true });
	}
}

async function atomicJson(path, value) {
	await atomicWrite(path, `${JSON.stringify(value, null, 2)}\n`);
}

async function withLock(paths, callback) {
	const temporary = `${paths.lock}.owner-${process.pid}-${randomUUID()}`;
	let acquired = false;
	try {
		await atomicJson(temporary, {
			pid: process.pid, startedAt: new Date().toISOString(),
			linuxIdentity: await linuxProcessIdentity(process.pid),
		});
		try { await link(temporary, paths.lock); }
		catch (error) {
			if (error?.code === "EEXIST") throw new Error(`Another release transaction owns ${paths.lock}.`);
			throw error;
		}
		acquired = true;
		await syncDirectory(paths.root);
		await durableRemove(temporary);
		return await callback();
	} finally {
		if (acquired) await durableRemove(paths.lock);
		await durableRemove(temporary);
	}
}

function processAlive(pid) {
	if (!Number.isSafeInteger(pid) || pid <= 0) return false;
	try { process.kill(pid, 0); return true; }
	catch (error) { return error?.code === "EPERM"; }
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

async function preserveDeadLock(paths) {
	await requireRegularFile(paths.lock, "Release lock");
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
	const preservedName = `${paths.prefix}.stale-lock-${new Date().toISOString().replace(/[-:.]/g, "")}-${randomUUID()}`;
	await durableRename(paths.lock, join(paths.root, preservedName));
	return preservedName;
}

function generationUrl(intendedBasePath, manifestSha256) {
	return `${safeBasePath(intendedBasePath)}releases/${requireSha(manifestSha256, "Manifest hash")}/`;
}

function apacheRegex(value) {
	return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function pointerNoStoreRules(intendedBasePath) {
	const path = apacheRegex(safeBasePath(intendedBasePath));
	return `<IfModule mod_headers.c>\n  Header always set Cache-Control "no-store" "expr=%{REQUEST_URI} =~ m#^${path}(?:index\\.html)?$#"\n</IfModule>\n`;
}

function pointerBlock(identity, intendedBasePath) {
	if (!BUILD_ID.test(identity.buildId || "")) throw new Error("Pointer build ID is invalid.");
	const manifestSha256 = requireSha(identity.manifestSha256, "Pointer manifest hash");
	const target = generationUrl(intendedBasePath, manifestSha256);
	return `${POINTER_BEGIN}\n# schema: surrealengine-version-pointer-v1\n# manifest-sha256: ${manifestSha256}\n# build-id: ${identity.buildId}\n# target: ${target}\n<IfModule mod_rewrite.c>\n  RewriteEngine On\n  RewriteRule ^(?:index\\.html)?$ ${target} [R=302,L,NE]\n</IfModule>\n${POINTER_END}\n`;
}

export function renderVersionPointer(identity, intendedBasePath, compatibilityRules) {
	if (typeof compatibilityRules !== "string" || !compatibilityRules.trim())
		throw new Error("Compatibility hosting rules are required.");
	return `${pointerBlock(identity, intendedBasePath)}${compatibilityRules.replace(/^\n+/, "").replace(/\s*$/, "\n")}`;
}

export function parseVersionPointer(contents, intendedBasePath) {
	if (typeof contents !== "string" || !contents.startsWith(`${POINTER_BEGIN}\n`)) return null;
	const end = contents.indexOf(`${POINTER_END}\n`);
	if (end < 0) throw new Error("Stable version pointer is truncated.");
	const prefix = contents.slice(0, end + POINTER_END.length + 1);
	const compatibilityRules = contents.slice(prefix.length);
	const manifest = prefix.match(/^# manifest-sha256: ([0-9a-f]{64})$/m)?.[1];
	const buildId = prefix.match(/^# build-id: ([A-Za-z0-9][A-Za-z0-9._-]{0,127})$/m)?.[1];
	if (!manifest || !buildId || prefix !== pointerBlock({ manifestSha256: manifest, buildId }, intendedBasePath))
		throw new Error("Stable version pointer is invalid or targets an unexpected base path.");
	if (!compatibilityRules.trim()) throw new Error("Stable version pointer lost its compatibility hosting rules.");
	return Object.freeze({ manifestSha256: manifest, buildId, target: generationUrl(intendedBasePath, manifest), compatibilityRules, contents });
}

async function readPointer(paths, intendedBasePath) {
	await requireRegularFile(paths.htaccess, "Stable hosting pointer");
	return parseVersionPointer(await readFile(paths.htaccess, "utf8"), intendedBasePath);
}

async function ensureReleasesDirectory(paths) {
	await mkdir(paths.releases, { recursive: true });
	const info = await lstat(paths.releases);
	if (info.isSymbolicLink() || !info.isDirectory() || !inside(paths.live, await realpath(paths.releases)))
		throw new Error("Published releases root must be a real directory inside the stable root.");
	await syncDirectory(paths.live);
}

function generationPath(paths, manifestSha256) {
	return join(paths.releases, requireSha(manifestSha256, "Generation manifest hash"));
}

async function verifyGeneration(paths, manifestSha256, intendedBasePath) {
	return verifyRelease(generationPath(paths, manifestSha256), { manifestSha256, intendedBasePath });
}

async function publishCandidateUnlocked(paths, candidateRoot, expectedManifestSha256, intendedBasePath) {
	const candidate = await verifyRelease(candidateRoot, {
		manifestSha256: expectedManifestSha256, intendedBasePath,
	});
	if (inside(paths.root, candidate.root)) throw new Error("Candidate must be outside the managed site root.");
	await ensureReleasesDirectory(paths);
	const destination = generationPath(paths, candidate.manifestSha256);
	if (await exists(destination)) {
		const published = await verifyGeneration(paths, candidate.manifestSha256, intendedBasePath);
		return { identity: published, alreadyPublished: true };
	}
	const partial = join(paths.root, `${paths.prefix}.version-partial-${candidate.manifestSha256}-${randomUUID()}`);
	try {
		await cp(candidate.root, partial, { recursive: true, errorOnExist: true, force: false });
		await verifyRelease(partial, { manifestSha256: candidate.manifestSha256, intendedBasePath });
		await syncTree(partial);
		await durableRename(partial, destination);
	} finally { await rm(partial, { recursive: true, force: true }); }
	return { identity: await verifyGeneration(paths, candidate.manifestSha256, intendedBasePath), alreadyPublished: false };
}

async function publishCompatibilityUnlocked(paths, compatibility, intendedBasePath) {
	await ensureReleasesDirectory(paths);
	const destination = generationPath(paths, compatibility.manifestSha256);
	if (await exists(destination)) {
		return { identity: await verifyGeneration(paths, compatibility.manifestSha256, intendedBasePath), alreadyPublished: true };
	}
	const manifest = JSON.parse(await readFile(join(paths.live, "release-manifest.json"), "utf8"));
	const partial = join(paths.root, `${paths.prefix}.bootstrap-partial-${compatibility.manifestSha256}-${randomUUID()}`);
	try {
		await mkdir(partial);
		for (const record of manifest.files) {
			const source = join(paths.live, ...record.path.split("/"));
			const destinationFile = join(partial, ...record.path.split("/"));
			await mkdir(dirname(destinationFile), { recursive: true });
			await cp(source, destinationFile, { errorOnExist: true, force: false });
		}
		await cp(join(paths.live, "release-manifest.json"), join(partial, "release-manifest.json"), {
			errorOnExist: true, force: false,
		});
		await verifyRelease(partial, { manifestSha256: compatibility.manifestSha256, intendedBasePath });
		await syncTree(partial);
		await durableRename(partial, destination);
	} finally { await rm(partial, { recursive: true, force: true }); }
	return { identity: await verifyGeneration(paths, compatibility.manifestSha256, intendedBasePath), alreadyPublished: false };
}

async function writeRecord(path, record) {
	if (path) await atomicJson(resolve(path), record);
	return Object.freeze(record);
}

async function refuseLegacyJournal(paths) {
	if (await exists(paths.legacyJournal))
		throw new Error("An unfinished directory-generation journal exists; recover it with web/release_transaction.mjs before using the version-pointer tool.");
}

function journalRecord(operation, paths, intendedBasePath, oldPointerText, oldManifestSha256, newPointerText, target) {
	return {
		schema: "surrealengine-version-pointer-journal-v1", operation, state: "prepared",
		createdAt: new Date().toISOString(), liveRelative: paths.liveRelative, intendedBasePath,
		oldPointerSha256: sha256(oldPointerText), oldPointerText, oldManifestSha256,
		newPointerSha256: sha256(newPointerText), newPointerText,
		newManifestSha256: target.manifestSha256, newBuildId: target.buildId,
	};
}

async function validateJournal(paths, intendedBasePath) {
	let journal;
	try {
		await requireRegularFile(paths.journal, "Version-pointer journal");
		journal = JSON.parse(await readFile(paths.journal, "utf8"));
	}
	catch (error) {
		if (error?.code === "ENOENT") throw new Error("No unfinished version-pointer journal exists.");
		throw new Error("Version-pointer journal is unreadable.");
	}
	if (journal?.schema !== "surrealengine-version-pointer-journal-v1" || journal.state !== "prepared" ||
		!["initialize", "promote", "rollback"].includes(journal.operation) ||
		journal.liveRelative !== paths.liveRelative || journal.intendedBasePath !== intendedBasePath ||
		!SHA256.test(journal.oldPointerSha256 || "") || sha256(journal.oldPointerText || "") !== journal.oldPointerSha256 ||
		!SHA256.test(journal.newPointerSha256 || "") || sha256(journal.newPointerText || "") !== journal.newPointerSha256 ||
		!SHA256.test(journal.oldManifestSha256 || "") || !SHA256.test(journal.newManifestSha256 || "") ||
		!BUILD_ID.test(journal.newBuildId || ""))
		throw new Error("Version-pointer journal has an invalid identity or target.");
	const parsedNew = parseVersionPointer(journal.newPointerText, intendedBasePath);
	if (!parsedNew || parsedNew.manifestSha256 !== journal.newManifestSha256 || parsedNew.buildId !== journal.newBuildId)
		throw new Error("Version-pointer journal new pointer is inconsistent.");
	const parsedOld = parseVersionPointer(journal.oldPointerText, intendedBasePath);
	if (journal.operation === "initialize") {
		if (parsedOld) throw new Error("Initialization journal unexpectedly contains an old pointer.");
	} else if (!parsedOld || parsedOld.manifestSha256 !== journal.oldManifestSha256) {
		throw new Error("Version-pointer journal old pointer is inconsistent.");
	}
	return journal;
}

async function finish(paths, record, recordPath) {
	await atomicJson(paths.completion, { ...record, schema: "surrealengine-version-pointer-completion-v1" });
	await durableRemove(paths.journal);
	return writeRecord(recordPath, record);
}

async function recoverUnlocked(paths, options) {
	const intendedBasePath = safeBasePath(options.intendedBasePath);
	const journal = await validateJournal(paths, intendedBasePath);
	const actualText = await readFile(paths.htaccess, "utf8");
	const actualSha = sha256(actualText);
	let action;
	let currentManifestSha256;
	if (actualSha === journal.oldPointerSha256) {
		await verifyGeneration(paths, journal.oldManifestSha256, intendedBasePath);
		action = "aborted-before-pointer-commit";
		currentManifestSha256 = journal.oldManifestSha256;
	} else if (actualSha === journal.newPointerSha256) {
		try {
			await verifyGeneration(paths, journal.newManifestSha256, intendedBasePath);
			action = "finalized-pointer-commit";
			currentManifestSha256 = journal.newManifestSha256;
		} catch (error) {
			if (error?.code && error.code !== "ENOENT") throw error;
			await verifyGeneration(paths, journal.oldManifestSha256, intendedBasePath);
			await atomicWrite(paths.htaccess, journal.oldPointerText, true);
			action = "rejected-target-and-restored-pointer";
			currentManifestSha256 = journal.oldManifestSha256;
		}
	} else {
		await verifyGeneration(paths, journal.oldManifestSha256, intendedBasePath);
		await atomicWrite(paths.htaccess, journal.oldPointerText, true);
		action = "restored-pointer-after-ambiguous-file-state";
		currentManifestSha256 = journal.oldManifestSha256;
	}
	const pointer = await readPointer(paths, intendedBasePath);
	if (journal.operation !== "initialize" || action === "finalized-pointer-commit") {
		if (!pointer || pointer.manifestSha256 !== currentManifestSha256)
			throw new Error("Recovered stable pointer does not match the selected generation.");
	} else if (pointer) throw new Error("Initialization recovery did not restore the legacy hosting file.");
	const record = {
		schema: "surrealengine-version-pointer-transaction-v1", operation: "recover",
		completedAt: new Date().toISOString(), action, sourceOperation: journal.operation,
		currentManifestSha256, journal,
		preservedStaleLock: options.preservedStaleLock || null,
	};
	return finish(paths, record, options.recordPath);
}

async function commitPointer(paths, options, operation, oldPointerText, oldManifestSha256, target, compatibilityRules) {
	const intendedBasePath = safeBasePath(options.intendedBasePath);
	const newPointerText = renderVersionPointer(target, intendedBasePath, compatibilityRules);
	const journal = journalRecord(operation, paths, intendedBasePath, oldPointerText,
		oldManifestSha256, newPointerText, target);
	await atomicJson(paths.journal, journal);
	try {
		await atomicWrite(paths.htaccess, newPointerText, true);
		if (options.transitionHook) await options.transitionHook("pointer-replaced", { paths, journal });
		const pointer = await readPointer(paths, intendedBasePath);
		if (!pointer || pointer.manifestSha256 !== target.manifestSha256)
			throw new Error("Committed pointer did not select the target generation.");
		await verifyGeneration(paths, target.manifestSha256, intendedBasePath);
		const record = {
			schema: "surrealengine-version-pointer-transaction-v1", operation,
			completedAt: new Date().toISOString(), previousManifestSha256: oldManifestSha256,
			currentManifestSha256: target.manifestSha256, currentBuildId: target.buildId,
			target: pointer.target,
		};
		return finish(paths, record, options.recordPath);
	} catch (error) {
		if (options.recoverOnFailure !== false && await exists(paths.journal)) {
			const recovery = await recoverUnlocked(paths, options);
			throw new Error(`${error.message} Recovery action: ${recovery.action}; current manifest: ${recovery.currentManifestSha256}.`);
		}
		throw error;
	}
}

export async function initializeVersionedRelease(options) {
	const paths = await pathsFor(options);
	const intendedBasePath = safeBasePath(options.intendedBasePath);
	const expected = requireSha(options.liveManifestSha256, "Expected live manifest hash");
	return withLock(paths, async () => {
		await refuseLegacyJournal(paths);
		if (await exists(paths.journal)) throw new Error("An unfinished version-pointer journal requires recovery.");
		const existingPointer = await readPointer(paths, intendedBasePath);
		if (existingPointer) {
			if (existingPointer.manifestSha256 !== expected)
				throw new Error("Stable pointer does not match the expected live manifest hash.");
			const identity = await verifyGeneration(paths, expected, intendedBasePath);
			return writeRecord(options.recordPath, {
				schema: "surrealengine-version-pointer-transaction-v1", operation: "initialize",
				completedAt: new Date().toISOString(), alreadyInitialized: true,
				currentManifestSha256: expected, currentBuildId: identity.buildId,
			});
		}
		const compatibility = await verifyRelease(paths.live, {
			manifestSha256: expected, intendedBasePath, ignoreTopLevelDirectories: ["releases"],
		});
		const originalHtaccess = await readFile(paths.htaccess, "utf8");
		const published = await publishCompatibilityUnlocked(paths, compatibility, intendedBasePath);
		const compatibilityRules = `${originalHtaccess.replace(/\s*$/, "\n")}${pointerNoStoreRules(intendedBasePath)}`;
		return commitPointer(paths, options, "initialize", originalHtaccess, expected,
			published.identity, compatibilityRules);
	});
}

export async function publishVersionedRelease(options) {
	const paths = await pathsFor(options);
	const intendedBasePath = safeBasePath(options.intendedBasePath);
	return withLock(paths, async () => {
		await refuseLegacyJournal(paths);
		if (await exists(paths.journal)) throw new Error("An unfinished version-pointer journal requires recovery.");
		const pointer = await readPointer(paths, intendedBasePath);
		if (!pointer) throw new Error("Initialize the stable version pointer before publishing a new generation.");
		await verifyGeneration(paths, pointer.manifestSha256, intendedBasePath);
		const published = await publishCandidateUnlocked(paths, options.candidate,
			requireSha(options.manifestSha256, "Candidate manifest hash"), intendedBasePath);
		return writeRecord(options.recordPath, {
			schema: "surrealengine-version-pointer-transaction-v1", operation: "publish",
			completedAt: new Date().toISOString(), alreadyPublished: published.alreadyPublished,
			published: published.identity,
			target: generationUrl(intendedBasePath, published.identity.manifestSha256),
		});
	});
}

async function movePointer(options, operation) {
	const paths = await pathsFor(options);
	const intendedBasePath = safeBasePath(options.intendedBasePath);
	const expected = requireSha(options.currentManifestSha256, "Expected current manifest hash");
	const targetSha = requireSha(options.targetManifestSha256, "Target manifest hash");
	if (expected === targetSha) throw new Error("Current and target generation hashes are the same.");
	return withLock(paths, async () => {
		await refuseLegacyJournal(paths);
		if (await exists(paths.journal)) throw new Error("An unfinished version-pointer journal requires recovery.");
		const pointer = await readPointer(paths, intendedBasePath);
		if (!pointer || pointer.manifestSha256 !== expected)
			throw new Error("Stable pointer does not match the expected current manifest hash.");
		await verifyGeneration(paths, expected, intendedBasePath);
		const target = await verifyGeneration(paths, targetSha, intendedBasePath);
		return commitPointer(paths, options, operation, pointer.contents, expected, target,
			pointer.compatibilityRules);
	});
}

export async function promoteVersionedRelease(options) {
	return movePointer(options, "promote");
}

export async function rollbackVersionedRelease(options) {
	return movePointer(options, "rollback");
}

export async function recoverVersionedRelease(options) {
	const paths = await pathsFor(options);
	await refuseLegacyJournal(paths);
	let preservedStaleLock = null;
	if (options.breakStaleLock && await exists(paths.lock)) {
		if (!(await exists(paths.journal))) {
			if (await exists(paths.legacyJournal))
				throw new Error("The stale lock belongs to the directory-generation transaction; use its recovery command.");
			const expected = requireSha(options.currentManifestSha256,
				"Expected current manifest hash for no-journal lock recovery");
			const pointer = await readPointer(paths, safeBasePath(options.intendedBasePath));
			if (pointer) {
				if (pointer.manifestSha256 !== expected)
					throw new Error("Stable pointer does not match the expected current manifest hash.");
				await verifyGeneration(paths, expected, options.intendedBasePath);
			} else {
				await verifyRelease(paths.live, {
					manifestSha256: expected, intendedBasePath: options.intendedBasePath,
					ignoreTopLevelDirectories: ["releases"],
				});
			}
		}
		preservedStaleLock = await preserveDeadLock(paths);
	}
	return withLock(paths, async () => {
		await refuseLegacyJournal(paths);
		if (!(await exists(paths.journal)) && preservedStaleLock) {
			const record = {
				schema: "surrealengine-version-pointer-transaction-v1", operation: "recover",
				completedAt: new Date().toISOString(), action: "cleared-dead-nonpointer-lock",
				currentManifestSha256: options.currentManifestSha256, preservedStaleLock,
			};
			await atomicJson(paths.completion, { ...record, schema: "surrealengine-version-pointer-completion-v1" });
			return writeRecord(options.recordPath, record);
		}
		return recoverUnlocked(paths, { ...options, preservedStaleLock });
	});
}
