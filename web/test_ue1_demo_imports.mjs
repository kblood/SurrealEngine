import "./ut99_importer.js";
import "./mutable_persistence.js";
await import("./browser_app.js");

const api = globalThis.SurrealGameImporter;
const mutable = globalThis.SurrealMutableData;
const launcher = globalThis.SurrealBrowserApp;

function assert(condition, message) {
	if (!condition) throw new Error(message);
}

function entry(path) {
	return { path, size: 1, getBlob: async () => new Blob(["x"]) };
}

function entries(paths) {
	return paths.map(entry);
}

const distributions = [
	{
		id: "ut99-demo-348",
		hash: "4bb5e71f78cf4806d9240df01f72236134af4a31",
		version: "348demo",
		paths: ["System/Core.u", "System/Engine.u", "System/BotPack.u", "System/UnrealTournament.exe", "System/UnrealTournament.ini",
			"Maps/DM-MorpheusDEMO.unr", "Textures/DemoFX.utx", "Sounds/Activates.uax", "Music/RUN.umx"],
		map: "DM-MorpheusDEMO",
	},
	{
		id: "unreal-demo-200",
		hash: "b851dcc69c4f773252c0498bd12756d90bcb59c2",
		version: "200",
		paths: ["System/Core.u", "System/Engine.u", "System/UnrealI.u", "System/UnrealIOrder.u", "System/Unreal.exe", "System/Default.ini",
			"Maps/Unreal.unr", "Textures/Ancient.utx", "Sounds/VRikers.uax", "Music/Vortex.umx"],
		map: "Unreal",
	},
	{
		id: "deus-ex-demo-1002f",
		hash: "4be582d4194400e87f64894c92b3f2119e012251",
		version: "1002f_DEMO",
		paths: ["System/Core.u", "System/Engine.u", "System/DeusEx.u", "System/DeusEx.exe", "System/DeusEx.ini",
			"Maps/00_Training.dx", "Textures/UNATCO.utx", "Sounds/Ambient.uax", "Music/Training_Music.umx"],
		map: "00_Training",
	},
];

for (const distribution of distributions) {
	const definition = api.GAME_DEFINITIONS[distribution.id];
	assert(definition, distribution.id + " definition missing");
	assert(definition.executableSHA1 === distribution.hash, distribution.id + " executable SHA1 differs from audit");
	assert(definition.version === distribution.version, distribution.id + " version differs from audit");
	assert(definition.demo && definition.experimental, distribution.id + " must remain explicitly experimental");

	const validation = api.validateEntries(entries(distribution.paths));
	assert(validation.gameId === distribution.id, distribution.id + " was not auto-detected");
	assert(api.validateEntries(entries(distribution.paths), distribution.id).game === definition,
		distribution.id + " requested selection did not resolve its descriptor");

	const metadata = {
		schema: api.SCHEMA_NAME,
		version: api.SCHEMA_VERSION,
		datasetId: distribution.id,
		gameId: distribution.id,
		fileCount: distribution.paths.length,
		totalBytes: distribution.paths.length,
		files: distribution.paths.map(path => ({ path, size: 1 })),
	};
	const manifest = api.mapManifestFromMetadata(metadata, "ready");
	assert(manifest.maps.includes(distribution.map), distribution.id + " map extension was not projected into the launcher");
	const directArgs = launcher.buildNativeArguments({ game: definition,
		map: distribution.map, renderer: "webgpu", skipIntro: true });
	assert(directArgs.includes("--url=" + distribution.map),
		distribution.id + " direct launch did not retain its selected map");
	const introArgs = launcher.buildNativeArguments({ game: definition,
		map: distribution.map, renderer: "webgpu", skipIntro: false });
	assert(!introArgs.some(argument => argument.startsWith("--url=")),
		distribution.id + " normal intro did not defer to its configured LocalMap");
}

// Existing retail/browser behavior remains distinct from the demo variants.
const retailUT = entries(["System/Core.u", "System/Engine.u", "System/Botpack.u", "System/UnrealTournament.exe", "System/UnrealTournament.ini",
	"Maps/DM-Deck16][.unr", "Textures/UTtech1.utx", "Sounds/Announcer.uax", "Music/utmenu23.umx"]);
assert(api.validateEntries(retailUT).gameId === "ut99", "retail UT99 was reclassified");

const retailUnreal = entries(["System/Core.u", "System/Engine.u", "System/UnrealShare.u", "System/UnrealI.u", "System/Unreal.exe", "System/Unreal.ini",
	"Maps/Vortex2.unr", "Textures/Ancient.utx", "Sounds/VRikers.uax", "Music/Vortex.umx"]);
assert(api.validateEntries(retailUnreal).gameId === "unreal-gold", "Unreal Gold was reclassified");

let wrongExtension = null;
try {
	const invalid = distributions[2].paths.map(path => path.endsWith(".dx") ? "Maps/Entry.unr" : path);
	api.validateEntries(entries(invalid), "deus-ex-demo-1002f");
} catch (error) {
	wrongExtension = error;
}
assert(wrongExtension && wrongExtension.code === "MISSING_GAME_DATA", "Deus Ex import accepted a non-.dx map set");

assert(mutable.classifyMutablePath("/gamedata/System/SE-Unreal.ini") === "engine-config", "Unreal demo config is not persisted");
assert(mutable.classifyMutablePath("/gamedata/System/SE-DeusEx.ini") === "engine-config", "Deus Ex demo config is not persisted");
assert(mutable.classifyMutablePath("/gamedata/Save/Save1.dxs") === "deus-ex-save", "Deus Ex demo saves are not persisted");
assert(mutable.classifyMutablePath("/gamedata/Save/Save1.usa") === "ut99-save", "existing UE1 save classification changed");

console.log("UE1 demo browser import descriptors passed");
