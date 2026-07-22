/*
 * Legal, local-only UT99 data import for the redistributable Web build.
 *
 * This module never uploads game data and never logs file contents. It stores
 * user-selected files in origin-private storage, then streams them into the
 * Emscripten filesystem before main() is called. Developer builds that embed
 * /gamedata keep their existing direct boot path.
 */
(function (global) {
	"use strict";

	const SCHEMA_NAME = "surrealengine-ut99-data";
	const SCHEMA_VERSION = 1;
	const MAP_MANIFEST_SCHEMA = "surrealengine-ut99-map-manifest";
	const MAP_MANIFEST_VERSION = 1;
	const DEFAULT_DATABASE_NAME = "surrealengine-ut99-data-v1";
	const DEFAULT_OPFS_DIRECTORY = "surrealengine-ut99-data-v1";
	const GAME_ROOT = "/gamedata";
	const COPY_CHUNK_BYTES = 4 * 1024 * 1024;
	const KNOWN_TOP_LEVEL_DIRECTORIES = new Map([
		["system", "System"],
		["system64", "System64"],
		["maps", "Maps"],
		["textures", "Textures"],
		["sounds", "Sounds"],
		["music", "Music"],
		["save", "Save"],
	]);
	const UT_EXECUTABLE_NAMES = [
		"UnrealTournament.exe",
		"ut-bin",
		"ut-bin-x86",
		"ut-bin-amd64",
		"ut-bin-x64",
	];
	const UT_EXECUTABLES = UT_EXECUTABLE_NAMES.map(name => name.toLowerCase());

	class ImportError extends Error {
		constructor(code, message, details) {
			super(message);
			this.name = "SurrealUT99ImportError";
			this.code = code;
			this.details = details || null;
		}
	}

	function safeMessage(error) {
		if (error instanceof ImportError) return error.message;
		if (error && error.name === "AbortError") return "Folder selection was cancelled.";
		return "The browser could not import the selected data. Try again or clear the saved import.";
	}

	function formatBytes(value) {
		if (!Number.isFinite(value)) return "unknown";
		const units = ["B", "KiB", "MiB", "GiB", "TiB"];
		let amount = Math.max(0, value);
		let unit = 0;
		while (amount >= 1024 && unit < units.length - 1) {
			amount /= 1024;
			unit++;
		}
		return (unit === 0 ? amount.toFixed(0) : amount.toFixed(amount >= 10 ? 1 : 2)) + " " + units[unit];
	}

	function canonicalizeRelativePath(input) {
		if (typeof input !== "string") {
			throw new ImportError("INVALID_PATH", "A selected file has no usable relative path.");
		}
		let path = input.replace(/\\/g, "/").replace(/^\.\//, "");
		while (path.includes("//")) path = path.replace(/\/\//g, "/");
		if (!path || path.startsWith("/") || /^[A-Za-z]:/.test(path)) {
			throw new ImportError("INVALID_PATH", "The selection contains an absolute or empty path.");
		}
		const parts = path.split("/");
		if (parts.some(part => !part || part === "." || part === ".." || part.includes("\0"))) {
			throw new ImportError("INVALID_PATH", "The selection contains an unsafe relative path.");
		}
		const canonicalTop = KNOWN_TOP_LEVEL_DIRECTORIES.get(parts[0].toLowerCase());
		if (canonicalTop) parts[0] = canonicalTop;
		return parts.join("/");
	}

	function stripSelectedRoot(path, selectedRoot) {
		const normalized = String(path || "").replace(/\\/g, "/").replace(/^\.\//, "");
		if (!selectedRoot) return normalized;
		const prefix = selectedRoot + "/";
		return normalized.startsWith(prefix) ? normalized.slice(prefix.length) : normalized;
	}

	function makeEntry(path, blobProvider, size) {
		return {
			path: canonicalizeRelativePath(path),
			size: Number(size) || 0,
			getBlob: blobProvider,
		};
	}

	function entriesFromFileList(fileList) {
		const files = Array.from(fileList || []);
		if (!files.length) throw new ImportError("EMPTY_SELECTION", "No files were selected.");

		const relativePaths = files.map(file => file.webkitRelativePath || file.name || "");
		const roots = relativePaths.map(path => String(path).replace(/\\/g, "/").split("/")[0]);
		const hasSharedFolderRoot = relativePaths.every(path => String(path).includes("/")) &&
			roots.every(root => root === roots[0]);
		const selectedRoot = hasSharedFolderRoot ? roots[0] : null;
		return files.map((file, index) => makeEntry(
			stripSelectedRoot(relativePaths[index], selectedRoot),
			async () => file,
			file.size));
	}

	async function entriesFromDirectoryHandle(rootHandle, onProgress) {
		if (!rootHandle || rootHandle.kind !== "directory") {
			throw new ImportError("INVALID_DIRECTORY", "Choose the Unreal Tournament installation folder.");
		}
		const entries = [];
		let visited = 0;
		async function walk(directory, prefix) {
			const children = [];
			for await (const child of directory.values()) children.push(child);
			children.sort((a, b) => a.name.localeCompare(b.name));
			for (const child of children) {
				const relativePath = prefix ? prefix + "/" + child.name : child.name;
				if (child.kind === "directory") {
					await walk(child, relativePath);
				} else if (child.kind === "file") {
					const file = await child.getFile();
					entries.push(makeEntry(relativePath, async () => child.getFile(), file.size));
					visited++;
					if (onProgress) onProgress({ phase: "scan", filesDone: visited });
				}
			}
		}
		await walk(rootHandle, "");
		if (!entries.length) throw new ImportError("EMPTY_SELECTION", "The selected folder contains no files.");
		return entries;
	}

	function validateEntries(entries) {
		if (!Array.isArray(entries) || !entries.length) {
			throw new ImportError("EMPTY_SELECTION", "The selected folder contains no files.");
		}
		const normalized = [];
		const byLowerPath = new Map();
		for (const source of entries) {
			const path = canonicalizeRelativePath(source.path);
			const lower = path.toLowerCase();
			if (byLowerPath.has(lower)) {
				throw new ImportError("DUPLICATE_PATH", "The selected folder contains duplicate paths that differ only by letter case.");
			}
			if (!Number.isFinite(source.size) || source.size < 0 || typeof source.getBlob !== "function") {
				throw new ImportError("INVALID_FILE", "A selected file could not be read.");
			}
			const entry = { path, size: source.size, getBlob: source.getBlob };
			byLowerPath.set(lower, entry);
			normalized.push(entry);
		}

		const paths = new Set(byLowerPath.keys());
		const hasDirectory = name => Array.from(paths).some(path => path.startsWith(name.toLowerCase() + "/"));
		const hasExtensionIn = (directory, extension) => Array.from(paths).some(path =>
			path.startsWith(directory.toLowerCase() + "/") && path.endsWith(extension));
		const missing = [];
		for (const directory of ["System", "Maps", "Textures", "Sounds", "Music"]) {
			if (!hasDirectory(directory)) missing.push(directory + "/ directory");
		}
		for (const packageName of ["Core.u", "Engine.u", "Botpack.u"]) {
			if (!paths.has("system/" + packageName.toLowerCase())) missing.push("System/" + packageName);
		}
		if (!paths.has("system/unrealtournament.ini")) missing.push("System/UnrealTournament.ini");
		if (!UT_EXECUTABLES.some(name => paths.has("system/" + name) || paths.has("system64/" + name))) {
			missing.push("a supported Unreal Tournament executable in System/ or System64/");
		}
		if (!hasExtensionIn("Maps", ".unr")) missing.push("at least one Maps/*.unr map");
		if (!hasExtensionIn("Textures", ".utx")) missing.push("at least one Textures/*.utx package");
		if (!hasExtensionIn("Sounds", ".uax")) missing.push("at least one Sounds/*.uax package");
		if (!hasExtensionIn("Music", ".umx")) missing.push("at least one Music/*.umx package");
		if (missing.length) {
			throw new ImportError(
				"MISSING_UT99_DATA",
				"This does not look like a complete UT99 installation. Missing: " + missing.join(", ") + ". Select the folder that directly contains System, Maps, Textures, Sounds, and Music.",
				{ missing: missing.slice() });
		}

		normalized.sort((a, b) => a.path.localeCompare(b.path));
		const totalBytes = normalized.reduce((total, entry) => total + entry.size, 0);
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
			files: validation.entries.map(entry => ({ path: entry.path, size: entry.size })),
		};
	}

	function validateMetadata(metadata) {
		if (!metadata || metadata.schema !== SCHEMA_NAME || metadata.version !== SCHEMA_VERSION ||
			typeof metadata.datasetId !== "string" || !Array.isArray(metadata.files)) {
			throw new ImportError("STORAGE_SCHEMA", "Saved UT99 data uses an unsupported or corrupt storage schema. Clear it and import again.");
		}
		const entries = metadata.files.map(file => ({
			path: canonicalizeRelativePath(file.path),
			size: Number(file.size),
			getBlob: async () => { throw new Error("loader not installed"); },
		}));
		const validation = validateEntries(entries);
		if (validation.fileCount !== metadata.fileCount || validation.totalBytes !== metadata.totalBytes) {
			throw new ImportError("STORAGE_METADATA", "Saved UT99 data metadata is inconsistent. Clear it and import again.");
		}
		return metadata;
	}

	function immutableMapManifest(state, maps, rejectedCount) {
		return Object.freeze({
			schema: MAP_MANIFEST_SCHEMA,
			version: MAP_MANIFEST_VERSION,
			state: state,
			maps: Object.freeze(Array.from(maps || [])),
			rejectedCount: Number.isSafeInteger(rejectedCount) && rejectedCount > 0 ? rejectedCount : 0,
		});
	}

	function isSafeMapBasename(value) {
		return typeof value === "string" &&
			/^(?:DM|CTF|DOM|AS)-[A-Za-z0-9][A-Za-z0-9_\-\[\]']{0,63}$/i.test(value);
	}

	function mapManifestFromMetadata(metadata, state) {
		if (!metadata) return immutableMapManifest(state || "unavailable", [], 0);
		validateMetadata(metadata);
		const candidates = [];
		let rejectedCount = 0;
		for (const file of metadata.files) {
			const path = canonicalizeRelativePath(file.path);
			if (!/^Maps\//i.test(path) || !/\.unr$/i.test(path)) continue;
			const match = /^Maps\/([^/]+)\.unr$/i.exec(path);
			if (!match) {
				rejectedCount++;
				continue;
			}
			const basename = match[1];
			// The public launcher manifest is narrower than valid UT package
			// metadata: it contains only direct map basenames which can safely
			// enter the launcher's fixed offline URL builder.
			if (!isSafeMapBasename(basename)) {
				rejectedCount++;
				continue;
			}
			candidates.push(basename);
		}
		candidates.sort((a, b) => {
			const lowerA = a.toLowerCase();
			const lowerB = b.toLowerCase();
			if (lowerA < lowerB) return -1;
			if (lowerA > lowerB) return 1;
			return a < b ? -1 : (a > b ? 1 : 0);
		});
		const seen = new Set();
		const maps = [];
		for (const candidate of candidates) {
			const key = candidate.toLowerCase();
			if (seen.has(key)) continue;
			seen.add(key);
			maps.push(candidate);
		}
		const resolvedState = state === "ready" && !maps.length ? "empty" :
			(state || (maps.length ? "ready" : "empty"));
		return immutableMapManifest(resolvedState, maps, rejectedCount);
	}

	function selectLaunchMap(manifest, preferredMap) {
		if (!manifest || manifest.schema !== MAP_MANIFEST_SCHEMA ||
			manifest.version !== MAP_MANIFEST_VERSION || !Array.isArray(manifest.maps)) {
			throw new ImportError("MAP_MANIFEST", "The imported map list is unavailable or invalid.");
		}
		if (manifest.state === "unavailable") {
			if (!isSafeMapBasename(preferredMap)) {
				throw new ImportError("MAP_SELECTION", "Choose a safe direct Unreal Tournament map name.");
			}
			return preferredMap;
		}
		if (manifest.state !== "ready" || !manifest.maps.length) {
			throw new ImportError("MAP_SELECTION", "The imported data contains no compatible direct game maps.");
		}
		const preferred = typeof preferredMap === "string" ? preferredMap.toLowerCase() : "";
		const selected = manifest.maps.find(map => map.toLowerCase() === preferred) || manifest.maps[0];
		if (!isSafeMapBasename(selected)) {
			throw new ImportError("MAP_SELECTION", "The selected imported map name is unsafe.");
		}
		return selected;
	}

	async function writeBlobToOPFS(fileHandle, blob) {
		const writable = await fileHandle.createWritable();
		try {
			await writable.write(blob);
			await writable.close();
		} catch (error) {
			try { await writable.abort(); } catch (_) { /* best effort */ }
			throw error;
		}
	}

	async function getOPFSDirectory(root, parts, create) {
		let current = root;
		for (const part of parts) current = await current.getDirectoryHandle(part, { create: !!create });
		return current;
	}

	class OPFSStorage {
		constructor(root, directoryName) {
			this.root = root;
			this.directoryName = directoryName || DEFAULT_OPFS_DIRECTORY;
			this.backend = "opfs";
		}

		async _appRoot(create) {
			return this.root.getDirectoryHandle(this.directoryName, { create: !!create });
		}

		async load() {
			let appRoot;
			let metadata;
			try {
				appRoot = await this._appRoot(false);
				const handle = await appRoot.getFileHandle("current.json");
				metadata = validateMetadata(JSON.parse(await (await handle.getFile()).text()));
			} catch (error) {
				if (error && error.name === "NotFoundError") return null;
				if (error instanceof ImportError) throw error;
				throw new ImportError("STORAGE_READ", "Saved UT99 data could not be read. Clear it and import again.");
			}
			try {
				const datasetRoot = await getOPFSDirectory(appRoot, ["imports", metadata.datasetId], false);
				return {
					metadata,
					getBlob: async path => {
						try {
							const parts = canonicalizeRelativePath(path).split("/");
							const parent = await getOPFSDirectory(datasetRoot, parts.slice(0, -1), false);
							return await (await parent.getFileHandle(parts[parts.length - 1])).getFile();
						} catch (_) {
							throw new ImportError("STORAGE_MISSING_FILE", "Saved UT99 data is incomplete. Clear it and import again.");
						}
					},
				};
			} catch (error) {
				if (error instanceof ImportError) throw error;
				throw new ImportError("STORAGE_MISSING_DATASET", "Saved UT99 data is incomplete. Clear it and import again.");
			}
		}

		async save(validation, onProgress) {
			const datasetId = Date.now().toString(36) + "-" + Math.random().toString(36).slice(2);
			const appRoot = await this._appRoot(true);
			const importsRoot = await appRoot.getDirectoryHandle("imports", { create: true });
			const datasetRoot = await importsRoot.getDirectoryHandle(datasetId, { create: true });
			let bytesDone = 0;
			try {
				for (let index = 0; index < validation.entries.length; index++) {
					const entry = validation.entries[index];
					const parts = entry.path.split("/");
					const parent = await getOPFSDirectory(datasetRoot, parts.slice(0, -1), true);
					const blob = await entry.getBlob();
					if (!blob || blob.size !== entry.size) {
						throw new ImportError("FILE_CHANGED", "A selected file changed while it was being imported. Select the folder again.");
					}
					await writeBlobToOPFS(await parent.getFileHandle(parts[parts.length - 1], { create: true }), blob);
					bytesDone += entry.size;
					if (onProgress) onProgress({ phase: "store", filesDone: index + 1, filesTotal: validation.fileCount, bytesDone, bytesTotal: validation.totalBytes });
				}
				const previous = await this.load().catch(() => null);
				const metadata = metadataFor(validation, this.backend, datasetId);
				const metadataBlob = new Blob([JSON.stringify(metadata)], { type: "application/json" });
				await writeBlobToOPFS(await appRoot.getFileHandle("current.json", { create: true }), metadataBlob);
				if (previous && previous.metadata.datasetId !== datasetId) {
					try { await importsRoot.removeEntry(previous.metadata.datasetId, { recursive: true }); } catch (_) { /* best effort */ }
				}
				return metadata;
			} catch (error) {
				try { await importsRoot.removeEntry(datasetId, { recursive: true }); } catch (_) { /* best effort */ }
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

	async function openDatabase(name) {
		if (!global.indexedDB) throw new ImportError("NO_STORAGE", "This browser does not provide persistent local storage for the imported data.");
		const request = global.indexedDB.open(name, SCHEMA_VERSION);
		request.onupgradeneeded = () => {
			const database = request.result;
			if (!database.objectStoreNames.contains("metadata")) database.createObjectStore("metadata");
			if (!database.objectStoreNames.contains("files")) {
				const files = database.createObjectStore("files", { keyPath: "key" });
				files.createIndex("datasetId", "datasetId", { unique: false });
			}
		};
		return requestPromise(request);
	}

	class IndexedDBStorage {
		constructor(databaseName) {
			this.databaseName = databaseName || DEFAULT_DATABASE_NAME;
			this.backend = "indexeddb";
			this.databasePromise = null;
		}

		_db() {
			if (!this.databasePromise) this.databasePromise = openDatabase(this.databaseName);
			return this.databasePromise;
		}

		async load() {
			try {
				const database = await this._db();
				const transaction = database.transaction("metadata", "readonly");
				const metadata = await requestPromise(transaction.objectStore("metadata").get("current"));
				await transactionPromise(transaction);
				if (!metadata) return null;
				validateMetadata(metadata);
				return {
					metadata,
					getBlob: async path => {
						const db = await this._db();
						const tx = db.transaction("files", "readonly");
						const key = metadata.datasetId + "\0" + canonicalizeRelativePath(path);
						const record = await requestPromise(tx.objectStore("files").get(key));
						await transactionPromise(tx);
						if (!record || !(record.blob instanceof Blob)) {
							throw new ImportError("STORAGE_MISSING_FILE", "Saved UT99 data is incomplete. Clear it and import again.");
						}
						return record.blob;
					},
				};
			} catch (error) {
				if (error instanceof ImportError) throw error;
				throw new ImportError("STORAGE_READ", "Saved UT99 data could not be read. Clear it and import again.");
			}
		}

		async _deleteDataset(datasetId) {
			if (!datasetId) return;
			const database = await this._db();
			const transaction = database.transaction("files", "readwrite");
			const index = transaction.objectStore("files").index("datasetId");
			const request = index.openKeyCursor(IDBKeyRange.only(datasetId));
			request.onsuccess = () => {
				const cursor = request.result;
				if (cursor) {
					transaction.objectStore("files").delete(cursor.primaryKey);
					cursor.continue();
				}
			};
			await transactionPromise(transaction);
		}

		async save(validation, onProgress) {
			const database = await this._db();
			const previous = await this.load().catch(() => null);
			const datasetId = Date.now().toString(36) + "-" + Math.random().toString(36).slice(2);
			let bytesDone = 0;
			try {
				const batchSize = 64;
				for (let start = 0; start < validation.entries.length; start += batchSize) {
					const batch = validation.entries.slice(start, start + batchSize);
					const blobs = await Promise.all(batch.map(entry => entry.getBlob()));
					const transaction = database.transaction("files", "readwrite");
					for (let offset = 0; offset < batch.length; offset++) {
						const entry = batch[offset];
						const blob = blobs[offset];
						if (!blob || blob.size !== entry.size) {
							transaction.abort();
							throw new ImportError("FILE_CHANGED", "A selected file changed while it was being imported. Select the folder again.");
						}
						transaction.objectStore("files").put({ key: datasetId + "\0" + entry.path, datasetId, path: entry.path, size: entry.size, blob });
						bytesDone += entry.size;
					}
					await transactionPromise(transaction);
					if (onProgress) onProgress({ phase: "store", filesDone: Math.min(start + batch.length, validation.fileCount), filesTotal: validation.fileCount, bytesDone, bytesTotal: validation.totalBytes });
				}
				const metadata = metadataFor(validation, this.backend, datasetId);
				const publish = database.transaction("metadata", "readwrite");
				publish.objectStore("metadata").put(metadata, "current");
				await transactionPromise(publish);
				if (previous && previous.metadata.datasetId !== datasetId) {
					this._deleteDataset(previous.metadata.datasetId).catch(() => {});
				}
				return metadata;
			} catch (error) {
				await this._deleteDataset(datasetId).catch(() => {});
				throw error;
			}
		}

		async clear() {
			const database = await this._db();
			const transaction = database.transaction(["metadata", "files"], "readwrite");
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
				return new OPFSStorage(root, settings.opfsDirectory);
			} catch (_) {
				// IndexedDB remains the compatibility fallback when OPFS cannot open.
			}
		}
		return new IndexedDBStorage(settings.databaseName);
	}

	async function storageDiagnostics(requiredBytes, requestPersistence) {
		const result = {
			requiredBytes: Number(requiredBytes) || 0,
			usage: null,
			quota: null,
			available: null,
			persisted: null,
			persistRequested: false,
		};
		const storage = global.navigator && global.navigator.storage;
		if (!storage) return result;
		if (typeof storage.estimate === "function") {
			try {
				const estimate = await storage.estimate();
				result.usage = Number.isFinite(estimate.usage) ? estimate.usage : null;
				result.quota = Number.isFinite(estimate.quota) ? estimate.quota : null;
				result.available = result.usage !== null && result.quota !== null ? Math.max(0, result.quota - result.usage) : null;
			} catch (_) { /* diagnostics are best effort */ }
		}
		if (typeof storage.persisted === "function") {
			try { result.persisted = await storage.persisted(); } catch (_) { /* best effort */ }
		}
		if (requestPersistence && result.persisted !== true && typeof storage.persist === "function") {
			result.persistRequested = true;
			try { result.persisted = await storage.persist(); } catch (_) { /* best effort */ }
		}
		return result;
	}

	function ensureFSDirectory(FS, path) {
		if (typeof FS.mkdirTree === "function") {
			FS.mkdirTree(path);
			return;
		}
		let current = "";
		for (const part of path.split("/").filter(Boolean)) {
			current += "/" + part;
			try { FS.mkdir(current); } catch (_) { /* already exists */ }
		}
	}

	async function writeBlobToFS(FS, path, blob) {
		const stream = typeof blob.stream === "function" ? blob.stream() : null;
		const handle = FS.open(path, "w");
		let position = 0;
		try {
			if (stream && typeof stream.getReader === "function") {
				const reader = stream.getReader();
				while (true) {
					const chunk = await reader.read();
					if (chunk.done) break;
					FS.write(handle, chunk.value, 0, chunk.value.byteLength, position);
					position += chunk.value.byteLength;
				}
			} else {
				const bytes = new Uint8Array(await blob.arrayBuffer());
				for (let offset = 0; offset < bytes.byteLength; offset += COPY_CHUNK_BYTES) {
					const length = Math.min(COPY_CHUNK_BYTES, bytes.byteLength - offset);
					FS.write(handle, bytes, offset, length, position);
					position += length;
				}
			}
		} finally {
			FS.close(handle);
		}
		if (position !== blob.size) throw new ImportError("MATERIALIZE_SIZE", "A saved UT99 file could not be restored completely. Clear it and import again.");
	}

	async function materializeDataset(FS, dataset, onProgress, rootPath) {
		if (!FS || typeof FS.open !== "function" || typeof FS.write !== "function") {
			throw new ImportError("NO_EMSCRIPTEN_FS", "The game runtime filesystem is unavailable.");
		}
		const root = rootPath || GAME_ROOT;
		ensureFSDirectory(FS, root);
		let bytesDone = 0;
		for (let index = 0; index < dataset.metadata.files.length; index++) {
			const file = dataset.metadata.files[index];
			const path = canonicalizeRelativePath(file.path);
			const parts = path.split("/");
			ensureFSDirectory(FS, root + "/" + parts.slice(0, -1).join("/"));
			const blob = await dataset.getBlob(path);
			if (!blob || blob.size !== file.size) {
				throw new ImportError("STORAGE_FILE_SIZE", "Saved UT99 data is incomplete or changed. Clear it and import again.");
			}
			await writeBlobToFS(FS, root + "/" + path, blob);
			bytesDone += file.size;
			if (onProgress) onProgress({ phase: "restore", filesDone: index + 1, filesTotal: dataset.metadata.fileCount, bytesDone, bytesTotal: dataset.metadata.totalBytes });
		}
	}

	function fsPathExists(FS, path) {
		try {
			if (typeof FS.analyzePath === "function") return !!FS.analyzePath(path).exists;
			FS.stat(path);
			return true;
		} catch (_) {
			return false;
		}
	}

	function hasDeveloperPreload(FS, rootPath) {
		const root = rootPath || GAME_ROOT;
		// The original GOTY media spells BotPack.u with a capital P, while
		// engine code and some patched distributions use Botpack.u. MEMFS is
		// case-sensitive, so accept both known spellings at this boot seam.
		const packages = fsPathExists(FS, root + "/System/Core.u") &&
			fsPathExists(FS, root + "/System/Engine.u") &&
			(fsPathExists(FS, root + "/System/Botpack.u") || fsPathExists(FS, root + "/System/BotPack.u"));
		const executable = UT_EXECUTABLE_NAMES.some(name => fsPathExists(FS, root + "/System/" + name) || fsPathExists(FS, root + "/System64/" + name));
		return packages && executable;
	}

	class ImporterUI {
		constructor(root) {
			this.root = root || null;
			this.status = root && root.querySelector("[data-ut99-status]");
			this.error = root && root.querySelector("[data-ut99-error]");
			this.progress = root && root.querySelector("[data-ut99-progress]");
			this.progressText = root && root.querySelector("[data-ut99-progress-text]");
			this.diagnostics = root && root.querySelector("[data-ut99-storage]");
			this.pickButton = root && root.querySelector("[data-ut99-pick]");
			this.fileInput = root && root.querySelector("[data-ut99-files]");
			this.fileLabel = root && root.querySelector("[data-ut99-file-label]");
			this.clearButton = root && root.querySelector("[data-ut99-clear]");
		}

		show() { if (this.root) this.root.hidden = false; }
		hide() { if (this.root) this.root.hidden = true; }
		setBusy(busy) {
			for (const control of [this.pickButton, this.fileInput, this.clearButton]) if (control) control.disabled = !!busy;
		}
		setStatus(message) { if (this.status) this.status.textContent = message || ""; }
		setError(message) {
			if (!this.error) return;
			this.error.textContent = message || "";
			this.error.hidden = !message;
		}
		setProgress(progress) {
			if (!this.progress) return;
			const total = progress.bytesTotal || progress.filesTotal || 0;
			const done = progress.bytesTotal ? progress.bytesDone : progress.filesDone;
			this.progress.max = Math.max(1, total);
			this.progress.value = Math.min(this.progress.max, done || 0);
			if (this.progressText) {
				if (progress.bytesTotal) this.progressText.textContent = formatBytes(progress.bytesDone) + " / " + formatBytes(progress.bytesTotal) + " (" + progress.filesDone + " / " + progress.filesTotal + " files)";
				else this.progressText.textContent = (progress.filesDone || 0) + " files scanned";
			}
		}
		setDiagnostics(diagnostic, backend) {
			if (!this.diagnostics) return;
			const parts = ["Storage: " + backend];
			if (diagnostic.usage !== null && diagnostic.quota !== null) parts.push(formatBytes(diagnostic.usage) + " used of " + formatBytes(diagnostic.quota));
			if (diagnostic.persisted === true) parts.push("persistent storage granted");
			else if (diagnostic.persistRequested) parts.push("persistent storage not granted; browser eviction remains possible");
			this.diagnostics.textContent = parts.join("; ");
		}
	}

	class ImportController {
		constructor(Module, options) {
			this.Module = Module;
			this.options = options || {};
			this.ui = new ImporterUI(this.options.uiRoot || null);
			this.storage = null;
			this.launched = false;
			this.launchPromise = null;
			this.busy = false;
			this.lastError = null;
			this._currentMapManifest = immutableMapManifest("unavailable", [], 0);
		}

		mapManifest() {
			return immutableMapManifest(
				this._currentMapManifest.state,
				this._currentMapManifest.maps,
				this._currentMapManifest.rejectedCount);
		}

		_setMapManifest(metadata, state) {
			try {
				this._currentMapManifest = mapManifestFromMetadata(metadata, state);
			} catch (_error) {
				// Browsing maps is optional product UX. Import validation and the
				// main boot gate remain authoritative if this defensive projection
				// ever rejects an otherwise loadable future metadata schema.
				this._currentMapManifest = immutableMapManifest("error", [], 0);
			}
			return this.mapManifest();
		}

		async clearSavedImport() {
			if (!this.storage || typeof this.storage.clear !== "function") {
				throw new ImportError("STORAGE_UNAVAILABLE", "Saved UT99 data storage is unavailable.");
			}
			await this.storage.clear();
			return this._setMapManifest(null, "cleared");
		}

		_log(message) {
			if (typeof this.options.log === "function") this.options.log("[data] " + message);
		}

		async initialize() {
			if (hasDeveloperPreload(this.Module.FS, this.options.rootPath)) {
				this._setMapManifest(null, "unavailable");
				this._log("developer-preloaded /gamedata detected; local importer bypassed");
				this.ui.hide();
				await this._launch("developer-preload");
				return { state: "launched", mode: "developer-preload", backend: "embedded" };
			}
			this.storage = await createStorage(this.options);
			this._bindControls();
			const diagnostic = await storageDiagnostics(0, false);
			this.ui.setDiagnostics(diagnostic, this.storage.backend);

			this.ui.show();
			this.ui.setStatus("Checking for saved UT99 data…");
			try {
				const dataset = await this.storage.load();
				if (dataset) {
					this.ui.setBusy(true);
					this.ui.setStatus("Restoring saved UT99 data…");
					await materializeDataset(this.Module.FS, dataset, progress => this.ui.setProgress(progress), this.options.rootPath);
					this._setMapManifest(dataset.metadata, "ready");
					this.ui.setStatus("Saved UT99 data restored. Starting SurrealEngine…");
					this._log("restored " + dataset.metadata.fileCount + " local files (" + formatBytes(dataset.metadata.totalBytes) + ") from " + this.storage.backend);
					await this._launch("persistent-import");
					return { state: "launched", mode: "persistent-import", backend: this.storage.backend, metadata: dataset.metadata };
				}
			} catch (error) {
				this._setMapManifest(null, "error");
				this.lastError = error;
				this.ui.setError(safeMessage(error));
				this._log("saved import is unavailable or corrupt; user action required");
			}
			if (this._currentMapManifest.state !== "error") this._setMapManifest(null, "empty");
			this.ui.setBusy(false);
			this.ui.setStatus("Select the folder from your own Unreal Tournament installation. Data stays on this device.");
			return { state: "waiting-for-import", backend: this.storage.backend, error: this.lastError };
		}

		_bindControls() {
			if (this.ui.pickButton) {
				if (typeof global.showDirectoryPicker !== "function") this.ui.pickButton.hidden = true;
				this.ui.pickButton.addEventListener("click", async () => {
					try {
						const directory = await global.showDirectoryPicker({ mode: "read" });
						const entries = await entriesFromDirectoryHandle(directory, progress => this.ui.setProgress(progress));
						await this.importEntries(entries);
					} catch (error) { this._handleImportError(error); }
				});
			}
			if (this.ui.fileInput) {
				this.ui.fileInput.addEventListener("change", async event => {
					try { await this.importEntries(entriesFromFileList(event.target.files)); }
					catch (error) { this._handleImportError(error); }
					finally { event.target.value = ""; }
				});
			}
			if (this.ui.clearButton) {
				this.ui.clearButton.addEventListener("click", async () => {
					if (this.busy) return;
					this.ui.setBusy(true);
					this.ui.setError("");
					try {
						await this.clearSavedImport();
						this.ui.setStatus(this.launched ? "Saved import cleared. Reload to choose data again." : "Saved import cleared. Select your UT99 installation folder.");
						this._log("saved local UT99 import cleared");
					} catch (_) {
						this.ui.setError("The browser could not clear the saved import. Check site storage permissions.");
					} finally {
						this.ui.setBusy(false);
					}
				});
			}
		}

		_handleImportError(error) {
			this.busy = false;
			this.lastError = error;
			this.ui.setBusy(false);
			if (!error || error.name !== "AbortError") this.ui.setError(safeMessage(error));
			if (error && error.name === "AbortError") this.ui.setStatus("Folder selection cancelled. No data was changed.");
		}

		async importEntries(entries) {
			if (this.busy) throw new ImportError("BUSY", "An import is already in progress.");
			this.busy = true;
			this.ui.show();
			this.ui.setBusy(true);
			this.ui.setError("");
			try {
				this.ui.setStatus("Validating the selected UT99 installation…");
				const validation = validateEntries(entries);
				const diagnostic = await storageDiagnostics(validation.totalBytes, true);
				this.ui.setDiagnostics(diagnostic, this.storage.backend);
				if (diagnostic.available !== null && diagnostic.available < validation.totalBytes) {
					throw new ImportError("QUOTA", "Not enough browser storage is available. The import needs " + formatBytes(validation.totalBytes) + " but only about " + formatBytes(diagnostic.available) + " is free. Clear site data or free device storage, then try again.");
				}
				this.ui.setStatus("Copying UT99 data into private browser storage… Keep this page open.");
				const metadata = await this.storage.save(validation, progress => this.ui.setProgress(progress));
				if (this.launched) {
					this.ui.setStatus("Replacement import saved. Reloading to use it…");
					this._log("replacement UT99 import saved; reloading before changing the live game filesystem");
					if (typeof this.options.onReimportReady === "function") this.options.onReimportReady(metadata);
					else if (global.location && typeof global.location.reload === "function") global.location.reload();
					return metadata;
				}
				const entriesByPath = new Map(validation.entries.map(entry => [entry.path, entry]));
				const sourceDataset = {
					metadata,
					getBlob: async path => {
						const entry = entriesByPath.get(path);
						if (!entry) throw new ImportError("SOURCE_MISSING", "A selected file disappeared during import. Select the folder again.");
						return entry.getBlob();
					},
				};
				this.ui.setStatus("Preparing UT99 data for SurrealEngine…");
				await materializeDataset(this.Module.FS, sourceDataset, progress => this.ui.setProgress(progress), this.options.rootPath);
				this._setMapManifest(metadata, "ready");
				this.ui.setStatus("Import complete. Starting SurrealEngine…");
				this._log("imported " + metadata.fileCount + " local files (" + formatBytes(metadata.totalBytes) + ") into " + this.storage.backend);
				await this._launch("new-import");
				return metadata;
			} catch (error) {
				this.lastError = error;
				this.ui.setError(safeMessage(error));
				this.ui.setStatus("Import did not finish. No game data was sent anywhere; fix the issue and try again.");
				throw error;
			} finally {
				this.busy = false;
				this.ui.setBusy(false);
			}
		}

		async _launch(mode) {
			if (this.launched) return;
			if (this.launchPromise) return this.launchPromise;
			this.launchPromise = (async () => {
				// Persistence overlays must run after /gamedata exists but before
				// native startup reads configuration and enumerates save files.
				if (typeof this.options.beforeLaunch === "function") await this.options.beforeLaunch(mode);
				this.launched = true;
				this.ui.setBusy(false);
				if (typeof this.options.launch === "function") {
					await this.options.launch(mode, { mapManifest: this.mapManifest() });
				}
			})();
			try { await this.launchPromise; }
			finally { if (!this.launched) this.launchPromise = null; }
		}
	}

	async function start(Module, options) {
		if (!Module || !Module.FS) throw new ImportError("NO_MODULE", "The SurrealEngine runtime is not initialized.");
		const controller = new ImportController(Module, options);
		const result = await controller.initialize();
		return { controller, result };
	}

	global.SurrealUT99Importer = Object.freeze({
		SCHEMA_NAME,
		SCHEMA_VERSION,
		MAP_MANIFEST_SCHEMA,
		MAP_MANIFEST_VERSION,
		GAME_ROOT,
		ImportError,
		OPFSStorage,
		IndexedDBStorage,
		ImportController,
		canonicalizeRelativePath,
		entriesFromFileList,
		entriesFromDirectoryHandle,
		validateEntries,
		validateMetadata,
		isSafeMapBasename,
		mapManifestFromMetadata,
		selectLaunchMap,
		createStorage,
		storageDiagnostics,
		ensureFSDirectory,
		writeBlobToFS,
		fsPathExists,
		materializeDataset,
		hasDeveloperPreload,
		formatBytes,
		start,
	});
})(typeof window !== "undefined" ? window : globalThis);
