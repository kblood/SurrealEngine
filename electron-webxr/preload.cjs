"use strict";

const { contextBridge, ipcRenderer } = require("electron");

function asArrayBuffer(value) {
  if (value instanceof ArrayBuffer) return value;
  if (ArrayBuffer.isView(value)) {
    return value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength);
  }
  if (value && value.type === "Buffer" && Array.isArray(value.data)) {
    return Uint8Array.from(value.data).buffer;
  }
  throw new TypeError("The host returned an invalid game-file payload.");
}

contextBridge.exposeInMainWorld("SurrealHostBridge", Object.freeze({
  pickGameDirectory: () => ipcRenderer.invoke("surreal:pick-game-directory"),
  readGameFile: async token => asArrayBuffer(await ipcRenderer.invoke("surreal:read-game-file", token)),
}));
