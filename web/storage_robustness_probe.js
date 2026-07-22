/*
 * Browser-side fixture for qualify_storage_robustness.py.
 *
 * Every persistent name is derived from a driver-supplied run id. The probe
 * never opens the production importer or mutable-data namespace, and every
 * byte is an explicit synthetic marker rather than UT99 content.
 */
(function (global) {
	"use strict";

	const PROBE_SCHEMA = "surrealengine-storage-robustness-probe";
	const PROBE_VERSION = 2;
	const SYNTHETIC_MARKER = "SURREALENGINE_SYNTHETIC_STORAGE_TEST_ONLY";
	const MUTABLE_PATHS = Object.freeze([
		"/gamedata/System/SE-User.ini",
		"/gamedata/Save/Save0.usa",
	]);

	function assertRunId(runId) {
		if (typeof runId !== "string" || !/^[a-z0-9-]{8,80}$/.test(runId))
			throw new Error("invalid storage robustness run id");
		return runId;
	}

	function names(runId) {
		const id = assertRunId(runId);
		return Object.freeze({
			mutableOPFS: "se-storage-robustness-" + id + "-mutable-opfs",
			mutableIDB: "se-storage-robustness-" + id + "-mutable-idb",
			importerOPFS: "se-storage-robustness-" + id + "-importer-opfs",
		});
	}

	function blob(text, paddingBytes) {
		const pieces = [SYNTHETIC_MARKER + "\n" + text];
		if (paddingBytes) pieces.push(new Uint8Array(paddingBytes));
		return new Blob(pieces, { type: "application/octet-stream" });
	}

	function entry(path, text, paddingBytes) {
		const value = blob(text, paddingBytes || 0);
		return { path, size: value.size, getBlob: async () => value };
	}

	function mutableEntries(tag, paddingBytes) {
		return [
			entry(MUTABLE_PATHS[0], "mutable-user:" + tag, paddingBytes || 0),
			entry(MUTABLE_PATHS[1], "mutable-save:" + tag, 0),
		];
	}

	function importerEntries(tag) {
		return [
			["System/Core.u", "core"], ["System/Engine.u", "engine"],
			["System/Botpack.u", "botpack"], ["System/UnrealTournament.ini", "ini"],
			["System/UnrealTournament.exe", "exe"], ["Maps/DM-Synthetic.unr", "map"],
			["Textures/Synthetic.utx", "texture"], ["Sounds/Synthetic.uax", "sound"],
			["Music/Synthetic.umx", "music"],
		].map(item => entry(item[0], "importer-" + item[1] + ":" + tag, 0));
	}

	async function stores(runId) {
		const api = global.SurrealMutableData;
		const importer = global.SurrealUT99Importer;
		if (!api || !importer) throw new Error("storage runtime APIs are unavailable");
		if (!navigator.storage || typeof navigator.storage.getDirectory !== "function")
			throw new Error("OPFS is unavailable");
		const root = await navigator.storage.getDirectory();
		const storeNames = names(runId);
		return {
			api,
			importer,
			names: storeNames,
			root,
			mutableOPFS: new api.OPFSStorage(root, storeNames.mutableOPFS),
			mutableIDB: new api.IndexedDBStorage(storeNames.mutableIDB),
			importerOPFS: new importer.OPFSStorage(root, storeNames.importerOPFS),
		};
	}

	async function readDataset(dataset, paths) {
		if (!dataset) return null;
		const values = {};
		for (const path of paths) values[path] = await (await dataset.getBlob(path)).text();
		return {
			datasetId: dataset.metadata.datasetId,
			version: dataset.metadata.version,
			fileCount: dataset.metadata.fileCount,
			totalBytes: dataset.metadata.totalBytes,
			values,
		};
	}

	async function seed(runId, tag) {
		const value = await stores(runId);
		const mutable = mutableEntries(tag);
		const importerValidation = value.importer.validateEntries(importerEntries(tag));
		const result = {
			opfs: await value.mutableOPFS.save(mutable),
			indexeddb: await value.mutableIDB.save(mutable),
			importer: await value.importerOPFS.save(importerValidation),
		};
		return { schema: PROBE_SCHEMA, version: PROBE_VERSION, tag, result };
	}

	async function inspect(runId) {
		const value = await stores(runId);
		return {
			schema: PROBE_SCHEMA,
			version: PROBE_VERSION,
			names: value.names,
			opfs: await readDataset(await value.mutableOPFS.load(), MUTABLE_PATHS),
			indexeddb: await readDataset(await value.mutableIDB.load(), MUTABLE_PATHS),
			importer: await readDataset(await value.importerOPFS.load(), ["System/Core.u", "Maps/DM-Synthetic.unr"]),
		};
	}

	async function clearMutableOnly(runId) {
		const value = await stores(runId);
		await value.mutableOPFS.clear();
		await value.mutableIDB.clear();
		return {
			mutableOPFS: await value.mutableOPFS.load(),
			mutableIDB: await value.mutableIDB.load(),
			importer: await readDataset(await value.importerOPFS.load(), ["System/Core.u"]),
		};
	}

	function legacyMetadata(metadata) {
		const result = Object.assign({}, metadata, { version: 1 });
		delete result.format;
		delete result.migration;
		return result;
	}

	async function seedLegacyMigration(runId, tag) {
		const value = await stores(runId);
		await Promise.all([value.mutableOPFS.clear(), value.mutableIDB.clear()]);
		const mutable = mutableEntries(tag);
		const opfsMetadata = await value.mutableOPFS.save(mutable);
		const idbMetadata = await value.mutableIDB.save(mutable);
		const opfsRoot = await value.root.getDirectoryHandle(value.names.mutableOPFS);
		const current = await opfsRoot.getFileHandle("current.json");
		const writable = await current.createWritable();
		await writable.write(JSON.stringify(legacyMetadata(opfsMetadata)));
		await writable.close();
		const db = await value.mutableIDB._db();
		const transaction = db.transaction("metadata", "readwrite");
		transaction.objectStore("metadata").put(legacyMetadata(idbMetadata), "current");
		await transactionDone(transaction);
		const importer = await readDataset(await value.importerOPFS.load(), ["System/Core.u"]);
		return {
			schema: PROBE_SCHEMA,
			version: PROBE_VERSION,
			tag,
			legacyDatasetIds: { opfs: opfsMetadata.datasetId, indexeddb: idbMetadata.datasetId },
			importerDatasetId: importer && importer.datasetId,
		};
	}

	const migrationInterruption = {
		reachedBeforePublish: false,
		promise: null,
	};

	async function beginInterruptedMigration(runId) {
		const value = await stores(runId);
		const storage = new value.api.OPFSStorage(value.root, value.names.mutableOPFS, {
			migrationHook: phase => {
				if (phase !== "before-publish") return;
				migrationInterruption.reachedBeforePublish = true;
				return new Promise(() => {});
			},
		});
		migrationInterruption.promise = storage.load();
		migrationInterruption.promise.catch(() => {});
		return { started: true };
	}

	function migrationInterruptionStatus() {
		return { reachedBeforePublish: migrationInterruption.reachedBeforePublish };
	}

	async function inspectMigrationPointer(runId) {
		const value = await stores(runId);
		const opfsRoot = await value.root.getDirectoryHandle(value.names.mutableOPFS);
		const current = await opfsRoot.getFileHandle("current.json");
		const metadata = JSON.parse(await (await current.getFile()).text());
		return { version: metadata.version, datasetId: metadata.datasetId };
	}

	async function retryAndInspectMigration(runId) {
		const value = await stores(runId);
		const opfs = await value.mutableOPFS.load();
		const indexeddb = await value.mutableIDB.load();
		const importer = await readDataset(await value.importerOPFS.load(), ["System/Core.u"]);
		const secondOPFS = await new value.api.OPFSStorage(value.root, value.names.mutableOPFS).load();
		const secondIDB = await new value.api.IndexedDBStorage(value.names.mutableIDB).load();
		return {
			schema: PROBE_SCHEMA,
			version: PROBE_VERSION,
			opfs: await readDataset(opfs, MUTABLE_PATHS),
			indexeddb: await readDataset(indexeddb, MUTABLE_PATHS),
			importer,
			diagnostics: {
				opfs: value.mutableOPFS.migrationDiagnostics(),
				indexeddb: value.mutableIDB.migrationDiagnostics(),
			},
			idempotentReloadDatasetIds: {
				opfs: secondOPFS && secondOPFS.metadata.datasetId,
				indexeddb: secondIDB && secondIDB.metadata.datasetId,
			},
		};
	}

	const interruption = {
		opfsReachedPartialSnapshot: false,
		importerReachedPartialDataset: false,
		idbSaveStarted: false,
		opfsPromise: null,
		idbPromise: null,
		importerPromise: null,
	};

	async function beginAbruptWrites(runId, tag) {
		const value = await stores(runId);
		const first = entry(MUTABLE_PATHS[1], "mutable-save:" + tag, 0);
		const secondBlob = blob("mutable-user:" + tag, 0);
		const second = {
			path: MUTABLE_PATHS[0],
			size: secondBlob.size,
			getBlob: () => {
				// OPFSStorage requests entries sequentially. Reaching this provider
				// proves the preceding synthetic file reached the new, uncommitted
				// snapshot while current.json still names the old snapshot.
				interruption.opfsReachedPartialSnapshot = true;
				return new Promise(() => {});
			},
		};
		interruption.opfsPromise = value.mutableOPFS.save([first, second]);
		interruption.opfsPromise.catch(() => {});

		// IndexedDBStorage gathers blobs, then writes files and metadata in one
		// transaction. The exact cut point is intentionally reported as unknown:
		// after a process kill either complete generation is valid, never a mix.
		interruption.idbSaveStarted = true;
		interruption.idbPromise = value.mutableIDB.save(mutableEntries(tag, 16 * 1024 * 1024));
		interruption.idbPromise.catch(() => {});

		let importerReads = 0;
		const interruptedImporterEntries = importerEntries(tag).map(source => ({
			path: source.path,
			size: source.size,
			getBlob: async () => {
				importerReads++;
				if (importerReads === 2) {
					// The first synthetic importer file has reached its new dataset,
					// but current.json cannot be replaced until every file completes.
					interruption.importerReachedPartialDataset = true;
					return new Promise(() => {});
				}
				return source.getBlob();
			},
		}));
		interruption.importerPromise = value.importerOPFS.save(
			value.importer.validateEntries(interruptedImporterEntries));
		interruption.importerPromise.catch(() => {});
		return { started: true };
	}

	function interruptionStatus() {
		return {
			opfsReachedPartialSnapshot: interruption.opfsReachedPartialSnapshot,
			importerReachedPartialDataset: interruption.importerReachedPartialDataset,
			idbSaveStarted: interruption.idbSaveStarted,
		};
	}

	class SyntheticFS {
		constructor() {
			this.files = new Map([["/gamedata/System/Core.u", new TextEncoder().encode(SYNTHETIC_MARKER + ":immutable-sentinel")]]);
			this.directories = new Set(["/", "/gamedata", "/gamedata/System"]);
		}
		analyzePath(path) { return { exists: this.files.has(path) || this.directories.has(path) }; }
		stat(path) {
			if (this.files.has(path)) return { mode: 1, type: "file" };
			if (this.directories.has(path)) return { mode: 2, type: "directory" };
			throw new Error("ENOENT: " + path);
		}
		isFile(mode) { return mode === 1; }
		readdir(path) {
			if (path === "/gamedata/Save") return [".", ".."];
			if (!this.directories.has(path)) throw new Error("ENOENT: " + path);
			return [".", ".."];
		}
		mkdirTree(path) {
			let current = "";
			for (const part of path.split("/").filter(Boolean)) {
				current += "/" + part;
				this.directories.add(current);
			}
		}
		readFile(path) {
			if (!this.files.has(path)) throw new Error("ENOENT: " + path);
			return new Uint8Array(this.files.get(path));
		}
		writeFile(path, bytes) {
			this.mkdirTree(path.slice(0, path.lastIndexOf("/")));
			this.files.set(path, new Uint8Array(bytes));
		}
		text(path) { return new TextDecoder().decode(this.files.get(path)); }
	}

	async function expectControllerFailure(api, storage, expectedCode) {
		const fs = new SyntheticFS();
		const controller = new api.MutableDataController({ FS: fs }, { storage, checkpointIntervalMs: 0 });
		const status = await controller.initialize();
		let flushCode = null;
		try { await controller.flush("must-not-overwrite-corrupt-store"); }
		catch (error) { flushCode = error && error.code; }
		const immutableSentinel = fs.text("/gamedata/System/Core.u");
		controller.dispose();
		return {
			status,
			expectedCode,
			flushCode,
			immutableSentinel,
			failedClosed: status.state === "restore-failed" && status.requiresClear === true &&
				status.automaticCheckpoints === false && status.error.includes(expectedCode) &&
				flushCode === "CLEAR_REQUIRED" && immutableSentinel === SYNTHETIC_MARKER + ":immutable-sentinel",
		};
	}

	async function expectImporterFailure(importer, storage, expectedCode) {
		const fs = new SyntheticFS();
		let launches = 0;
		const controller = new importer.ImportController({ FS: fs }, {
			storage,
			launch: () => { launches++; },
		});
		const status = await controller.initialize();
		const immutableSentinel = fs.text("/gamedata/System/Core.u");
		return {
			status: {
				state: status.state,
				backend: status.backend,
				errorCode: status.error && status.error.code,
			},
			expectedCode,
			launches,
			immutableSentinel,
			failedClosed: status.state === "waiting-for-import" && status.error &&
				status.error.code === expectedCode && launches === 0 &&
				immutableSentinel === SYNTHETIC_MARKER + ":immutable-sentinel",
		};
	}

	async function transactionDone(transaction) {
		return new Promise((resolve, reject) => {
			transaction.oncomplete = resolve;
			transaction.onerror = () => reject(transaction.error || new Error("IndexedDB transaction failed"));
			transaction.onabort = () => reject(transaction.error || new Error("IndexedDB transaction aborted"));
		});
	}

	async function injectAndCheckCorruption(runId) {
		const value = await stores(runId);
		const results = {};

		// A future OPFS schema must not be guessed at or overwritten.
		await value.mutableOPFS.save(mutableEntries("corruption-opfs-baseline"));
		const opfsRoot = await value.root.getDirectoryHandle(value.names.mutableOPFS);
		const currentHandle = await opfsRoot.getFileHandle("current.json");
		const currentMetadata = JSON.parse(await (await currentHandle.getFile()).text());
		const futureMetadata = Object.assign({}, currentMetadata, { version: value.api.SCHEMA_VERSION + 1 });
		const writable = await currentHandle.createWritable();
		await writable.write(JSON.stringify(futureMetadata));
		await writable.close();
		results.futureOPFS = await expectControllerFailure(value.api, value.mutableOPFS, "STORAGE_SCHEMA");
		results.futureOPFS.pointerVersionAfterRefusal = JSON.parse(await (await currentHandle.getFile()).text()).version;

		// A missing committed snapshot must also fail closed before writing MEMFS.
		await value.mutableOPFS.clear();
		const missingMetadata = await value.mutableOPFS.save(mutableEntries("corruption-missing-snapshot"));
		const missingRoot = await value.root.getDirectoryHandle(value.names.mutableOPFS);
		const snapshots = await missingRoot.getDirectoryHandle("snapshots");
		await snapshots.removeEntry(missingMetadata.datasetId, { recursive: true });
		results.missingOPFS = await expectControllerFailure(value.api, value.mutableOPFS, "STORAGE_MISSING_DATASET");

		// IndexedDB future schemas and missing blobs exercise the same controller
		// policy through the other production backend.
		await value.mutableIDB.clear();
		const idbMetadata = await value.mutableIDB.save(mutableEntries("corruption-idb-baseline"));
		const db = await value.mutableIDB._db();
		let transaction = db.transaction("metadata", "readwrite");
		transaction.objectStore("metadata").put(Object.assign({}, idbMetadata, { version: value.api.SCHEMA_VERSION + 1 }), "current");
		await transactionDone(transaction);
		results.futureIDB = await expectControllerFailure(value.api, value.mutableIDB, "STORAGE_SCHEMA");
		transaction = db.transaction("metadata", "readonly");
		const persistedFuture = await new Promise((resolve, reject) => {
			const request = transaction.objectStore("metadata").get("current");
			request.onsuccess = () => resolve(request.result);
			request.onerror = () => reject(request.error);
		});
		await transactionDone(transaction);
		results.futureIDB.pointerVersionAfterRefusal = persistedFuture.version;

		await value.mutableIDB.clear();
		const missingIDBMetadata = await value.mutableIDB.save(mutableEntries("corruption-idb-missing-file"));
		transaction = db.transaction("files", "readwrite");
		transaction.objectStore("files").delete(missingIDBMetadata.datasetId + "\0" + MUTABLE_PATHS[0]);
		await transactionDone(transaction);
		results.missingIDB = await expectControllerFailure(value.api, value.mutableIDB, "STORAGE_MISSING_FILE");

		// The game-data importer independently rejects unsupported metadata and
		// missing committed datasets without launching native main or mutating
		// the synthetic immutable sentinel.
		await value.importerOPFS.clear();
		const importerValidation = value.importer.validateEntries(importerEntries("corruption-importer-future"));
		await value.importerOPFS.save(importerValidation);
		const importerRoot = await value.root.getDirectoryHandle(value.names.importerOPFS);
		const importerCurrent = await importerRoot.getFileHandle("current.json");
		const importerMetadata = JSON.parse(await (await importerCurrent.getFile()).text());
		const importerWritable = await importerCurrent.createWritable();
		await importerWritable.write(JSON.stringify(Object.assign({}, importerMetadata, { version: 2 })));
		await importerWritable.close();
		results.futureImporterOPFS = await expectImporterFailure(value.importer, value.importerOPFS, "STORAGE_SCHEMA");
		results.futureImporterOPFS.pointerVersionAfterRefusal =
			JSON.parse(await (await importerCurrent.getFile()).text()).version;

		await value.importerOPFS.clear();
		const importerMissingMetadata = await value.importerOPFS.save(
			value.importer.validateEntries(importerEntries("corruption-importer-missing")));
		const importerMissingRoot = await value.root.getDirectoryHandle(value.names.importerOPFS);
		const imports = await importerMissingRoot.getDirectoryHandle("imports");
		await imports.removeEntry(importerMissingMetadata.datasetId, { recursive: true });
		results.missingImporterOPFS = await expectImporterFailure(
			value.importer, value.importerOPFS, "STORAGE_MISSING_DATASET");

		return { schema: PROBE_SCHEMA, version: PROBE_VERSION, results };
	}

	async function cleanup(runId) {
		const value = await stores(runId);
		await Promise.all([value.mutableOPFS.clear(), value.mutableIDB.clear(), value.importerOPFS.clear()]);
		return true;
	}

	global.SurrealStorageRobustnessProbe = Object.freeze({
		PROBE_SCHEMA,
		PROBE_VERSION,
		SYNTHETIC_MARKER,
		names,
		seed,
		inspect,
		clearMutableOnly,
		seedLegacyMigration,
		beginInterruptedMigration,
		migrationInterruptionStatus,
		inspectMigrationPointer,
		retryAndInspectMigration,
		beginAbruptWrites,
		interruptionStatus,
		injectAndCheckCorruption,
		cleanup,
	});
})(typeof window !== "undefined" ? window : globalThis);
