#!/usr/bin/env node
import process from "node:process";
import {
	initializeVersionedRelease, promoteVersionedRelease, publishVersionedRelease,
	recoverVersionedRelease, rollbackVersionedRelease,
} from "./versioned_release_library.mjs";

function argumentsByName(values) {
	const result = new Map();
	for (const value of values) {
		if (!value.startsWith("--") || !value.includes("="))
			throw new Error(`Invalid argument: ${value}`);
		const [name, ...rest] = value.slice(2).split("=");
		result.set(name, rest.join("="));
	}
	return result;
}

function required(arguments_, name) {
	const value = arguments_.get(name);
	if (!value) throw new Error(`Pass --${name}=<value>.`);
	return value;
}

const operation = process.argv[2];
const arguments_ = argumentsByName(process.argv.slice(3));
if (arguments_.get("execute") !== "yes")
	throw new Error(`${operation || "Versioned release operation"} requires --execute=yes.`);

const common = {
	siteRoot: required(arguments_, "site-root"),
	liveRelative: arguments_.get("live-relative") || "Ports/SurrealEngine",
	intendedBasePath: arguments_.get("intended-base-path") || "/webxr/Ports/SurrealEngine/",
	recordPath: arguments_.get("record") || null,
};

let result;
if (operation === "initialize") result = await initializeVersionedRelease({
	...common, liveManifestSha256: required(arguments_, "live-manifest-sha256"),
});
else if (operation === "publish") result = await publishVersionedRelease({
	...common, candidate: required(arguments_, "candidate"),
	manifestSha256: required(arguments_, "manifest-sha256"),
});
else if (operation === "promote") result = await promoteVersionedRelease({
	...common,
	currentManifestSha256: required(arguments_, "current-manifest-sha256"),
	targetManifestSha256: required(arguments_, "target-manifest-sha256"),
});
else if (operation === "rollback") result = await rollbackVersionedRelease({
	...common,
	currentManifestSha256: required(arguments_, "current-manifest-sha256"),
	targetManifestSha256: required(arguments_, "target-manifest-sha256"),
});
else if (operation === "recover") result = await recoverVersionedRelease({
	...common,
	breakStaleLock: arguments_.get("break-stale-lock") === "yes",
	currentManifestSha256: arguments_.get("current-manifest-sha256") || null,
});
else throw new Error("Operation must be initialize, publish, promote, rollback, or recover.");

process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
