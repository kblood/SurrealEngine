/*
 * Emscripten 6.0.2 passes Uint8Array views backed by a resizable
 * WebAssembly.ArrayBuffer to its internal UTF8Decoder. Chrome rejects those
 * views. With TEXTDECODER=1, null selects Emscripten's portable scalar UTF-8
 * path. This deliberately leaves globalThis.TextDecoder and application-owned
 * decoding unchanged.
 */
mergeInto(LibraryManager.library, {
	$UTF8Decoder: "null",
});
