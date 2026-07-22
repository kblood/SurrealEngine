"use strict";

// Bump this for every deploy that changes shell/runtime compatibility. Old
// SurrealEngine caches are removed only after this version has installed all
// required shell files successfully.
const APP_VERSION = "2026.07.22-m10.3";
const CACHE_PREFIX = "surrealengine-webxr-";
const SHELL_CACHE = CACHE_PREFIX + APP_VERSION + "-shell";
const RUNTIME_CACHE = CACHE_PREFIX + APP_VERSION + "-runtime";

const SHELL_URLS = [
	"./index_webxr.html",
	"./offline.html",
	"./manifest.webmanifest",
	"./pwa_register.js",
	"./ut99_importer.js",
	"./mutable_persistence.js",
	"./webxr_settings.js",
	"./webxr_session.js",
	"../Assets/surreal-engine-icon.svg",
	"../Resources/surreal-engine-icon-128.png",
	"../Resources/surreal-engine-icon-256.png",
].map(path => new URL(path, self.location.href).href);

// Only the deliberately no-data artifact is eligible for lazy immutable
// caching. The developer build directory and every .data package remain
// network-only/blocked even when their file names otherwise look familiar.
const IMMUTABLE_RUNTIME_URLS = new Set([
	"../build-emscripten-nodata/SurrealEngine.js",
	"../build-emscripten-nodata/SurrealEngine.wasm",
	"../dist/SurrealEngine.js",
	"../dist/SurrealEngine.wasm",
].map(path => new URL(path, self.location.href).pathname));

const SHELL_PATHS = new Set(SHELL_URLS.map(value => new URL(value).pathname));
const INDEX_PATH = new URL("./index_webxr.html", self.location.href).pathname;
const OFFLINE_URL = new URL("./offline.html", self.location.href).href;
const COMMERCIAL_EXTENSIONS = /\.(?:data|u|unr|utx|uax|umx|uz|uz2)$/i;

function canonicalRequest(url) {
	return new Request(url.origin + url.pathname, { credentials: "same-origin" });
}

function isCommercialDataURL(url) {
	const path = url.pathname.toLowerCase();
	return path === "/gamedata" || path.startsWith("/gamedata/") ||
		path.includes("/surrealengine-ut99-data-v1/") ||
		path.includes("/ut99-data/") || COMMERCIAL_EXTENSIONS.test(path);
}

function classifyRequest(request) {
	const url = new URL(request.url);
	if (isCommercialDataURL(url)) return "blocked-commercial-data";
	if (url.origin !== self.location.origin) return "network-only";
	if (url.pathname === INDEX_PATH) return "network-first-html";
	if (SHELL_PATHS.has(url.pathname)) return "cache-first-shell";
	if (IMMUTABLE_RUNTIME_URLS.has(url.pathname)) return "cache-first-runtime";
	return "network-only";
}

function cacheable(response) {
	return response && response.ok && response.type !== "opaque";
}

async function notifyClients(type, detail) {
	const windows = await self.clients.matchAll({ type: "window", includeUncontrolled: true });
	for (const client of windows) client.postMessage(Object.assign({ type }, detail || {}));
}

async function cacheFirst(request, cacheName) {
	const url = new URL(request.url);
	const key = canonicalRequest(url);
	const cache = await caches.open(cacheName);
	const cached = await cache.match(key);
	if (cached) return cached;
	try {
		const response = await fetch(request);
		if (cacheable(response)) await cache.put(key, response.clone());
		return response;
	} catch (error) {
		await notifyClients("SURREAL_PWA_FETCH_ERROR", {
			policy: cacheName === RUNTIME_CACHE ? "cache-first-runtime" : "cache-first-shell",
			path: url.pathname,
		});
		return new Response("SurrealEngine resource unavailable offline: " + url.pathname, {
			status: 503,
			headers: { "Content-Type": "text/plain; charset=utf-8", "X-Surreal-PWA": "offline-miss" },
		});
	}
}

async function networkFirstHTML(request) {
	const cache = await caches.open(SHELL_CACHE);
	try {
		const response = await fetch(request);
		if (cacheable(response)) await cache.put(canonicalRequest(new URL(request.url)), response.clone());
		return response;
	} catch (_) {
		const cached = await cache.match(canonicalRequest(new URL(request.url)));
		if (cached) return cached;
		const offline = await cache.match(OFFLINE_URL);
		if (offline) return new Response(await offline.blob(), {
			status: 503,
			statusText: "Offline",
			headers: offline.headers,
		});
		return new Response("SurrealEngine WebXR is offline and the launcher shell is unavailable.", {
			status: 503,
			headers: { "Content-Type": "text/plain; charset=utf-8", "X-Surreal-PWA": "offline-no-shell" },
		});
	}
}

async function networkOnly(request) {
	try {
		return await fetch(request);
	} catch (_) {
		const url = new URL(request.url);
		await notifyClients("SURREAL_PWA_FETCH_ERROR", { policy: "network-only", path: url.pathname });
		if (request.mode === "navigate") {
			const cache = await caches.open(SHELL_CACHE);
			const offline = await cache.match(OFFLINE_URL);
			if (offline) return new Response(await offline.blob(), {
				status: 503,
				statusText: "Offline",
				headers: offline.headers,
			});
		}
		return new Response("This resource is intentionally not cached and the network is unavailable.", {
			status: 503,
			headers: { "Content-Type": "text/plain; charset=utf-8", "X-Surreal-PWA": "network-only-offline" },
		});
	}
}

function policySelfTest() {
	const request = path => new Request(new URL(path, self.location.href));
	return {
		gamedataBlocked: classifyRequest(request("../gamedata/System/Core.u")) === "blocked-commercial-data",
		dataBundleBlocked: classifyRequest(request("../build-emscripten/SurrealEngine.data?v=1")) === "blocked-commercial-data",
		packageBlocked: classifyRequest(request("../user-content/DM-Test.unr")) === "blocked-commercial-data",
		developerRuntimeNotCached: classifyRequest(request("../build-emscripten/SurrealEngine.js")) === "network-only",
		noDataJSImmutable: classifyRequest(request("../build-emscripten-nodata/SurrealEngine.js")) === "cache-first-runtime",
		noDataWasmImmutable: classifyRequest(request("../build-emscripten-nodata/SurrealEngine.wasm")) === "cache-first-runtime",
		indexNetworkFirst: classifyRequest(request("./index_webxr.html?update=1")) === "network-first-html",
		mutableOverlayInShell: classifyRequest(request("./mutable_persistence.js")) === "cache-first-shell",
		webXRSettingsInShell: classifyRequest(request("./webxr_settings.js")) === "cache-first-shell",
	};
}

self.addEventListener("install", event => {
	event.waitUntil((async () => {
		const cache = await caches.open(SHELL_CACHE);
		await cache.addAll(SHELL_URLS.map(url => new Request(url, { cache: "reload", credentials: "same-origin" })));
		await self.skipWaiting();
	})());
});

self.addEventListener("activate", event => {
	event.waitUntil((async () => {
		const shell = await caches.open(SHELL_CACHE);
		const required = await Promise.all(SHELL_URLS.map(url => shell.match(url)));
		if (required.some(response => !response)) throw new Error("Refusing activation: shell cache is incomplete");
		const names = await caches.keys();
		await Promise.all(names.map(name =>
			name.startsWith(CACHE_PREFIX) && name !== SHELL_CACHE && name !== RUNTIME_CACHE ? caches.delete(name) : false));
		await self.clients.claim();
		await notifyClients("SURREAL_PWA_ACTIVATED", { version: APP_VERSION });
	})());
});

self.addEventListener("fetch", event => {
	if (event.request.method !== "GET") return;
	const policy = classifyRequest(event.request);
	if (policy === "blocked-commercial-data") {
		event.respondWith(Promise.resolve(new Response(
			"Commercial/user-imported game data is never served through the SurrealEngine PWA cache.", {
				status: 451,
				headers: { "Content-Type": "text/plain; charset=utf-8", "X-Surreal-PWA": policy },
			})));
	} else if (policy === "network-first-html") {
		event.respondWith(networkFirstHTML(event.request));
	} else if (policy === "cache-first-shell") {
		event.respondWith(cacheFirst(event.request, SHELL_CACHE));
	} else if (policy === "cache-first-runtime") {
		event.respondWith(cacheFirst(event.request, RUNTIME_CACHE));
	} else {
		event.respondWith(networkOnly(event.request));
	}
});

self.addEventListener("message", event => {
	if (!event.data || event.data.type !== "SURREAL_PWA_STATUS") return;
	const response = {
		type: "SURREAL_PWA_STATUS",
		version: APP_VERSION,
		shellCache: SHELL_CACHE,
		runtimeCache: RUNTIME_CACHE,
		shellURLs: SHELL_URLS.slice(),
		immutableRuntimePaths: Array.from(IMMUTABLE_RUNTIME_URLS),
		policySelfTest: policySelfTest(),
	};
	if (event.ports && event.ports[0]) event.ports[0].postMessage(response);
});
