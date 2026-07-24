#!/usr/bin/env node
import process from "node:process";
import {
	promoteRelease, recoverRelease, rollbackRelease, stageRelease,
} from "./release_transaction_library.mjs";

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
const common = {
	siteRoot: required(arguments_, "site-root"),
	liveRelative: arguments_.get("live-relative") || "Ports/SurrealEngine",
	intendedBasePath: arguments_.get("intended-base-path") || "/webxr/Ports/SurrealEngine/",
	recordPath: arguments_.get("record") || null,
};
const mutation = {
	swapMode: arguments_.get("swap-mode") || (process.platform === "linux" ? "exchange" : "journaled"),
	exchangeCommand: arguments_.get("exchange-command") || null,
};

let result;
if (operation === "stage") {
	result = await stageRelease({
		...common, candidate: required(arguments_, "candidate"),
		manifestSha256: required(arguments_, "manifest-sha256"),
	});
} else if (["promote", "rollback", "recover"].includes(operation)) {
	if (!arguments_.has("execute"))
		throw new Error(`${operation} requires --execute=yes because it changes the stable release.`);
	if (arguments_.get("execute") !== "yes") throw new Error("--execute must equal yes.");
	if (operation === "promote") result = await promoteRelease({
		...common, ...mutation, stageName: required(arguments_, "stage-name"),
		manifestSha256: required(arguments_, "manifest-sha256"),
		liveManifestSha256: required(arguments_, "live-manifest-sha256"),
	});
	else if (operation === "rollback") result = await rollbackRelease({
		...common, ...mutation, rollbackName: required(arguments_, "rollback-name"),
		manifestSha256: required(arguments_, "manifest-sha256"),
		liveManifestSha256: required(arguments_, "live-manifest-sha256"),
	});
	else result = await recoverRelease({
		...common, ...mutation, breakStaleLock: arguments_.get("break-stale-lock") === "yes",
		liveManifestSha256: arguments_.get("live-manifest-sha256") || null,
	});
} else {
	throw new Error("Operation must be stage, promote, rollback, or recover.");
}

process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
