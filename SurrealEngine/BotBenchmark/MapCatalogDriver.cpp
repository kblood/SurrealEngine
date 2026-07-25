#include "Precomp.h"
#include "MapCatalogDriver.h"

#include "Editor/Export.h"
#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Runtime/HeadlessDriver.h"
#include "UObject/UActor.h"
#include "UObject/UClass.h"
#include "UObject/ULevel.h"
#include "Utils/CommandLine.h"
#include "Utils/File.h"
#include "Utils/Logger.h"
#include "Utils/SHA1Sum.h"

#include <filesystem>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
	std::string JsonString(const std::string& value)
	{
		std::ostringstream out;
		out << '"';
		for (unsigned char character : value)
		{
			switch (character)
			{
			case '\\': out << "\\\\"; break;
			case '"': out << "\\\""; break;
			case '\n': out << "\\n"; break;
			case '\r': out << "\\r"; break;
			case '\t': out << "\\t"; break;
			default:
				if (character < 0x20)
				{
					out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
						<< static_cast<int>(character) << std::dec << std::setfill(' ');
				}
				else
				{
					out << static_cast<char>(character);
				}
				break;
			}
		}
		out << '"';
		return out.str();
	}

	void WriteVector(std::ostringstream& out, const vec3& value)
	{
		out << "{\"x\":" << value.x << ",\"y\":" << value.y
			<< ",\"z\":" << value.z << '}';
	}

	void WriteReachSpecIndexes(std::ostringstream& out, const Array<LevelReachSpec>& specs,
		const UNavigationPoint* point, FixedArrayView<int, 16> indexes,
		bool pointIsStart, const char* relation)
	{
		out << '[';
		bool first = true;
		for (int index : indexes)
		{
			if (index == -1)
				break;
			if (index < 0 || static_cast<size_t>(index) >= specs.size())
				throw std::runtime_error(std::string("catalog ") + relation
					+ " reachspec index is outside the level reachspec array");
			const LevelReachSpec& spec = specs[index];
			if ((pointIsStart ? spec.startActor : spec.endActor) != point)
				throw std::runtime_error(std::string("catalog ") + relation
					+ " reachspec does not refer to its owning navigation point");
			if (!first)
				out << ',';
			first = false;
			out << index;
		}
		out << ']';
	}

	void WriteReachFlags(std::ostringstream& out, int32_t flags)
	{
		struct ReachFlagName
		{
			int32_t Flag;
			const char* Name;
		};
		constexpr ReachFlagName names[] = {
			{ R_WALK, "walk" }, { R_FLY, "fly" }, { R_SWIM, "swim" },
			{ R_JUMP, "jump" }, { R_DOOR, "door" }, { R_SPECIAL, "special" },
			{ R_PLAYERONLY, "player_only" },
		};
		const uint32_t rawFlags = static_cast<uint32_t>(flags);
		uint32_t knownFlags = 0;
		out << "\"reach_flags\":" << flags << ",\"reach_flag_names\":[";
		bool first = true;
		for (const ReachFlagName& entry : names)
		{
			knownFlags |= static_cast<uint32_t>(entry.Flag);
			if ((rawFlags & static_cast<uint32_t>(entry.Flag)) == 0)
				continue;
			if (!first)
				out << ',';
			first = false;
			out << JsonString(entry.Name);
		}
		out << "]" << ",\"unknown_reach_flags\":" << (rawFlags & ~knownFlags);
	}

	void WritePackageIdentity(std::ostringstream& out, const Package* package)
	{
		if (!package)
			throw std::runtime_error("catalog package identity is unavailable");
		out << "{\"name\":" << JsonString(package->GetPackageName().ToString())
			<< ",\"file_name\":" << JsonString(package->GetPackageFileName())
			<< ",\"package_version\":" << package->GetVersion()
			<< ",\"licensee_mode\":" << package->GetLicenseeMode()
			<< ",\"sha1\":" << JsonString(SHA1Sum::of_file(package->GetPackageFilePath())) << '}';
	}

	bool IsWithin(const std::filesystem::path& child, const std::filesystem::path& parent)
	{
		auto childIt = child.begin();
		for (auto parentIt = parent.begin(); parentIt != parent.end(); ++parentIt, ++childIt)
		{
			if (childIt == child.end() || *childIt != *parentIt)
				return false;
		}
		return true;
	}

	bool IsSafeOutputSegment(const std::string& name)
	{
		return !name.empty() && name != "." && name != ".." &&
			name.find_first_of("\\/:*?\"<>|") == std::string::npos;
	}

	std::vector<std::string> ParsePackageList(const std::string& value)
	{
		std::vector<std::string> packages;
		std::istringstream input(value);
		std::string package;
		while (std::getline(input, package, ','))
		{
			if (!IsSafeOutputSegment(package))
				throw std::runtime_error("catalog script package names must be simple package identifiers");
			packages.push_back(package);
		}
		if (packages.empty())
			throw std::runtime_error("catalog script package list is empty");
		return packages;
	}

	class MapCatalogDriver final : public HeadlessDriver
	{
	public:
		explicit MapCatalogDriver(Engine& engine) : EngineRef(engine) {}

		HeadlessDriverConfig GetConfig() const override { return { .MaxTicks = 1 }; }

		void Start() override
		{
			try
			{
				if (!commandline)
					throw std::runtime_error("map catalog requires command-line arguments");
				const std::string map = commandline->GetArg("", "--catalog-map");
				const std::string output = commandline->GetArg("", "--catalog-output");
				const std::string exportScripts = commandline->GetArg("", "--catalog-export-scripts");
				const std::string requestedPackages = commandline->GetArg("", "--catalog-script-packages");
				if (map.empty() || output.empty())
					throw std::runtime_error("map catalog requires --catalog-map and --catalog-output");
				if (!IsSafeOutputSegment(map))
					throw std::runtime_error("--catalog-map must be a simple map identifier");
				if (!exportScripts.empty() && exportScripts != "0" && exportScripts != "1")
					throw std::runtime_error("--catalog-export-scripts must be 0 or 1");
				if (!requestedPackages.empty() && exportScripts != "1")
					throw std::runtime_error("--catalog-script-packages requires --catalog-export-scripts=1");
				const std::filesystem::path gameRoot = std::filesystem::absolute(
					EngineRef.LaunchInfo.gameRootFolder).lexically_normal();
				const std::filesystem::path outputPath = std::filesystem::absolute(output).lexically_normal();
				if (IsWithin(outputPath, gameRoot))
					throw std::runtime_error("map catalog output must not be inside the game root");

				EngineRef.LaunchInfo.noEntryMap = true;
				UnrealURL url(EngineRef.GetDefaultURL(
					EngineRef.packages->GetIniValue("system", "URL", "LocalMap")), map);
				EngineRef.LoadMap(url);
				if (!EngineRef.Level || !EngineRef.LevelInfo)
					throw std::runtime_error("map catalog did not load a level");
				std::filesystem::create_directories(outputPath);
				const std::filesystem::path file = outputPath / (map + ".json");
				File::write_all_text(file.string(), Serialize(map));
				if (exportScripts == "1")
					ExportScripts(outputPath, requestedPackages);
				Output = file.string();
			}
			catch (const std::exception& error)
			{
				Failure = error.what();
				LogMessage("Map catalog failed: " + Failure);
			}
			Complete = true;
		}

		bool IsComplete() const override { return Complete; }
		void Tick(const DeterministicFrameTime&) override {}
		int Finish(const HeadlessRunSummary&) override { return Failure.empty() ? 0 : 1; }

	private:
		void ExportScripts(const std::filesystem::path& outputPath, const std::string& requestedPackages) const
		{
			std::vector<std::string> packageNames = requestedPackages.empty()
				? std::vector<std::string>{ "Botpack", "Engine", "UnrealI", "UnrealShare" }
				: ParsePackageList(requestedPackages);
			std::ostringstream manifest;
			manifest << "{\n  \"schema\":\"surreal-script-export-spike-v1\",\n  \"packages\":[";
			bool firstPackage = true;
			for (const std::string& packageName : packageNames)
			{
				if (!EngineRef.packages->HasPackage(NameString(packageName)))
				{
					if (requestedPackages.empty())
						continue;
					throw std::runtime_error("catalog script package was not found: " + packageName);
				}
				Package* package = EngineRef.packages->GetPackage(NameString(packageName));
				Array<UClass*> classes = package->GetAllObjects<UClass>();
				std::sort(classes.begin(), classes.end(), [](const UClass* left, const UClass* right)
				{
					return left->FriendlyName.ToString() < right->FriendlyName.ToString();
				});
				const std::filesystem::path classesPath = outputPath / "scripts" / packageName / "Classes";
				size_t exportedClasses = 0;
				for (UClass* cls : classes)
				{
					const std::string className = cls->FriendlyName.ToString();
					if (!IsSafeOutputSegment(className))
						throw std::runtime_error("catalog encountered a class name unsafe for external output");
					MemoryStreamWriter script = Exporter::ExportClass(cls);
					if (script.Size() == 0)
						continue;
					std::filesystem::create_directories(classesPath);
					File::write_all_bytes((classesPath / (className + ".uc")).string(), script.Data(), script.Size());
					exportedClasses++;
				}
				if (!firstPackage)
					manifest << ',';
				firstPackage = false;
				manifest << "\n    {\"name\":" << JsonString(packageName)
					<< ",\"identity\":";
				WritePackageIdentity(manifest, package);
				manifest
					<< ",\"class_count\":" << classes.size()
					<< ",\"script_class_count\":" << exportedClasses << '}';
			}
			manifest << "\n  ]\n}\n";
			File::write_all_text((outputPath / "script-export.json").string(), manifest.str());
		}

		std::string Serialize(const std::string& map) const
		{
			std::map<const UActor*, size_t> actorIndexes;
			for (size_t index = 0; index < EngineRef.Level->Actors.size(); index++)
			{
				if (UActor* actor = EngineRef.Level->Actors[index])
					actorIndexes.emplace(actor, index);
			}
			std::ostringstream out;
			out.imbue(std::locale::classic());
			out << std::fixed << std::setprecision(6);
			out << "{\n  \"schema\":\"surreal-map-catalog-spike-v1\",\n"
				<< "  \"game\":{\"name\":" << JsonString(EngineRef.LaunchInfo.gameName)
				<< ",\"version\":" << JsonString(EngineRef.LaunchInfo.gameVersionString) << "},\n"
				<< "  \"map\":" << JsonString(map) << ",\n"
				<< "  \"map_package\":";
			WritePackageIdentity(out, EngineRef.Level->package);
			out << ",\n"
				<< "  \"counts\":{\"actors_exact\":" << EngineRef.Level->Actors.size();
			size_t navigationCount = 0;
			size_t zoneCount = 0;
			for (UActor* actor : EngineRef.Level->Actors)
			{
				navigationCount += UObject::TryCast<UNavigationPoint>(actor) ? 1 : 0;
				zoneCount += UObject::TryCast<UZoneInfo>(actor) ? 1 : 0;
			}
			out << ",\"navigation_points_exact\":" << navigationCount
				<< ",\"reachspecs_exact\":" << EngineRef.Level->ReachSpecs.size()
				<< ",\"zones_exact\":" << zoneCount << "},\n  \"navigation_points\":[";
			bool first = true;
			for (size_t index = 0; index < EngineRef.Level->Actors.size(); index++)
			{
				UNavigationPoint* point = UObject::TryCast<UNavigationPoint>(EngineRef.Level->Actors[index]);
				if (!point)
					continue;
				if (!first)
					out << ',';
				first = false;
				out << "\n    {\"actor_index\":" << index << ",\"name\":"
					<< JsonString(point->Name.ToString()) << ",\"class\":"
					<< JsonString(UObject::GetUClassFullName(point).ToString()) << ",\"position\":";
				WriteVector(out, point->Location());
				out << ",\"collision_radius\":" << point->CollisionRadius()
					<< ",\"collision_height\":" << point->CollisionHeight()
					<< ",\"extra_cost\":" << point->ExtraCost()
					<< ",\"end_point\":" << (point->HasProperty("bEndPoint") && point->GetBool("bEndPoint") ? "true" : "false")
					<< ",\"end_point_only\":" << (point->HasProperty("bEndPointOnly") && point->GetBool("bEndPointOnly") ? "true" : "false")
					<< ",\"never_use_strafing\":" << (point->HasProperty("bNeverUseStrafing") && point->GetBool("bNeverUseStrafing") ? "true" : "false")
					<< ",\"one_way\":" << (point->HasProperty("bOneWayPath") && point->GetBool("bOneWayPath") ? "true" : "false")
					<< ",\"player_only\":" << (point->HasProperty("bPlayerOnly") && point->GetBool("bPlayerOnly") ? "true" : "false")
					<< ",\"special_cost\":" << (point->HasProperty("bSpecialCost") && point->GetBool("bSpecialCost") ? "true" : "false")
					<< ",\"paths\":";
				WriteReachSpecIndexes(out, EngineRef.Level->ReachSpecs, point,
					point->Paths(), true, "forward");
				out << ",\"upstream_paths\":";
				WriteReachSpecIndexes(out, EngineRef.Level->ReachSpecs, point,
					point->upstreamPaths(), false, "upstream");
				out << ",\"pruned_paths\":";
				WriteReachSpecIndexes(out, EngineRef.Level->ReachSpecs, point,
					point->PrunedPaths(), true, "pruned");
				out << ",\"visible_no_reach_actor_indexes\":[";
				bool firstVisibleNoReach = true;
				for (UNavigationPoint* visible : point->VisNoReachPaths())
				{
					if (!visible)
						break;
					auto visibleIt = actorIndexes.find(visible);
					if (visibleIt == actorIndexes.end())
						throw std::runtime_error("catalog visible-no-reach navigation point is absent from the level actor list");
					if (!firstVisibleNoReach)
						out << ',';
					firstVisibleNoReach = false;
					out << visibleIt->second;
				}
				out << "]}";
			}
			out << "\n  ],\n  \"reachspecs\":[";
			for (size_t index = 0; index < EngineRef.Level->ReachSpecs.size(); index++)
			{
				if (index)
					out << ',';
				const LevelReachSpec& spec = EngineRef.Level->ReachSpecs[index];
				auto findActor = [&](const UActor* actor)
				{
					auto it = actorIndexes.find(actor);
					return it == actorIndexes.end() ? -1 : static_cast<int64_t>(it->second);
				};
				out << "\n    {\"index\":" << index << ",\"start_actor_index\":"
					<< findActor(spec.startActor) << ",\"end_actor_index\":"
					<< findActor(spec.endActor) << ",\"distance\":" << spec.distance
					<< ",\"collision_radius\":" << spec.collisionRadius
					<< ",\"collision_height\":" << spec.collisionHeight << ',';
				WriteReachFlags(out, spec.reachFlags);
				out << ",\"pruned\":" << (spec.bPruned ? "true" : "false") << '}';
			}
			out << "\n  ],\n  \"zones\":[";
			first = true;
			for (size_t index = 0; index < EngineRef.Level->Actors.size(); index++)
			{
				UZoneInfo* zone = UObject::TryCast<UZoneInfo>(EngineRef.Level->Actors[index]);
				if (!zone)
					continue;
				if (!first)
					out << ',';
				first = false;
				out << "\n    {\"actor_index\":" << index << ",\"name\":"
					<< JsonString(zone->Name.ToString()) << ",\"class\":"
					<< JsonString(UObject::GetUClassFullName(zone).ToString())
					<< ",\"pain\":" << (zone->bPainZone() ? "true" : "false")
					<< ",\"water\":" << (zone->bWaterZone() ? "true" : "false")
					<< ",\"kill\":" << (zone->bKillZone() ? "true" : "false")
					<< ",\"damage_per_second\":" << zone->DamagePerSec()
					<< ",\"gravity\":";
				WriteVector(out, zone->ZoneGravity());
				out << ",\"velocity\":";
				WriteVector(out, zone->ZoneVelocity());
				out << '}';
			}
			out << "\n  ]\n}\n";
			return out.str();
		}

		Engine& EngineRef;
		bool Complete = false;
		std::string Failure;
		std::string Output;
	};
}

void RegisterMapCatalogDriver(HeadlessDriverRegistry& registry)
{
	if (!registry.Contains("map-catalog"))
		registry.Register("map-catalog", [](Engine& engine)
		{
			return std::make_unique<MapCatalogDriver>(engine);
		});
}
