/*
 * Local-only persistence for SurrealEngine files that the Web build mutates.
 *
 * This is deliberately separate from the UT99 importer. Only the explicit
 * paths classified by classifyMutablePath() can enter this store. In
 * particular, packages, maps, textures, sounds, music, and the original INI
 * files selected by the user are never enumerated or copied here.
 */
(function (global) {
	"use strict";

	const SCHEMA_NAME = "surrealengine-mutable-data";
	const SCHEMA_VERSION = 1;
	const DEFAULT_DATABASE_NAME = "surrealengine-mutable-data-v1";
	const DEFAULT_OPFS_DIRECTORY = "surrealengine-mutable-data-v1";
	const CHECKPOINT_INTERVAL_MS = 30000;
	const MAX_FILE_BYTES = 128 * 1024 * 1024;
	const MAX_TOTAL_BYTES = 512 * 1024 * 1024;
	const EXACT_MUTABLE_PATHS = new Map([
		["/gamedata/System/SE-UnrealTournament.ini", "engine-config"],
		["/gamedata/System/SE-User.ini", "user-keybindings-vr-settings"],
		["/home/web_user/.config/SurrealEngine/Settings.json", "launcher-settings"],
		["/home/web_user/.config/SurrealEngine/SE-Log-LastRun.txt", "last-run-log"],
	]);
	const UT99_SAVE_PATTERN = /^\/gamedata\/Save\/Save[0-9]+\.usa$/;

	class MutableDataError extends Error {
		constructor(code, message, details) {
			super(message);
			this.name = "SurrealMutableDataError";
			this.code = code;
			this.details = details || null;
		}
	}

	function canonicalMutablePath(input) {
		if (typeof input !== "string") {
			throw new MutableDataError("INVALID_PATH", "A mutable-data entry has no usable path.");
		}
		let path = input.replace(/\\/g, "/");
		while (path.includes("//")) path = path.replace(/\/\//g, "/");
		if (!path.startsWith("/") || path === "/") {
			throw new MutableDataError("INVALID_PATH", "Mutable-data paths must be absolute Emscripten paths.");
		}
		const parts = path.slice(1).split("/");
		if (parts.some(part => !part || part === "." || part === ".." || part.includes("\0"))) {
			throw new MutableDataError("INVALID_PATH", "A mutable-data path contains unsafe components.");
		}
		return "/" + parts.join("/");
	}

	function classifyMutablePath(input) {
		let path;
		try { path = canonicalMutablePath(input); }
		catch (_) { return null; }
		if (EXACT_MUTABLE_PATHS.has(path)) return EXACT_MUTABLE_PATHS.get(path);
		if (UT99_SAVE_PATTERN.test(path)) return "ut99-save";
		return null;
	}

	function validateEntries(entries) {
		if (!Array.isArray(entries)) {
			throw new MutableDataError("INVALID_ENTRIES", "Mutable-data storage contains an invalid file list.");
		}
		const normalized = [];
		const paths = new Set();
		let totalBytes = 0;
		for (const source of entries) {
			const path = canonicalMutablePath(source && source.path);
			const category = classifyMutablePath(path);
			if (!category) {
				throw new MutableDataError("PATH_NOT_ALLOWED", "Mutable-data storage contains a path outside the allowlist.", { path });
			}
			if (paths.has(path)) throw new MutableDataError("DUPLICATE_PATH", "Mutable-data storage contains a duplicate path.", { path });
			const size = Number(source.size);
			if (!Number.isSafeInteger(size) || size < 0 || size > MAX_FILE_BYTES || typeof source.getBlob !== "function") {
				throw new MutableDataError("INVALID_FILE", "A mutable-data file has invalid metadata.", { path });
			}
			totalBytes += size;
			if (totalBytes > MAX_TOTAL_BYTES) {
				throw new MutableDataError("DATA_TOO_LARGE", "Mutable data exceeds the local persistence safety limit.");
			}
			paths.add(path);
			normalized.push({ path, category, size, getBlob: source.getBlob });
		}
		normalized.sort((a, b) => a.path.localeCompare(b.path));
		return { entries: normalized, fileCount: normalized.length, totalBytes };
	}

	function metadataFor(validation, backend, datasetId) {
		return {
			schema: SCHEMA_NAME,
			version: SCHEMA_VERSION,
			datasetId,
			createdAt: new Date().toISOString(),
			backend,
			fileCount: validation.fileCount,
			totalBytes: validation.totalBytes,
			files: validation.entries.map(entry => ({ path: entry.path, category: entry.category, size: entry.size })),
		};
	}

	function validateMetadata(metadata) {
		if (!metadata || metadata.schema !== SCHEMA_NAME || metadata.version !== SCHEMA_VERSION ||
			typeof metadata.datasetId !== "string" || !metadata.datasetId || !Array.isArray(metadata.files)) {
			throw new MutableDataError("STORAGE_SCHEMA", "Saved mutable data uses an unsupported or corrupt schema. Clear mutable data and try again.");
		}
		const validation = validateEntries(metadata.files.map(file => ({
			path: file.path,
			size: file.size,
			getBlob: async () => { throw new MutableDataError("STORAGE_READ", "Mutable file provider is unavailable."); },
		})));
		if (metadata.fileCount !== validation.fileCount || metadata.totalBytes !== validation.totalBytes) {
			throw new MutableDataError("STORAGE_METADATA", "Saved mutable-data metadata is inconsistent. Clear mutable data and try again.");
		}
		return validation;
	}

	function datasetId() {
		return Date.now().toString(36) + "-" + Math.random().toString(36).slice(2);
	}

	async function writeBlobToOPFS(fileHandle, blob) {
		const writable = await fileHandle.createWritable();
		try { await writable.write(blob); await writable.close(); }
		catch (error) { try { await writable.abort(); } catch (_) { /* best effort */ } throw error; }
	}

	async function getOPFSDirectory(root, parts, create) {
		let directory = root;
		for (const part of parts) directory = await directory.getDirectoryHandle(part, { create: !!create });
		return directory;
	}

	function storageParts(path) {
		return canonicalMutablePath(path).slice(1).split("/");
	}

	class OPFSStorage {
		constructor(root, directoryName) {
			this.root = root;
			this.directoryName = directoryName || DEFAULT_OPFS_DIRECTORY;
			this.backend = "opfs";
		}

		async load() {
			let appRoot;
			let metadataFile;
			try {
				appRoot = await this.root.getDirectoryHandle(this.directoryName);
				metadataFile = await appRoot.getFileHandle("current.json");
			} catch (error) {
				if (error && error.name === "NotFoundError") return null;
				throw new MutableDataError("STORAGE_READ", "Saved mutable data could not be opened.");
			}
			let metadata;
			try { metadata = JSON.parse(await (await metadataFile.getFile()).text()); }
			catch (_) { throw new MutableDataError("STORAGE_SCHEMA", "Saved mutable-data metadata is corrupt. Clear mutable data and try again."); }
			validateMetadata(metadata);
			let snapshotRoot;
			try { snapshotRoot = await getOPFSDirectory(appRoot, ["snapshots", metadata.datasetId], false); }
			catch (_) { throw new MutableDataError("STORAGE_MISSING_DATASET", "Saved mutable files are incomplete. Clear mutable data and try again."); }
			return {
				metadata,
				getBlob: async path => {
					if (!classifyMutablePath(path)) throw new MutableDataError("PATH_NOT_ALLOWED", "Refusing to read a path outside the mutable-data allowlist.");
					const parts = storageParts(path);
					try {
						const parent = await getOPFSDirectory(snapshotRoot, parts.slice(0, -1), false);
						return await (await parent.getFileHandle(parts[parts.length - 1])).getFile();
					} catch (_) { throw new MutableDataError("STORAGE_MISSING_FILE", "A saved mutable file is missing. Clear mutable data and try again.", { path }); }
				},
			};
		}

		async save(entries) {
			const validation = validateEntries(entries);
			const previous = await this.load().catch(() => null);
			const appRoot = await this.root.getDirectoryHandle(this.directoryName, { create: true });
			const snapshots = await appRoot.getDirectoryHandle("snapshots", { create: true });
			const id = datasetId();
			const snapshotRoot = await snapshots.getDirectoryHandle(id, { create: true });
			try {
				for (const entry of validation.entries) {
					const blob = await entry.getBlob();
					if (!blob || blob.size !== entry.size) throw new MutableDataError("FILE_CHANGED", "A mutable file changed during a checkpoint.", { path: entry.path });
					const parts = storageParts(entry.path);
					const parent = await getOPFSDirectory(snapshotRoot, parts.slice(0, -1), true);
					await writeBlobToOPFS(await parent.getFileHandle(parts[parts.length - 1], { create: true }), blob);
				}
				const metadata = metadataFor(validation, this.backend, id);
				await writeBlobToOPFS(await appRoot.getFileHandle("current.json", { create: true }),
					new Blob([JSON.stringify(metadata)], { type: "application/json" }));
				if (previous && previous.metadata.datasetId !== id) {
					try { await snapshots.removeEntry(previous.metadata.datasetId, { recursive: true }); } catch (_) { /* stale cleanup is best effort */ }
				}
				return metadata;
			} catch (error) {
				try { await snapshots.removeEntry(id, { recursive: true }); } catch (_) { /* best effort */ }
				throw error;
			}
		}

		async clear() {
			try { await this.root.removeEntry(this.directoryName, { recursive: true }); }
			catch (error) { if (!error || error.name !== "NotFoundError") throw error; }
		}
	}

	function requestPromise(request) {
		return new Promise((resolve, reject) => {
			request.onsuccess = () => resolve(request.result);
			request.onerror = () => reject(request.error || new Error("IndexedDB request failed"));
		});
	}

	function transactionPromise(transaction) {
		return new Promise((resolve, reject) => {
			transaction.oncomplete = () => resolve();
			transaction.onerror = () => reject(transaction.error || new Error("IndexedDB transaction failed"));
			transaction.onabort = () => reject(transaction.error || new Error("IndexedDB transaction aborted"));
		});
	}

	class IndexedDBStorage {
		constructor(databaseName) {
			this.databaseName = databaseName || DEFAULT_DATABASE_NAME;
			this.backend = "indexeddb";
			this.databasePromise = null;
		}

		async _db() {
			if (!this.databasePromise) {
				this.databasePromise = new Promise((resolve, reject) => {
					const request = indexedDB.open(this.databaseName, 1);
					request.onupgradeneeded = () => {
						const db = request.result;
						if (!db.objectStoreNames.contains("metadata")) db.createObjectStore("metadata");
						if (!db.objectStoreNames.contains("files")) db.createObjectStore("files");
					};
					request.onsuccess = () => resolve(request.result);
					request.onerror = () => reject(request.error || new Error("IndexedDB open failed"));
				});
			}
			return this.databasePromise;
		}

		async load() {
			const db = await this._db();
			const transaction = db.transaction("metadata", "readonly");
			const metadata = await requestPromise(transaction.objectStore("metadata").get("current"));
			await transactionPromise(transaction);
			if (!metadata) return null;
			validateMetadata(metadata);
			return {
				metadata,
				getBlob: async path => {
					if (!classifyMutablePath(path)) throw new MutableDataError("PATH_NOT_ALLOWED", "Refusing to read a path outside the mutable-data allowlist.");
					const read = db.transaction("files", "readonly");
					const record = await requestPromise(read.objectStore("files").get(metadata.datasetId + "\0" + canonicalMutablePath(path)));
					await transactionPromise(read);
					if (!record || !(record.blob instanceof Blob)) throw new MutableDataError("STORAGE_MISSING_FILE", "A saved mutable file is missing.", { path });
					return record.blob;
				},
			};
		}

		async save(entries) {
			const validation = validateEntries(entries);
			const blobs = await Promise.all(validation.entries.map(entry => entry.getBlob()));
			for (let index = 0; index < blobs.length; index++) {
				if (!blobs[index] || blobs[index].size !== validation.entries[index].size) {
					throw new MutableDataError("FILE_CHANGED", "A mutable file changed during a checkpoint.", { path: validation.entries[index].path });
				}
			}
			const db = await this._db();
			const id = datasetId();
			const metadata = metadataFor(validation, this.backend, id);
			const transaction = db.transaction(["metadata", "files"], "readwrite");
			const metadataStore = transaction.objectStore("metadata");
			const files = transaction.objectStore("files");
			files.clear();
			for (let index = 0; index < validation.entries.length; index++) {
				const entry = validation.entries[index];
				files.put({ datasetId: id, path: entry.path, size: entry.size, blob: blobs[index] }, id + "\0" + entry.path);
			}
			metadataStore.put(metadata, "current");
			await transactionPromise(transaction);
			return metadata;
		}

		async clear() {
			const db = await this._db();
			const transaction = db.transaction(["metadata", "files"], "readwrite");
			transaction.objectStore("metadata").clear();
			transaction.objectStore("files").clear();
			await transactionPromise(transaction);
		}
	}

	async function createStorage(options) {
		const settings = options || {};
		if (settings.storage) return settings.storage;
		if (settings.forceBackend !== "indexeddb" && global.navigator && global.navigator.storage &&
			typeof global.navigator.storage.getDirectory === "function") {
			try {
				const root = await global.navigator.storage.getDirectory();
				await root.getDirectoryHandle(settings.opfsDirectory || DEFAULT_OPFS_DIRECTORY, { create: true });
				return new OPFSStorage(root, settings.opfsDirectory);
			} catch (_) { /* IndexedDB is the compatibility fallback. */ }
		}
		return new IndexedDBStorage(settings.databaseName);
	}

	function fsExists(FS, path) {
		try {
			if (typeof FS.analyzePath === "function") return !!FS.analyzePath(path).exists;
			FS.stat(path);
			return true;
		} catch (_) { return false; }
	}

	function fsIsFile(FS, path) {
		try {
			const stat = FS.stat(path);
			return typeof FS.isFile === "function" ? FS.isFile(stat.mode) : stat.type !== "directory";
		} catch (_) { return false; }
	}

	function readFSBlob(FS, path) {
		const bytes = FS.readFile(path, { encoding: "binary" });
		const copy = bytes instanceof Uint8Array ? new Uint8Array(bytes) : new Uint8Array(bytes.buffer || bytes);
		return new Blob([copy], { type: path.endsWith(".json") ? "application/json" : "application/octet-stream" });
	}

	async function snapshotFS(FS) {
		if (!FS || typeof FS.readFile !== "function") throw new MutableDataError("NO_EMSCRIPTEN_FS", "The runtime filesystem is unavailable.");
		const paths = Array.from(EXACT_MUTABLE_PATHS.keys());
		if (fsExists(FS, "/gamedata/Save") && typeof FS.readdir === "function") {
			for (const name of FS.readdir("/gamedata/Save")) {
				const path = "/gamedata/Save/" + name;
				if (classifyMutablePath(path)) paths.push(path);
			}
		}
		const entries = [];
		for (const path of paths) {
			if (!classifyMutablePath(path) || !fsIsFile(FS, path)) continue;
			const blob = readFSBlob(FS, path);
			entries.push({ path, size: blob.size, getBlob: async () => blob });
		}
		return validateEntries(entries);
	}

	function ensureFSDirectory(FS, path) {
		const importer = global.SurrealUT99Importer;
		if (importer && typeof importer.ensureFSDirectory === "function") return importer.ensureFSDirectory(FS, path);
		if (typeof FS.mkdirTree === "function") return FS.mkdirTree(path);
		let current = "";
		for (const part of path.split("/").filter(Boolean)) {
			current += "/" + part;
			try { FS.mkdir(current); } catch (_) { /* already exists */ }
		}
	}

	async function writeBlobToFS(FS, path, blob) {
		const importer = global.SurrealUT99Importer;
		if (importer && typeof importer.writeBlobToFS === "function") return importer.writeBlobToFS(FS, path, blob);
		FS.writeFile(path, new Uint8Array(await blob.arrayBuffer()));
	}

	async function restoreDataset(FS, dataset) {
		if (!dataset) return { fileCount: 0, totalBytes: 0 };
		const validation = validateMetadata(dataset.metadata);
		const blobs = [];
		let totalBytes = 0;
		for (const file of validation.entries) {
			const blob = await dataset.getBlob(file.path);
			if (!blob || blob.size !== file.size) throw new MutableDataError("STORAGE_FILE_SIZE", "A saved mutable file has the wrong size.", { path: file.path });
			blobs.push(blob);
			totalBytes += blob.size;
		}
		// Validate the complete stored snapshot before overwriting any imported
		// baseline file in MEMFS.
		for (let index = 0; index < validation.entries.length; index++) {
			const file = validation.entries[index];
			ensureFSDirectory(FS, file.path.slice(0, file.path.lastIndexOf("/")));
			await writeBlobToFS(FS, file.path, blobs[index]);
		}
		return { fileCount: validation.fileCount, totalBytes };
	}

	class MutableDataController {
		constructor(Module, options) {
			this.Module = Module;
			this.options = options || {};
			this.storage = null;
			this.interval = null;
			this.disposed = false;
			this.automaticPaused = false;
			this.requiresClear = false;
			this.queue = Promise.resolve();
			this.details = {
				schema: SCHEMA_NAME, version: SCHEMA_VERSION, backend: null, state: "created",
				fileCount: 0, totalBytes: 0, lastRestoreAt: null, lastFlushAt: null,
				lastCheckpointReason: null, error: null,
			};
			this.onVisibilityChange = () => {
				if (global.document && global.document.hidden) this.checkpoint("visibility-hidden");
			};
			this.onPageHide = () => this.checkpoint("pagehide");
		}

		_log(message) {
			if (typeof this.options.log === "function") this.options.log("[mutable] " + message);
		}

		status() {
			return Object.assign({}, this.details, {
				automaticCheckpoints: !this.automaticPaused && !this.disposed,
				requiresClear: this.requiresClear,
				allowlist: Array.from(EXACT_MUTABLE_PATHS.keys()).concat(["/gamedata/Save/Save<N>.usa"]),
			});
		}

		async initialize() {
			this.storage = await createStorage(this.options);
			this.details.backend = this.storage.backend;
			this.details.state = "restoring";
			try {
				const dataset = await this.storage.load();
				const restored = await restoreDataset(this.Module.FS, dataset);
				this.details.fileCount = restored.fileCount;
				this.details.totalBytes = restored.totalBytes;
				this.details.lastRestoreAt = new Date().toISOString();
				this.details.state = "ready";
				this._log("restored " + restored.fileCount + " allowlisted file(s) from " + this.storage.backend);
			} catch (error) {
				this.details.state = "restore-failed";
				this.details.error = error && error.code ? error.code + ": " + error.message : String(error);
				this.requiresClear = true;
				this.automaticPaused = true;
				this._log("restore failed; continuing without mutable overlay: " + this.details.error);
			}
			this._attachLifecycle();
			return this.status();
		}

		_attachLifecycle() {
			if (global.document) global.document.addEventListener("visibilitychange", this.onVisibilityChange);
			if (global.addEventListener) global.addEventListener("pagehide", this.onPageHide);
			const intervalMs = Number.isFinite(this.options.checkpointIntervalMs) ?
				this.options.checkpointIntervalMs : CHECKPOINT_INTERVAL_MS;
			if (intervalMs > 0) this.interval = global.setInterval(() => this.checkpoint("interval"), intervalMs);
		}

		_enqueue(callback) {
			const task = this.queue.then(callback, callback);
			this.queue = task.catch(() => {});
			return task;
		}

		async _flush(reason) {
			this.details.state = "flushing";
			this.details.lastCheckpointReason = reason || "explicit";
			try {
				if (typeof this.options.logSnapshot === "function") {
					const text = String(await this.options.logSnapshot());
					const bytes = new TextEncoder().encode(text);
					if (bytes.byteLength > MAX_FILE_BYTES) throw new MutableDataError("LOG_TOO_LARGE", "The last-run log exceeds the mutable-file safety limit.");
					const logPath = "/home/web_user/.config/SurrealEngine/SE-Log-LastRun.txt";
					ensureFSDirectory(this.Module.FS, logPath.slice(0, logPath.lastIndexOf("/")));
					this.Module.FS.writeFile(logPath, bytes);
				}
				const snapshot = await snapshotFS(this.Module.FS);
				const metadata = await this.storage.save(snapshot.entries);
				this.details.fileCount = metadata.fileCount;
				this.details.totalBytes = metadata.totalBytes;
				this.details.lastFlushAt = new Date().toISOString();
				this.details.state = "ready";
				this.details.error = null;
				this._log("checkpointed " + metadata.fileCount + " allowlisted file(s); reason=" + this.details.lastCheckpointReason);
				return this.status();
			} catch (error) {
				this.details.state = "flush-failed";
				this.details.error = error && error.code ? error.code + ": " + error.message : String(error);
				this._log("checkpoint failed: " + this.details.error);
				throw error;
			}
		}

		flush(reason) {
			if (this.disposed) return Promise.reject(new MutableDataError("DISPOSED", "Mutable-data persistence has been disposed."));
			if (this.requiresClear) return Promise.reject(new MutableDataError("CLEAR_REQUIRED", "Clear incompatible or corrupt mutable data before creating a new checkpoint."));
			this.automaticPaused = false;
			return this._enqueue(() => this._flush(reason || "explicit"));
		}

		checkpoint(reason) {
			if (this.disposed || this.automaticPaused) return this.queue;
			return this._enqueue(() => this._flush(reason || "checkpoint")).catch(() => this.status());
		}

		clear() {
			if (this.disposed) return Promise.reject(new MutableDataError("DISPOSED", "Mutable-data persistence has been disposed."));
			this.automaticPaused = true;
			return this._enqueue(async () => {
				await this.storage.clear();
				this.requiresClear = false;
				this.details.state = "cleared";
				this.details.fileCount = 0;
				this.details.totalBytes = 0;
				this.details.error = null;
				this._log("cleared mutable-data storage only; imported game data was not touched");
				return this.status();
			});
		}

		whenIdle() { return this.queue; }

		dispose() {
			this.disposed = true;
			if (this.interval !== null) global.clearInterval(this.interval);
			if (global.document) global.document.removeEventListener("visibilitychange", this.onVisibilityChange);
			if (global.removeEventListener) global.removeEventListener("pagehide", this.onPageHide);
			this.details.state = "disposed";
		}
	}

	async function start(Module, options) {
		if (!Module || !Module.FS) throw new MutableDataError("NO_EMSCRIPTEN_FS", "The SurrealEngine runtime filesystem is unavailable.");
		const controller = new MutableDataController(Module, options);
		const result = await controller.initialize();
		return { controller, result };
	}

	global.SurrealMutableData = Object.freeze({
		SCHEMA_NAME,
		SCHEMA_VERSION,
		EXACT_MUTABLE_PATHS: Object.freeze(Array.from(EXACT_MUTABLE_PATHS.keys())),
		MutableDataError,
		OPFSStorage,
		IndexedDBStorage,
		MutableDataController,
		canonicalMutablePath,
		classifyMutablePath,
		validateEntries,
		validateMetadata,
		createStorage,
		snapshotFS,
		restoreDataset,
		start,
	});
})(typeof window !== "undefined" ? window : globalThis);
