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
#include <set>
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

	void WriteActorIndexOrNull(std::ostringstream& out,
		const std::map<const UActor*, size_t>& actorIndexes, const UActor* actor,
		const char* relationship)
	{
		if (!actor)
		{
			out << "null";
			return;
		}
		auto it = actorIndexes.find(actor);
		if (it == actorIndexes.end())
			throw std::runtime_error(std::string("catalog ") + relationship
				+ " actor is absent from the level actor list");
		out << it->second;
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

	bool StartsWith(const std::string& value, const std::string& prefix)
	{
		return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
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
				const std::string gameConfig = commandline->GetArg("", "--catalog-game-config");
				if (map.empty() || output.empty())
					throw std::runtime_error("map catalog requires --catalog-map and --catalog-output");
				if (!IsSafeOutputSegment(map))
					throw std::runtime_error("--catalog-map must be a simple map identifier");
				if (!exportScripts.empty() && exportScripts != "0" && exportScripts != "1")
					throw std::runtime_error("--catalog-export-scripts must be 0 or 1");
				if (!gameConfig.empty() && gameConfig != "0" && gameConfig != "1")
					throw std::runtime_error("--catalog-game-config must be 0 or 1");
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
				if (gameConfig == "1")
					ExportBotConfig(outputPath);
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
		void ExportBotConfig(const std::filesystem::path& outputPath) const
		{
			struct ConfigSection
			{
				const char* Name;
			};
			constexpr ConfigSection sections[] = {
				{ "Botpack.ChallengeBotInfo" },
				{ "UnrealShare.BotInfo" },
			};
			std::set<std::string> rosterClasses;
			std::ostringstream out;
			out.imbue(std::locale::classic());
			out << std::fixed << std::setprecision(6)
				<< "{\n  \"schema\":\"surreal-bot-config-catalog-spike-v2\",\n"
				<< "  \"ini_source\":\"loaded_user_ini\",\n  \"roster_sections\":[";
			bool firstSection = true;
			for (const ConfigSection& section : sections)
			{
				Array<NameString> keys = EngineRef.packages->GetIniKeysFromSection("user", section.Name);
				if (keys.empty())
					continue;
				std::sort(keys.begin(), keys.end(), [](const NameString& left, const NameString& right)
				{
					return left.ToString() < right.ToString();
				});
				if (!firstSection)
					out << ',';
				firstSection = false;
				out << "\n    {\"section\":" << JsonString(section.Name) << ",\"entries\":[";
				bool firstEntry = true;
				for (const NameString& key : keys)
				{
					const std::string keyName = key.ToString();
					const std::string value = EngineRef.packages->GetIniValue("user", section.Name, key);
					if (!firstEntry)
						out << ',';
					firstEntry = false;
					out << "{\"key\":" << JsonString(keyName) << ",\"value\":" << JsonString(value) << '}';
					if (StartsWith(keyName, "BotClasses[") && !value.empty())
						rosterClasses.insert(value);
				}
				out << "]}";
			}
			if (rosterClasses.empty())
			{
				if (EngineRef.packages->HasPackage("Botpack"))
					rosterClasses.insert("Botpack.Bot");
				else if (EngineRef.packages->HasPackage("UnrealShare"))
					rosterClasses.insert("UnrealShare.Bots");
			}
			out << "\n  ],\n  \"bot_classes\":[";
			bool firstClass = true;
			for (const std::string& className : rosterClasses)
			{
				UClass* cls = EngineRef.packages->FindClass(className);
				if (!cls)
					throw std::runtime_error("catalog roster bot class could not be resolved: " + className);
				const char* requiredProperties[] = {
					"GroundSpeed", "JumpZ", "MaxStepHeight", "AccelRate",
					"bCanWalk", "bCanJump", "bCanSwim", "bCanFly", "bCanOpenDoors", "bCanDoSpecial",
				};
				for (const char* property : requiredProperties)
				{
					if (!cls->GetProperty(property))
						throw std::runtime_error("catalog roster bot class is missing required pawn property: "
							+ className + "." + property);
				}
				if (!firstClass)
					out << ',';
				firstClass = false;
				out << "\n    {\"roster_class\":" << JsonString(className)
					<< ",\"resolved_class\":" << JsonString(UObject::GetUClassFullName(cls).ToString())
					<< ",\"movement\":{\"ground_speed\":" << cls->GetFloat("GroundSpeed")
					<< ",\"jump_z\":" << cls->GetFloat("JumpZ")
					<< ",\"max_step_height\":" << cls->GetFloat("MaxStepHeight")
					<< ",\"accel_rate\":" << cls->GetFloat("AccelRate")
					<< "},\"class_default_capabilities\":{\"walk\":" << (cls->GetBool("bCanWalk") ? "true" : "false")
					<< ",\"jump\":" << (cls->GetBool("bCanJump") ? "true" : "false")
					<< ",\"swim\":" << (cls->GetBool("bCanSwim") ? "true" : "false")
					<< ",\"fly\":" << (cls->GetBool("bCanFly") ? "true" : "false")
					<< ",\"open_doors\":" << (cls->GetBool("bCanOpenDoors") ? "true" : "false")
					<< ",\"special\":" << (cls->GetBool("bCanDoSpecial") ? "true" : "false")
					<< "},\"class_default_capabilities_realized_at_spawn\":false}";
			}
			out << "\n  ]\n}\n";
			File::write_all_text((outputPath / "bot-config.json").string(), out.str());
		}

		void WriteTraversalActors(std::ostringstream& out,
			const std::map<const UActor*, size_t>& actorIndexes) const
		{
			out << "\"traversal_actors\":[";
			bool first = true;
			for (size_t index = 0; index < EngineRef.Level->Actors.size(); index++)
			{
				UActor* actor = EngineRef.Level->Actors[index];
				if (!actor)
					continue;
				const char* kind = nullptr;
				if (UObject::TryCast<ULiftCenter>(actor))
					kind = "lift_center";
				else if (UObject::TryCast<ULiftExit>(actor))
					kind = "lift_exit";
				else if (UObject::TryCast<UMover>(actor))
					kind = "mover";
				else if (UObject::TryCast<UTeleporter>(actor))
					kind = "teleporter";
				else if (UObject::TryCast<UWarpZoneMarker>(actor))
					kind = "warp_zone_marker";
				else if (UObject::TryCast<UWarpZoneInfo>(actor))
					kind = "warp_zone_info";
				else if (UObject::TryCast<UInventorySpot>(actor))
					kind = "inventory_spot";
				else if (UObject::TryCast<UPlayerStart>(actor))
					kind = "player_start";
				if (!kind)
					continue;
				if (!first)
					out << ',';
				first = false;
				out << "\n    {\"actor_index\":" << index << ",\"name\":"
					<< JsonString(actor->Name.ToString()) << ",\"class\":"
					<< JsonString(UObject::GetUClassFullName(actor).ToString())
					<< ",\"kind\":" << JsonString(kind);
				if (ULiftCenter* liftCenter = UObject::TryCast<ULiftCenter>(actor))
				{
					out << ",\"mover_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, liftCenter->MyLift(), "lift-center mover");
					out << ",\"recommended_trigger_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, liftCenter->RecommendedTrigger(), "lift-center recommended trigger");
					out << ",\"lift_tag\":" << JsonString(liftCenter->LiftTag().ToString())
						<< ",\"lift_trigger\":" << JsonString(liftCenter->LiftTrigger().ToString());
				}
				else if (ULiftExit* liftExit = UObject::TryCast<ULiftExit>(actor))
				{
					out << ",\"mover_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, liftExit->MyLift(), "lift-exit mover");
					out << ",\"recommended_trigger_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, liftExit->RecommendedTrigger(), "lift-exit recommended trigger");
					out << ",\"lift_tag\":" << JsonString(liftExit->LiftTag().ToString())
						<< ",\"lift_trigger\":" << JsonString(liftExit->LiftTrigger().ToString());
				}
				else if (UMover* mover = UObject::TryCast<UMover>(actor))
				{
					out << ",\"marker_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, mover->myMarker(), "mover marker");
					out << ",\"recommended_trigger_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, mover->RecommendedTrigger(), "mover recommended trigger");
					out << ",\"trigger_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, mover->TriggerActor(), "mover trigger");
					out << ",\"trigger_actor2_index\":";
					WriteActorIndexOrNull(out, actorIndexes, mover->TriggerActor2(), "mover second trigger");
					out << ",\"leader_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, mover->Leader(), "mover leader");
					out << ",\"follower_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, mover->Follower(), "mover follower");
					out << ",\"move_time\":" << mover->MoveTime()
						<< ",\"stay_open_time\":" << mover->StayOpenTime()
						<< ",\"num_keys\":" << static_cast<int>(mover->NumKeys());
				}
				else if (UTeleporter* teleporter = UObject::TryCast<UTeleporter>(actor))
				{
					out << ",\"trigger_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, teleporter->TriggerActor(), "teleporter trigger");
					out << ",\"trigger_actor2_index\":";
					WriteActorIndexOrNull(out, actorIndexes, teleporter->TriggerActor2(), "teleporter second trigger");
					out << ",\"url\":" << JsonString(teleporter->URL())
						<< ",\"enabled\":" << (teleporter->bEnabled() ? "true" : "false")
						<< ",\"changes_velocity\":" << (teleporter->bChangesVelocity() ? "true" : "false")
						<< ",\"changes_yaw\":" << (teleporter->bChangesYaw() ? "true" : "false");
				}
				else if (UWarpZoneMarker* marker = UObject::TryCast<UWarpZoneMarker>(actor))
				{
					out << ",\"marked_warp_zone_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, marker->markedWarpZone(), "warp-zone marker");
					out << ",\"trigger_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, marker->TriggerActor(), "warp-zone marker trigger");
					out << ",\"trigger_actor2_index\":";
					WriteActorIndexOrNull(out, actorIndexes, marker->TriggerActor2(), "warp-zone marker second trigger");
				}
				else if (UWarpZoneInfo* zone = UObject::TryCast<UWarpZoneInfo>(actor))
				{
					out << ",\"other_side_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, zone->OtherSideActor(), "warp-zone other side");
					out << ",\"this_tag\":" << JsonString(zone->ThisTag().ToString())
						<< ",\"other_side_url\":" << JsonString(zone->OtherSideURL());
				}
				else if (UInventorySpot* spot = UObject::TryCast<UInventorySpot>(actor))
				{
					out << ",\"marked_item_actor_index\":";
					WriteActorIndexOrNull(out, actorIndexes, spot->markedItem(), "inventory-spot marked item");
				}
				else if (UPlayerStart* start = UObject::TryCast<UPlayerStart>(actor))
				{
					out << ",\"team_number\":" << static_cast<int>(start->TeamNumber())
						<< ",\"enabled\":" << (start->bEnabled() ? "true" : "false")
						<< ",\"coop_start\":" << (start->bCoopStart() ? "true" : "false")
						<< ",\"single_player_start\":" << (start->bSinglePlayerStart() ? "true" : "false");
				}
				out << '}';
			}
			out << "\n  ]";
		}

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
			out << "{\n  \"schema\":\"surreal-map-catalog-spike-v3\",\n"
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
				<< ",\"zones_exact\":" << zoneCount << "},\n  \"actors\":[";
			bool firstActor = true;
			for (size_t index = 0; index < EngineRef.Level->Actors.size(); index++)
			{
				UActor* actor = EngineRef.Level->Actors[index];
				if (!firstActor)
					out << ',';
				firstActor = false;
				out << "\n    {\"actor_index\":" << index << ",\"present\":"
					<< (actor ? "true" : "false");
				if (actor)
				{
					out << ",\"name\":" << JsonString(actor->Name.ToString())
						<< ",\"class\":" << JsonString(UObject::GetUClassFullName(actor).ToString());
				}
				out << '}';
			}
			out << "\n  ],\n  \"navigation_points\":[";
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
					<< ",\"resolved_zone_actor_index\":";
				WriteActorIndexOrNull(out, actorIndexes, point->Region().Zone,
					"navigation-point resolved zone");
				out
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
			out << "\n  ],\n  ";
			WriteTraversalActors(out, actorIndexes);
			out << ",\n  \"zones\":[";
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
			if (!EngineRef.Level->Model)
				throw std::runtime_error("catalog level model is unavailable");
			out << "\n  ],\n  \"zone_graph\":[";
			for (size_t index = 0; index < EngineRef.Level->Model->Zones.size(); index++)
			{
				if (index)
					out << ',';
				const ZoneProperties& zone = EngineRef.Level->Model->Zones[index];
				out << "\n    {\"zone_index\":" << index << ",\"zone_actor_index\":";
				WriteActorIndexOrNull(out, actorIndexes, zone.ZoneActor, "model zone actor");
				out << ",\"connectivity\":" << JsonString(std::to_string(zone.Connectivity))
					<< ",\"visibility\":" << JsonString(std::to_string(zone.Visibility)) << '}';
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
