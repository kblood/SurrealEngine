#include "Precomp.h"
#include "TraceCorpusDriver.h"

#include "Engine.h"
#include "Package/PackageManager.h"
#include "Runtime/HeadlessDriver.h"
#include "UObject/UActor.h"
#include "UObject/UClass.h"
#include "UObject/ULevel.h"
#include "Utils/CommandLine.h"
#include "Utils/File.h"

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <vector>

// Replays the trace corpus that the retail UT99 BotTelemetry mutator emits, so the
// two engines can be diffed on identical queries. The mutator anchors every probe
// to a NavigationPoint and fires four axis-aligned traces, once with a collision
// extent and once without, with bTraceActors false so only BSP and movers take part.
namespace
{
	// vector(rot(0, d * 16384, 0)) in UnrealScript, which is what the mutator uses.
	const vec3 ProbeDirections[4] = {
		vec3(1.0f, 0.0f, 0.0f),
		vec3(0.0f, 1.0f, 0.0f),
		vec3(-1.0f, 0.0f, 0.0f),
		vec3(0.0f, -1.0f, 0.0f),
	};

	std::string ActorNameOrDash(UObject* object)
	{
		if (!object)
			return "-";
		return object->Name.ToString();
	}

	std::string ClassNameOrDash(UObject* object)
	{
		if (!object || !object->Class)
			return "-";
		return object->Class->Name.ToString();
	}

	class TraceCorpusDriver final : public HeadlessDriver
	{
	public:
		explicit TraceCorpusDriver(Engine& engine) : EngineRef(engine) {}

		HeadlessDriverConfig GetConfig() const override { return { .MaxTicks = 1 }; }

		void Start() override
		{
			try
			{
				if (!commandline)
					throw std::runtime_error("trace corpus requires command-line arguments");
				const std::string map = commandline->GetArg("", "--trace-corpus-map");
				const std::string output = commandline->GetArg("", "--trace-corpus-output");
				if (map.empty() || output.empty())
					throw std::runtime_error(
						"trace corpus requires --trace-corpus-map and --trace-corpus-output");

				const float radius = ArgAsFloat("--trace-corpus-radius", 17.0f);
				const float height = ArgAsFloat("--trace-corpus-height", 39.0f);
				const float distance = ArgAsFloat("--trace-corpus-distance", 60.0f);
				const float reachDistance = ArgAsFloat("--trace-corpus-reach-distance", 1000.0f);
				const bool reachMode = !commandline->GetArg("", "--trace-corpus-reach").empty();

				const float visionDistance = ArgAsFloat("--trace-corpus-vision-distance", 800.0f);
				const int visionYawSteps = ArgAsInt("--trace-corpus-vision-yaw-steps", 32);
				const int visionStride = ArgAsInt("--trace-corpus-vision-stride", 16);
				const bool visionMode = !commandline->GetArg("", "--trace-corpus-vision").empty();

				EngineRef.LaunchInfo.noEntryMap = true;
				UnrealURL url(EngineRef.GetDefaultURL(
					EngineRef.packages->GetIniValue("system", "URL", "LocalMap")), map);
				EngineRef.LoadMap(url);

				File::write_all_text(output, visionMode
					? BuildVisionCorpus(radius, height, visionDistance, visionYawSteps, visionStride)
					: reachMode
						? BuildReachCorpus(radius, height, reachDistance)
						: BuildCorpus(radius, height, distance));
				Complete = true;
			}
			catch (const std::exception& error)
			{
				Failure = error.what();
				Complete = true;
			}
		}

		bool IsComplete() const override { return Complete; }
		void Tick(const DeterministicFrameTime&) override {}
		int Finish(const HeadlessRunSummary&) override { return Failure.empty() ? 0 : 1; }

	private:
		float ArgAsFloat(const char* name, float fallback) const
		{
			const std::string value = commandline->GetArg("", name);
			if (value.empty())
				return fallback;
			return std::stof(value);
		}

		int ArgAsInt(const char* name, int fallback) const
		{
			const std::string value = commandline->GetArg("", name);
			if (value.empty())
				return fallback;
			return std::stoi(value);
		}

		// The mutator traces from an Info actor with no bearing on the result; any
		// actor works because the endpoints are explicit. LevelInfo keeps the world
		// hit attribution identical to retail's LevelInfo0.
		UActor* FindTracingActor() const
		{
			for (UActor* actor : EngineRef.Level->Actors)
			{
				if (actor && actor->Level())
					return actor;
			}
			return nullptr;
		}

		std::string BuildCorpus(float radius, float height, float distance) const
		{
			UActor* tracingActor = FindTracingActor();
			if (!tracingActor)
				throw std::runtime_error("no actor available to trace from");

			const vec3 extent(radius, radius, height);
			std::ostringstream out;
			out << "# surrealengine trace corpus replay\n";
			out << "#\tcols_T\tidx node dir kind sx sy sz ex ey ez hit hitclass hx hy hz"
				<< " nx1000 ny1000 nz1000\n";

			int idx = 0;
			for (UActor* actor : EngineRef.Level->Actors)
			{
				UNavigationPoint* point = UObject::TryCast<UNavigationPoint>(actor);
				if (!point)
					continue;

				const vec3 start = point->Location();
				const std::string node = point->Name.ToString();
				for (int direction = 0; direction < 4; direction++)
				{
					const vec3 end = start + ProbeDirections[direction] * distance;
					EmitProbe(out, idx++, node, direction, "box",
						start, end, tracingActor, extent, false);
					EmitProbe(out, idx++, node, direction, "zero",
						start, end, tracingActor, vec3(0.0f, 0.0f, 0.0f), false);
					// CheckWaterJump traces with bTraceActors true, so cover that too.
					EmitProbe(out, idx++, node, direction, "boxact",
						start, end, tracingActor, extent, true);
					EmitProbe(out, idx++, node, direction, "zeroact",
						start, end, tracingActor, vec3(0.0f, 0.0f, 0.0f), true);
				}
			}
			out << "#\tcorpus_done\t" << idx << "\n";
			return out.str();
		}

		// The mirror of the retail mutator's DumpReachCorpus: ask ActorReachable and
		// PointReachable for every ordered pair of navigation points inside
		// reachDistance, from a bot pawn standing on the source point.
		std::string BuildReachCorpus(float radius, float height, float reachDistance) const
		{
			UActor* tracingActor = FindTracingActor();
			if (!tracingActor)
				throw std::runtime_error("no actor available to spawn from");

			std::vector<UNavigationPoint*> nodes;
			for (UActor* actor : EngineRef.Level->Actors)
			{
				if (UNavigationPoint* point = UObject::TryCast<UNavigationPoint>(actor))
					nodes.push_back(point);
			}
			if (nodes.empty())
				throw std::runtime_error("map has no navigation points");

			UClass* probeClass = EngineRef.packages->FindClass("Botpack.TMale1");
			if (!probeClass)
				throw std::runtime_error("Botpack.TMale1 is not available");

			UPawn* probe = UObject::TryCast<UPawn>(tracingActor->Spawn(probeClass,
				nullptr, {}, nodes.front()->Location(), {}));
			if (!probe)
				throw std::runtime_error("could not spawn the reach probe pawn");

			probe->SetCollision(false, false, false);
			probe->bHidden() = true;
			probe->SetCollisionSize(radius, height);

			std::ostringstream out;
			out << "# surrealengine reach corpus replay\n";
			out << "#\tcols_R\tidx from to dist dz actorreachable pointreachable\n";
			out << "#\treach_probe\t" << probeClass->Name.ToString()
				<< "\tr=" << static_cast<int>(probe->CollisionRadius())
				<< "\th=" << static_cast<int>(probe->CollisionHeight())
				<< "\tstep=" << static_cast<int>(probe->MaxStepHeight())
				<< "\tplayer=" << (probe->bIsPlayer() ? "True" : "False")
				<< "\twalk=" << (probe->bCanWalk() ? "True" : "False")
				<< "\tswim=" << (probe->bCanSwim() ? "True" : "False")
				<< "\tfly=" << (probe->bCanFly() ? "True" : "False") << "\n";

			// The navigation graph as this engine holds it. Both engines load the
			// same .unr, so the reachspec indices are directly comparable.
			for (size_t n = 0; n < nodes.size(); n++)
			{
				UNavigationPoint* node = nodes[n];
				auto join = [](auto values)
				{
					std::string text;
					for (size_t i = 0; i < 16; i++)
					{
						if (i > 0)
							text += ",";
						text += std::to_string(values[i]);
					}
					return text;
				};
				out << "G\t" << n
					<< '\t' << node->Name.ToString()
					<< '\t' << ClassNameOrDash(node)
					<< '\t' << static_cast<int>(node->Location().x)
					<< '\t' << static_cast<int>(node->Location().y)
					<< '\t' << static_cast<int>(node->Location().z)
					<< '\t' << node->ExtraCost()
					<< '\t' << (node->bEndPoint() ? "True" : "False")
					<< '\t' << (node->bPlayerOnly() ? "True" : "False")
					<< '\t' << join(node->Paths())
					<< '\t' << join(node->upstreamPaths())
					<< '\t' << join(node->PrunedPaths())
					<< '\n';
			}
			out << "#\tnodegraph_done\t" << nodes.size() << "\n";

			int idx = 0;
			for (UNavigationPoint* from : nodes)
			{
				if (!probe->SetLocation(from->Location()))
				{
					out << "#\treach_skip\t" << from->Name.ToString() << "\tsetlocation\n";
					continue;
				}
				probe->SetPhysics(PHYS_Walking);

				for (UNavigationPoint* to : nodes)
				{
					if (to == from)
						continue;
					const vec3 delta = to->Location() - from->Location();
					const float distance = length(delta);
					if (distance > reachDistance)
						continue;

					// ActorReachable leaves the pawn's physics mode altered, so every
					// call is re-armed from the same state or later answers drift.
					auto arm = [&]
					{
						probe->SetLocation(from->Location());
						probe->SetPhysics(PHYS_Walking);
					};

					arm();
					const bool actorReachable = probe->ActorReachable(to, true,
						PawnMovement::DirectReachCommandCallerOrigin::ScriptActorReachable);
					arm();
					const bool pointReachable = probe->PointReachable(to->Location());

					out << "R\t" << idx++
						<< '\t' << from->Name.ToString()
						<< '\t' << to->Name.ToString()
						<< '\t' << static_cast<int>(distance)
						<< '\t' << static_cast<int>(delta.z)
						<< '\t' << (actorReachable ? 1 : 0)
						<< '\t' << (pointReachable ? 1 : 0)
						<< '\n';
				}
			}

			probe->Destroy();
			out << "#\treach_corpus_done\t" << idx << "\n";
			return out.str();
		}

		// The mirror of the retail mutator's DumpVisionCorpus: sweep an observer pawn's
		// PeripheralVision and facing yaw against every navigation point within visionDistance,
		// and record CanSee/LineOfSightTo the same way UPawn::CanSee builds its own vision cone.
		std::string BuildVisionCorpus(float radius, float height, float visionDistance,
			int yawSteps, int stride) const
		{
			UActor* tracingActor = FindTracingActor();
			if (!tracingActor)
				throw std::runtime_error("no actor available to spawn from");

			// Walk NavigationPointList rather than the actor list, because the mutator
			// does and the stride below picks observers by position in that order.
			std::vector<UNavigationPoint*> nodes;
			for (UNavigationPoint* point = tracingActor->Level()->NavigationPointList();
				point != nullptr; point = point->nextNavigationPoint())
			{
				nodes.push_back(point);
			}
			if (nodes.empty())
				throw std::runtime_error("map has no navigation points");
			if (stride <= 0)
				stride = 1;
			if (yawSteps <= 0)
				yawSteps = 1;

			UClass* probeClass = EngineRef.packages->FindClass("Botpack.TMale1");
			if (!probeClass)
				throw std::runtime_error("Botpack.TMale1 is not available");

			UPawn* observer = UObject::TryCast<UPawn>(tracingActor->Spawn(probeClass,
				nullptr, {}, nodes.front()->Location(), {}));
			if (!observer)
				throw std::runtime_error("could not spawn the vision observer pawn");
			UPawn* target = UObject::TryCast<UPawn>(tracingActor->Spawn(probeClass,
				nullptr, {}, nodes.front()->Location(), {}));
			if (!target)
				throw std::runtime_error("could not spawn the vision target pawn");

			observer->SetCollision(false, false, false);
			observer->SetCollisionSize(radius, height);
			observer->bHidden() = true;

			target->SetCollision(false, false, false);
			target->SetCollisionSize(radius, height);

			std::ostringstream out;
			out << "# surrealengine vision corpus replay\n";
			out << "#\tcols_V\tidx obs tgt dist dz yaw cos1000 periph1000 sightradius"
				<< " visibility cansee los\n";
			out << "#\tvision_probe\t" << probeClass->Name.ToString()
				<< "\tr=" << static_cast<int>(observer->CollisionRadius())
				<< "\th=" << static_cast<int>(observer->CollisionHeight())
				<< "\tsightradius=" << static_cast<int>(observer->SightRadius())
				<< "\tperiphdefault=" << static_cast<int>(1000.0f * observer->PeripheralVision()) << "\n";

			// PeripheralVision is the cosine of the half-angle of the view cone: 0.7 is a tight
			// forward cone, 0.0 is a full hemisphere, -0.2 looks slightly behind the pawn.
			static const float peripheralValues[3] = { 0.7f, 0.0f, -0.2f };

			int idx = 0;
			for (size_t n = 0; n < nodes.size(); n += static_cast<size_t>(stride))
			{
				UNavigationPoint* observerNode = nodes[n];
				if (!observer->SetLocation(observerNode->Location()))
				{
					out << "#\tvision_skip\t" << observerNode->Name.ToString() << "\tsetlocation\n";
					continue;
				}

				for (UNavigationPoint* targetNode : nodes)
				{
					if (targetNode == observerNode)
						continue;
					const vec3 delta = targetNode->Location() - observerNode->Location();
					const float distance = length(delta);
					if (distance > visionDistance)
						continue;

					if (!target->SetLocation(targetNode->Location()))
					{
						out << "#\tvision_skip\t" << targetNode->Name.ToString() << "\tsetlocation\n";
						continue;
					}

					for (float peripheralVision : peripheralValues)
					{
						observer->PeripheralVision() = peripheralVision;

						for (int i = 0; i < yawSteps; i++)
						{
							const int yaw = i * (65536 / yawSteps);
							observer->Rotation() = Rotator(0, yaw, 0);

							const vec3 forward = Coords::Rotation(observer->Rotation()).XAxis;
							const float cosine = dot(
								normalize(target->Location() - observer->Location()), forward);

							const bool canSee = observer->CanSee(target);
							const bool los = observer->LineOfSightTo(target, false);

							out << "V\t" << idx++
								<< '\t' << observerNode->Name.ToString()
								<< '\t' << targetNode->Name.ToString()
								<< '\t' << static_cast<int>(distance)
								<< '\t' << static_cast<int>(delta.z)
								<< '\t' << yaw
								<< '\t' << static_cast<int>(1000.0f * cosine)
								<< '\t' << static_cast<int>(1000.0f * peripheralVision)
								<< '\t' << static_cast<int>(observer->SightRadius())
								<< '\t' << static_cast<int>(target->Visibility())
								<< '\t' << (canSee ? 1 : 0)
								<< '\t' << (los ? 1 : 0)
								<< '\n';
						}
					}
				}
			}

			observer->Destroy();
			target->Destroy();
			out << "#\tvision_corpus_done\t" << idx << "\n";
			return out.str();
		}

		void EmitProbe(std::ostringstream& out, int idx, const std::string& node,
			int direction, const char* kind, const vec3& start, const vec3& end,
			UActor* tracingActor, const vec3& extent, bool traceActors) const
		{
			vec3 hitLocation(0.0f);
			vec3 hitNormal(0.0f);
			UObject* hit = tracingActor->Trace(hitLocation, hitNormal, end, start,
				traceActors, extent);
			if (!hit)
			{
				hitLocation = vec3(0.0f);
				hitNormal = vec3(0.0f);
			}

			out << "T\t" << idx << '\t' << node << '\t' << direction << '\t' << kind
				<< '\t' << static_cast<int>(start.x)
				<< '\t' << static_cast<int>(start.y)
				<< '\t' << static_cast<int>(start.z)
				<< '\t' << static_cast<int>(end.x)
				<< '\t' << static_cast<int>(end.y)
				<< '\t' << static_cast<int>(end.z)
				<< '\t' << ActorNameOrDash(hit)
				<< '\t' << ClassNameOrDash(hit)
				<< '\t' << static_cast<int>(hitLocation.x)
				<< '\t' << static_cast<int>(hitLocation.y)
				<< '\t' << static_cast<int>(hitLocation.z)
				<< '\t' << static_cast<int>(hitNormal.x * 1000.0f)
				<< '\t' << static_cast<int>(hitNormal.y * 1000.0f)
				<< '\t' << static_cast<int>(hitNormal.z * 1000.0f)
				<< '\n';
		}

		Engine& EngineRef;
		bool Complete = false;
		std::string Failure;
	};
}

void RegisterTraceCorpusDriver(HeadlessDriverRegistry& registry)
{
	if (!registry.Contains("trace-corpus"))
	{
		registry.Register("trace-corpus", [](Engine& engine)
		{
			return std::make_unique<TraceCorpusDriver>(engine);
		});
	}
}
