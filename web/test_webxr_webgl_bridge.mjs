import fs from "node:fs";
import vm from "node:vm";

const source = fs.readFileSync(new URL("./webxr_webgl_bridge.js", import.meta.url), "utf8");
const sandbox = { globalThis: {}, console };
vm.runInNewContext(source, sandbox);
const bridge = sandbox.globalThis.SurrealWebXRWebGLBridge;
const asymmetric = [2,0,0,0, 0,3,0,0, .2,-.3,-1,-1, 0,0,-.2,0];
const converted = bridge.convertProjectionDepth(asymmetric);
if (converted[2] !== 0 || converted[6] !== 0 || converted[10] !== -1 || converted[11] !== -1 ||
	Math.abs(converted[14] + .1) > 1e-7 || converted[15] !== 0)
	throw new Error("projection depth conversion does not match the shared native transform");
if (converted[8] !== .2 || converted[9] !== -.3)
	throw new Error("asymmetric projection terms were changed");
console.log("WebXR WebGL bridge helper tests passed");
