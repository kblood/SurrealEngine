#include "Precomp.h"
#include "UActor.h"
#include "ULevel.h"
#include "UMesh.h"
#include "UTexture.h"
#include "UConSys.h"
#include "USubsystem.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "VM/Bytecode.h"
#include "Package/PackageManager.h"
#include "Package/IniProperty.h"
#include "Engine.h"
#include "BotBenchmark.h"
#include "Render/RenderSubsystem.h"
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_set>

// TODO: Compare behavior more closely with original engine. Might differ depending on game.
static constexpr float stepDownDeltaFactor = 1.3f;

UActor* UActor::Spawn(UClass* SpawnClass, std::optional<UActor*> SpawnOwner, std::optional<NameString> SpawnTag, std::optional<vec3> SpawnLocation, std::optional<Rotator> SpawnRotation)
{
	if (!SpawnClass || SpawnClass->ClsFlags & ClassFlags::Abstract)
	{
		LogMessage("Could not spawn class: " + (SpawnClass ? SpawnClass->Name.ToString() : std::string("null")));
		return nullptr;
	}

	vec3 location = SpawnLocation ? *SpawnLocation : Location();
	Rotator rotation = SpawnRotation ? *SpawnRotation : Rotation();

	float radius = SpawnClass->GetDefaultObject<UActor>()->CollisionRadius();
	float height = SpawnClass->GetDefaultObject<UActor>()->CollisionHeight();
	bool bCollideWorld = SpawnClass->GetDefaultObject<UActor>()->bCollideWorld();
	bool bCollideWhenPlacing = SpawnClass->GetDefaultObject<UActor>()->bCollideWhenPlacing();
	if (bCollideWorld || bCollideWhenPlacing)
	{
		auto result = CheckLocation(location, radius, height, bCollideWorld || bCollideWhenPlacing);
		if (!result.first)
		{
			LogMessage("Could not find usable location when trying to spawn: " + SpawnClass->Name.ToString());
			return nullptr;
		}
		location = result.second;
	}

	// To do: package needs to be grabbed from outer, or the "transient package" if it is None, a virtual package for runtime objects
	// To do: find unique new name in the package
	static std::map<NameString, int> nextIndex;
	NameString name = SpawnClass->Name.ToString() + std::to_string(nextIndex[SpawnClass->Name]++);
	UActor* actor = UObject::Cast<UActor>(engine->LevelPackage->NewObject(name, UObject::Cast<UClass>(SpawnClass), ObjectFlags::Transient, true));

	actor->Outer() = XLevel()->Outer();
	actor->XLevel() = XLevel();
	actor->Level() = Level();
	actor->Tag() = (SpawnTag && !SpawnTag->IsNone()) ? *SpawnTag : SpawnClass->Name;
	actor->bTicked() = bTicked(); // To do: should it tick in the same world tick it was spawned in or wait until the next one?
	actor->Instigator() = Instigator();
	actor->Brush() = nullptr;
	actor->Location() = location;
	actor->OldLocation() = location;
	actor->Rotation() = rotation;
	actor->Region().Zone = actor->Level();
	actor->Index = (int)XLevel()->Actors.size();
	XLevel()->Actors.push_back(actor);
	XLevel()->Collision.AddToCollision(actor);
	XLevel()->Light.AddLight(actor);

	actor->SetOwner(SpawnOwner.has_value() && SpawnOwner.value() ? *SpawnOwner : nullptr);

	if (Level()->bBegunPlay())
	{
		CallEvent(actor, EventName::Spawned);
		CallEvent(actor, EventName::PreBeginPlay);
		CallEvent(actor, EventName::BeginPlay);

		if (actor->bDeleteMe())
		{
			LogMessage("Object deleted itself during Spawn!");
			return nullptr;
		}

		// To do: we need to call EventName::EncroachingOn events here?

		actor->InitActorZone();

		CallEvent(actor, EventName::PostBeginPlay);
		CallEvent(actor, EventName::SetInitialState);
		if (engine->LaunchInfo.IsDeusEx())
			CallEvent(actor, "PostPostBeginPlay");

		actor->InitBase();

		if (engine->LaunchInfo.ue1Version >= 400)
		{
			static bool spawnNotificationLocked = false;
			if (!spawnNotificationLocked)
			{
				struct NotificationLockGuard
				{
					NotificationLockGuard() { spawnNotificationLocked = true; }
					~NotificationLockGuard() { spawnNotificationLocked = false; }
				} lockGuard;

				for (USpawnNotify* notifyObj = Level()->SpawnNotify(); notifyObj != nullptr; notifyObj = notifyObj->Next())
				{
					UClass* cls = notifyObj->ActorClass();
					if (cls && actor->IsA(cls->Name))
						actor = UObject::Cast<UGameInfo>(CallEvent(notifyObj, EventName::SpawnNotification, { ExpressionValue::ObjectValue(actor) }).ToObject());
				}
			}
		}
	}
	if (UProjectile* projectile = UObject::TryCast<UProjectile>(actor))
		BotBenchmark::ProjectileSpawned(projectile, this);

	return actor;
}

void UActor::InitBase()
{
	if (engine->LaunchInfo.ue1Version > 219)
	{
		NameString attachTag = AttachTag();
		if (!attachTag.IsNone())
		{
			for (UActor* levelActor : XLevel()->Actors)
			{
				if (levelActor && levelActor->Tag() == attachTag)
				{
					levelActor->SetBase(this, false);
				}
			}
			return;
		}
	}

	// Find base for certain types
	bool isDecorationInventoryOrPawn = UObject::TryCast<UDecoration>(this) || UObject::TryCast<UInventory>(this) || UObject::TryCast<UPawn>(this);
	if (isDecorationInventoryOrPawn && !ActorBase() && bCollideWorld() && (Physics() == PHYS_None || Physics() == PHYS_Rotating))
	{
		CollisionHitList hits = XLevel()->Collision.OverlapTest(this);
		if (!hits.empty())
		{
			SetBase(hits.front().Actor, true);
		}
	}

	if (engine->LaunchInfo.ue1Version < 400 && !ActorBase()) // Unreal expects a base to always exist. What about UT? TournamentPlayer seems to indicate not.
	{
		SetBase(Level(), false);
	}
}

std::pair<bool, vec3> UActor::CheckLocation(vec3 location, float radius, float height, bool check)
{
	// Search for a valid spot near the location

	if (!check)
		return { true, location };

	// What is a reasonable size for this grid? what did UE1 do?
	int offset[] = { 0, 1, -1 };
	bool found = false;
	float scale = std::max(radius, height);
	for (int z = 0; z < 3 && !found; z++)
	{
		for (int y = 0; y < 3 && !found; y++)
		{
			for (int x = 0; x < 3 && !found; x++)
			{
				vec3 testlocation = location + vec3(offset[x] * scale, offset[y] * scale, offset[z] * scale);
				CollisionHitList hits = XLevel()->Collision.OverlapTest(testlocation, height, radius, false, true, false);
				if (hits.empty())
				{
					location = testlocation;
					found = true;
				}
			}
		}
	}
	return { found, location };
}

bool UActor::Destroy()
{
	//engine->LogMessage("UActor.Destroy(" + Class->FriendlyName.ToString() + ")");

	if (bStatic() || bNoDelete())
		return false;
	if (bDeleteMe())
		return true;
	if (UProjectile* projectile = UObject::TryCast<UProjectile>(this))
		BotBenchmark::ProjectileDestroyed(projectile);

	bDeleteMe() = true;

	//GotoState({}, {}); // What should happen to function calls after Destroy() has been called? Razor2 calls SetRoll afterwards!
	SetBase(nullptr, true);

	engine->audiodev->ActorDestroyed(this);

	ULevel* level = XLevel();

	RemoveFromBspNode();
	level->Collision.RemoveFromCollision(this);
	level->Light.RemoveLight(this);

	CallEvent(this, EventName::Destroyed);

	if (engine->LaunchInfo.IsUnrealTournament_469())
	{
		for (const auto actor : Touching_UT469())
			if (actor)
				UnTouch(actor);
	}
	else
	{
		for (const auto actor : Touching())
			if (actor)
				UnTouch(actor);
	}


	SetOwner(nullptr);

	while (!ChildActors.empty())
	{
		ChildActors.back()->SetOwner(nullptr);
	}
	while (!BasedActors.empty())
	{
		BasedActors.back()->SetBase(nullptr, true);
	}

	if (Index == -1)
		throw std::runtime_error("Actor index was never set!");
	level->Actors[Index] = nullptr;

	return true;
}

PointRegion UActor::FindRegion(const vec3& offset)
{
	return XLevel()->Model->FindRegion(Location() + offset, Level());
}

void UActor::InitActorZone()
{
	Region() = FindRegion();
	if (Region().Zone->bWaterZone() && !this->IsA("Projectile"))
	{
		SetPhysics(PHYS_Swimming);
		SetBase(nullptr, true);
	}
}

void UActor::UpdateActorZone()
{
	PointRegion oldregion = Region();
	PointRegion newregion = FindRegion();

	if (oldregion.Zone && oldregion.Zone != newregion.Zone)
		CallEvent(oldregion.Zone, EventName::ActorLeaving, { ExpressionValue::ObjectValue(this) });

	Region() = newregion;

	if (newregion.Zone && oldregion.Zone != newregion.Zone)
	{
		CallEvent(this, EventName::ZoneChange, { ExpressionValue::ObjectValue(newregion.Zone) });
		CallEvent(newregion.Zone, EventName::ActorEntered, { ExpressionValue::ObjectValue(this) });
	}

	if (Region().Zone)
	{
		if (Region().Zone->bDestructive() && IsA("Carcass"))
		{
			// If the actor is a Carcass and the zone is marked as bDestructive, destroy it.
			Destroy();
		}
		else if (engine->LaunchInfo.ue1Version > 219 && Owner() == nullptr && Region().Zone->bNoInventory() && IsA("Inventory"))
		{
			// If the new zone is bNoInventory, destroy Inventory that's not owned by anyone (i.e. in pickup state).
			Destroy();
		}
	}
}

void UActor::SetOwner(UActor* newOwner)
{
	if (Owner())
	{
		CallEvent(Owner(), EventName::LostChild, { ExpressionValue::ObjectValue(this) });
		Owner()->RemoveChildActor(this);
	}

	Owner() = newOwner;

	if (Owner())
	{
		CallEvent(Owner(), EventName::GainedChild, { ExpressionValue::ObjectValue(this) });
		Owner()->AddChildActor(this);
	}
}

void UActor::AddChildActor(UActor* actor)
{
	if (actor)
		ChildActors.push_back(actor);
}

void UActor::RemoveChildActor(UActor* actor)
{
	if (!actor)
		return;

	auto it = ChildActors.begin();

	while (it != ChildActors.end())
	{
		if (*it == actor)
		{
			ChildActors.erase(it);
			return;
		}
		it++;
	}
}

void UActor::AddBasedActor(UActor* actor)
{
	if (actor)
		BasedActors.push_back(actor);
}

void UActor::RemoveBasedActor(UActor* actor)
{
	if (!actor)
		return;

	auto it = BasedActors.begin();

	while (it != BasedActors.end())
	{
		if (*it == actor)
		{
			BasedActors.erase(it);
			return;
		}
		it++;
	}
}

void UActor::SetBase(UActor* newBase, bool sendBaseChangeEvent)
{
	if (ActorBase() != newBase)
	{
		if (this->IsBasedOn(newBase))
			return; // don't allow any cycles in the tree

		if (ActorBase() && ActorBase() != Level())
		{
			ActorBase()->RemoveBasedActor(this);
			ActorBase()->StandingCount() = (uint8_t)std::min<size_t>(ActorBase()->BasedActors.size(), 0xff);
			CallEvent(ActorBase(), EventName::Detach, { ExpressionValue::ObjectValue(this) });
		}

		ActorBase() = newBase;

		if (ActorBase() && ActorBase() != Level())
		{
			ActorBase()->AddBasedActor(this);
			ActorBase()->StandingCount() = (uint8_t)std::min<size_t>(ActorBase()->BasedActors.size(), 0xff);
			// Note: in the unlikely case of an actor having > 255 bases, StandingCount() won't be an accurate number.
			CallEvent(ActorBase(), EventName::Attach, { ExpressionValue::ObjectValue(this) });
		}

		if (sendBaseChangeEvent)
			CallEvent(this, EventName::BaseChange);
	}
}

void UActor::Tick(float elapsed)
{
	TickAnimation(elapsed);
	if (engine->LaunchInfo.IsDeusEx())
		TickBlendAnimation(elapsed);

	if (Role() >= ROLE_SimulatedProxy && IsEventEnabled(EventName::Tick))
	{
		CallEvent(this, EventName::Tick, { ExpressionValue::FloatValue(elapsed) });
	}

	if (StateFrame)
	{
		if (StateFrame->LatentState == LatentRunState::Sleep)
		{
			LatentFloat() = std::max(LatentFloat() - elapsed, 0.0f);
			if (LatentFloat() == 0.0f)
				StateFrame->LatentState = LatentRunState::Continue;
		}
		else if (StateFrame->LatentState == LatentRunState::FinishInterpolation)
		{
			if (!bInterpolating())
				StateFrame->LatentState = LatentRunState::Continue;
		}

		if (Role() >= ROLE_SimulatedProxy && StateFrame->LatentState == LatentRunState::Continue)
		{
			StateFrame->Tick();
		}
	}

	TickPhysics(elapsed);

	if (TimerRate() > 0.0f) // Role() == ROLE_Authority && RemoteRole() == ROLE_AutonomousProxy
	{
		TimerCounter() += elapsed;
		while (TimerRate() > 0.0f && TimerCounter() > TimerRate())
		{
			TimerCounter() -= TimerRate();
			if (!bTimerLoop())
				TimerRate() = 0.0f;
			CallEvent(this, EventName::Timer);
		}
	}
}

void UActor::TickPhysics(float elapsed)
{
	for (float timeLeft = elapsed; timeLeft > 0.0f && !bDeleteMe(); timeLeft -= 0.02f)
	{
		float physTimeElapsed = std::min(timeLeft, 0.02f);
		int mode = Physics();
		if (mode != PHYS_None)
		{
			switch (mode)
			{
			case PHYS_Walking: TickWalking(physTimeElapsed); break;
			case PHYS_Falling: TickFalling(physTimeElapsed); break;
			case PHYS_Swimming: TickSwimming(physTimeElapsed); break;
			case PHYS_Flying: TickFlying(physTimeElapsed); break;
			case PHYS_Rotating: break;
			case PHYS_Projectile: TickProjectile(physTimeElapsed); break;
			case PHYS_Rolling: TickRolling(physTimeElapsed); break;
			case PHYS_Interpolating: TickInterpolating(physTimeElapsed); break;
			case PHYS_MovingBrush: TickMovingBrush(physTimeElapsed); break;
			case PHYS_Spider: TickSpider(physTimeElapsed); break;
			case PHYS_Trailer: TickTrailer(physTimeElapsed); break;
			}
			TickRotating(physTimeElapsed); // Rotation logic applies to multiple physics modes and not just PHYS_Rotating
		}

		if (engine->LaunchInfo.ue1Version >= 400)
		{
			if (PendingTouch())
			{
				CallEvent(PendingTouch(), EventName::PostTouch, { ExpressionValue::ObjectValue(this) });
				if (PendingTouch())
				{
					UActor* cur = PendingTouch();
					UActor* next = cur->PendingTouch();
					PendingTouch() = next;
					cur->PendingTouch() = nullptr;
				}
			}
		}
	}
}

void UActor::TickWalking(float elapsed)
{
	// Only pawns can walk!
	UPawn* pawn = UObject::TryCast<UPawn>(this);
	if (!pawn)
		return;

	if (Region().ZoneNumber == 0)
	{
		CallEvent(this, EventName::FellOutOfWorld);
		return;
	}

	// Save our starting point and state

	OldLocation() = Location();
	bJustTeleported() = false;

	// Update the actor velocity based on the acceleration and zone

	UZoneInfo* zone = Region().Zone;
	// UDecoration* decor = UObject::TryCast<UDecoration>(this);
	UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(this);

	Velocity().z = 0.0f;

	if (dot(Acceleration(), Acceleration()) > 0.0001f)
	{
		float accelRate = pawn->AccelRate();
		if (player && player->bIsWalking())
			accelRate *= 0.3f;

		// Acceleration must never exceed the acceleration rate
		float accelSpeed = length(Acceleration());
		vec3 accelDir = Acceleration() * (1.0f / accelSpeed);
		if (accelSpeed > accelRate)
			Acceleration() = accelDir * accelRate;

		float speed = length(Velocity());
		Velocity() = Velocity() - (Velocity() - accelDir * speed) * (zone->ZoneGroundFriction() * elapsed);
	}
	else
	{
		float speed = length(Velocity());
		if (speed > 0.0f)
		{
			float newSpeed = std::max(speed - speed * zone->ZoneGroundFriction() * 2.0f * elapsed, 0.0f);
			Velocity() = Velocity() * (newSpeed / speed);
		}
	}

	Velocity() = Velocity() + Acceleration() * elapsed;

	float maxSpeed = player ? player->GroundSpeed() : pawn->GroundSpeed() * pawn->DesiredSpeed();
	if (player && player->bIsWalking())
		maxSpeed *= 0.3f;

	float speed = length(Velocity());
	if (speed > 0.0f && speed > maxSpeed)
		Velocity() = Velocity() * (maxSpeed / speed);

	Velocity().z = 0.0f;

	// The classic step up, move and step down algorithm:

	float gravityDirection = zone->ZoneGravity().z > 0.0f ? 1.0f : -1.0f;
	vec3 stepUpDelta(0.0f, 0.0f, -gravityDirection * pawn->MaxStepHeight());
	vec3 stepDownDelta(0.0f, 0.0f, gravityDirection * pawn->MaxStepHeight() * stepDownDeltaFactor);

	// "Step up and move" as long as we have time left and only hitting surfaces with low enough slope that it could be walked
	float timeLeft = elapsed;
	vec3 vel = Velocity() + zone->ZoneVelocity() * elapsed * 25.0f;
	// Axis-aligned movement is still movement. The old AND test discarded an
	// exact X- or Y-cardinal velocity and left bots stationary in straight map
	// corridors.
	bool isMoving = dot(vel.xy(), vel.xy()) > 0.0001f;
	if (isMoving)
	{
		auto dispatchHitWall = [&](const CollisionHit& wallHit)
		{
			UActor* blocker = wallHit.Actor ? wallHit.Actor : Level();
			const bool eventEnabled = IsEventEnabled(EventName::HitWall);
			const NameString stateBefore = GetStateName();
			const int physicsBefore = Physics();
			const vec3 destinationBefore = pawn->Destination();
			const vec3 focusBefore = pawn->Focus();
			const float moveTimerBefore = pawn->MoveTimer();
			const bool fromWallBefore = pawn->bFromWall();
			const int latentBefore = StateFrame ? static_cast<int>(StateFrame->LatentState) : -1;
			const size_t statementBefore = StateFrame ? StateFrame->StatementIndex : 0;
			const uint64_t hitId = BotBenchmark::IsActive() ? BotBenchmark::NextWalkingHitWallId() : 0;
			if (BotBenchmark::IsActive())
			{
				BotBenchmark::Emit("walking_hit_wall", {
					{ "hit_id", std::to_string(hitId) },
					{ "actor", Name.ToString() },
					{ "blocker", blocker ? blocker->Name.ToString() : "None" },
					{ "blocker_class", blocker ? UObject::GetUClassFullName(blocker).ToString() : "None" },
					{ "blocker_is_mover", blocker && blocker->IsA("Mover") ? "true" : "false" },
					{ "event_enabled", eventEnabled ? "true" : "false" },
					{ "normal_x", std::to_string(wallHit.Normal.x) },
					{ "normal_y", std::to_string(wallHit.Normal.y) },
					{ "normal_z", std::to_string(wallHit.Normal.z) },
					{ "physics_before", std::to_string(physicsBefore) },
					{ "state_before", stateBefore.ToString() },
					{ "latent_before", std::to_string(latentBefore) },
					{ "statement_index_before", std::to_string(statementBefore) },
					{ "location_x_before", std::to_string(Location().x) },
					{ "location_y_before", std::to_string(Location().y) },
					{ "location_z_before", std::to_string(Location().z) },
					{ "destination_x_before", std::to_string(destinationBefore.x) },
					{ "destination_y_before", std::to_string(destinationBefore.y) },
					{ "destination_z_before", std::to_string(destinationBefore.z) },
					{ "focus_x_before", std::to_string(focusBefore.x) },
					{ "focus_y_before", std::to_string(focusBefore.y) },
					{ "focus_z_before", std::to_string(focusBefore.z) },
					{ "move_timer_before", std::to_string(moveTimerBefore) },
					{ "b_from_wall_before", fromWallBefore ? "true" : "false" }
				});
			}

			CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(wallHit.Normal), ExpressionValue::ObjectValue(blocker) });

			if (BotBenchmark::IsActive())
			{
				const int latentAfter = StateFrame ? static_cast<int>(StateFrame->LatentState) : -1;
				const size_t statementAfter = StateFrame ? StateFrame->StatementIndex : 0;
				int adjustFromWallLabelAfter = -1;
				if (StateFrame && StateFrame->Func && StateFrame->Func->Name == NameString("Roaming"))
				{
					UState* roaming = static_cast<UState*>(StateFrame->Func);
					if (roaming->Code)
						adjustFromWallLabelAfter = roaming->Code->FindLabelIndex(NameString("AdjustFromWall"));
				}
				BotBenchmark::Emit("walking_hit_wall_result", {
					{ "hit_id", std::to_string(hitId) },
					{ "actor", Name.ToString() },
					{ "blocker", blocker ? blocker->Name.ToString() : "None" },
					{ "blocker_class", blocker ? UObject::GetUClassFullName(blocker).ToString() : "None" },
					{ "event_enabled", eventEnabled ? "true" : "false" },
					{ "physics_after", std::to_string(Physics()) },
					{ "state_after", GetStateName().ToString() },
					{ "state_changed", stateBefore != GetStateName() ? "true" : "false" },
					{ "latent_after", std::to_string(latentAfter) },
					{ "statement_index_after", std::to_string(statementAfter) },
					{ "adjust_from_wall_label_index_after", std::to_string(adjustFromWallLabelAfter) },
					{ "location_x_after", std::to_string(Location().x) },
					{ "location_y_after", std::to_string(Location().y) },
					{ "location_z_after", std::to_string(Location().z) },
					{ "destination_x_after", std::to_string(pawn->Destination().x) },
					{ "destination_y_after", std::to_string(pawn->Destination().y) },
					{ "destination_z_after", std::to_string(pawn->Destination().z) },
					{ "focus_x_after", std::to_string(pawn->Focus().x) },
					{ "focus_y_after", std::to_string(pawn->Focus().y) },
					{ "focus_z_after", std::to_string(pawn->Focus().z) },
					{ "move_timer_after", std::to_string(pawn->MoveTimer()) },
					{ "b_from_wall_after", pawn->bFromWall() ? "true" : "false" },
					{ "health_after", std::to_string(pawn->Health()) },
					{ "b_delete_me_after", pawn->bDeleteMe() ? "true" : "false" }
				});
				BotBenchmark::WalkingHitWallResult(hitId, pawn, blocker, eventEnabled,
					wallHit.Normal.x, wallHit.Normal.y, wallHit.Normal.z);
			}
		};

		for (int iteration = 0; timeLeft > 0.0f && iteration < 5; iteration++)
		{
			// This is the last position known to have walkable floor. MayFall is
			// allowed to veto this iteration's ledge crossing by clearing bCanJump.
			const vec3 groundedLocation = Location();
			vec3 moveDelta = vel * timeLeft;

			// step up first so we can get past stairs going up
			TryMove(stepUpDelta);

			// try move forward
			CollisionHit hit = TryMove(moveDelta);
			timeLeft -= timeLeft * hit.Fraction;
			moveDelta = vel * timeLeft;

			// move back down to original vertical position
			TryMove(-stepUpDelta);

			if (hit.Fraction < FLT_EPSILON)
			{
				// try move forward once again, in case our head bumped into something while stepped up
				hit = TryMove(moveDelta);
				timeLeft -= timeLeft * hit.Fraction;
			}

			if (hit.Fraction < 1.0f)
			{
				if (player && hit.Actor)
				{
					if (UObject::IsType<UDecoration>(hit.Actor) && UObject::Cast<UDecoration>(hit.Actor)->bPushable() && dot(hit.Normal, moveDelta) < -0.9f)
					{
						// We hit a pushable decoration that is facing our movement direction

						bJustTeleported() = true;
						vel = Velocity() = Velocity() * Mass() / (Mass() + hit.Actor->Mass());
						dispatchHitWall(hit);
						timeLeft = 0.0f;
					}
					else if (hit.Actor->bCollideActors() && hit.Actor->CollisionHeight() > 0.0f && hit.Actor->CollisionRadius() > 0.0f)
					{
						// Player Pawns (including UT Bots) still need the script callback
						// for actor blockers. Bot.HitWall coordinates movers through
						// Mover.HandleDoor and falls back to stock wall adjustment.
						dispatchHitWall(hit);
					}
				}
				else if (hit.Normal.z < 0.2f && hit.Normal.z > -0.2f)
				{
					// We hit a wall
					dispatchHitWall(hit);

					vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
					if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
					{
						hit = TryMove(alignedDelta);
						timeLeft -= timeLeft * hit.Fraction;
						if (hit.Fraction < 1.0f)
						{
							dispatchHitWall(hit);
						}
					}
					else
					{
						timeLeft = 0.0f;
					}
				}
			}

			// Check if unrealscript got us out of walking mode
			if (Physics() != PHYS_Walking)
				return;

			// Can we reach the ground from here if we step down? (dry run)
			CollisionHit floorHit = TryMove(stepDownDelta, true);
			if (floorHit.Fraction == 1.0f || floorHit.Normal.z < 0.7071f)
			{
				// UE1 gives a walking Pawn that is otherwise allowed to jump one
				// script callback immediately before it walks off a ledge. Stock bot
				// states use MayFall to clear bCanJump when the drop is unsafe.
				const bool requestedMayFall = pawn->bCanJump();
				if (requestedMayFall)
					CallEvent(pawn, EventName::MayFall);

				// The event may destroy the Pawn or transition it to another physics
				// mode/state. Do not apply stale walking decisions afterward.
				if (pawn->bDeleteMe() || pawn->Physics() != PHYS_Walking)
					return;

				if (!pawn->bCanJump())
				{
					// Back out only this ungrounded movement iteration. The route to
					// groundedLocation was just traversed, so this remains collision
					// checked while avoiding a direct location teleport.
					TryMove(groundedLocation - Location());
					Velocity() = vec3(0.0f);
					Acceleration() = vec3(0.0f);
					if (BotBenchmark::IsActive())
					{
						BotBenchmark::Emit("ledge_fall_prevented", {
							{ "actor", pawn->Name.ToString() },
							{ "state", pawn->GetStateName().ToString() },
							{ "may_fall_dispatched", requestedMayFall ? "true" : "false" }
						});
					}
					return;
				}

				if (BotBenchmark::IsActive())
				{
					BotBenchmark::Emit("ledge_fall_allowed", {
						{ "actor", pawn->Name.ToString() },
						{ "state", pawn->GetStateName().ToString() },
						{ "may_fall_dispatched", requestedMayFall ? "true" : "false" }
					});
				}

				// No ground was found and script left the fall enabled.
				SetPhysics(PHYS_Falling);
				SetBase(nullptr, true);
				return;
			}

			// We could reach the ground. Step down there.
			floorHit = TryMove(stepDownDelta);
			if (floorHit.Fraction != 1.0f)
				SetBase(floorHit.Actor, true);
		}
	}
	else
	{
		// Can we reach the ground from here?
		CollisionHit floorHit = TryMove(stepDownDelta, true);
		if (floorHit.Fraction == 1.0f || floorHit.Normal.z < 0.7071f)
		{
			// No we couldn't. We are falling
			SetPhysics(PHYS_Falling);
			SetBase(nullptr, true);
		}
	}

	if (!bJustTeleported())
		Velocity() = (Location() - OldLocation()) / elapsed;
	Velocity().z = 0.0f;
}

void UActor::TickFalling(float elapsed)
{
	if (Region().ZoneNumber == 0)
	{
		CallEvent(this, EventName::FellOutOfWorld);
		return;
	}

	UZoneInfo* zone = Region().Zone;
	UDecoration* decor = UObject::TryCast<UDecoration>(this);
	UPawn* pawn = UObject::TryCast<UPawn>(this);

	// UnrealScript property references
	vec3& acceleration = Acceleration();
	vec3& velocity = Velocity();
	vec3& oldLocation = OldLocation();
	vec3& location = Location();
	float groundSpeed = 0.0f;

	if (pawn)
	{
		groundSpeed = pawn->GroundSpeed();
		float maxAccel = engine->LaunchInfo.ue1Version > 219 ? pawn->AirControl() * pawn->AccelRate() : 0.0f;
		float accel = length(acceleration);
		if (accel > maxAccel)
			acceleration = normalize(acceleration) * maxAccel;
	}

	float gravityScale = 2.0f;
	float fluidFriction = 0.0f;

	if (decor && decor->bBobbing())
	{
		gravityScale = 1.0f;
	}
	else if (pawn && pawn->FootRegion().Zone->bWaterZone() && velocity.z < 0.0f)
	{
		fluidFriction = pawn->FootRegion().Zone->ZoneFluidFriction();
	}

	OldLocation() = Location();
	bJustTeleported() = false;

	float fluidFactor = 1.0f - fluidFriction * elapsed;
	vec3 accelVector = acceleration * 1.5f;
	vec3 gravityVector = gravityScale * zone->ZoneGravity();

	vec3 oldVelocity = velocity;
	vec3 newVelocity = oldVelocity * fluidFactor + (accelVector + gravityVector) * 0.5f * elapsed;

	// Limit air control to controlling which direction we are moving in the XY plane, but not increase the speed beyond the ground speed
	vec2 velocity2d = velocity.xy();
	vec2 newVelocity2d = newVelocity.xy();
	float curSpeedSquared = dot(velocity2d, velocity2d);
	if (pawn && curSpeedSquared >= (groundSpeed * groundSpeed) && dot(newVelocity2d, newVelocity2d) > curSpeedSquared)
	{
		float xySpeed = length(velocity2d);
		newVelocity = vec3(normalize(newVelocity2d) * xySpeed, newVelocity.z);
	}
	velocity = newVelocity;

	float timeLeft = elapsed;
	for (int iteration = 0; timeLeft > 0.0f && iteration < 5; iteration++)
	{
		float zoneTerminalVelocity = zone->ZoneTerminalVelocity();
		if (dot(velocity, velocity) > zoneTerminalVelocity * zoneTerminalVelocity)
		{
			velocity = normalize(velocity) * zoneTerminalVelocity;
			newVelocity = velocity;
		}

		vec3 moveDelta = (newVelocity + zone->ZoneVelocity() * elapsed * 25.0f) * timeLeft;
		vec3 dirNormal = normalize(newVelocity);

		CollisionHit hit = TryMove(moveDelta);
		timeLeft -= timeLeft * hit.Fraction;

		if (hit.Fraction < 1.0f)
		{
			if (hit.Actor && hit.Actor->IsA("Pawn"))
			{
				// So projectiles don't think they hit a wall.
			}
			else
			{
				CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });
			}

			// Hit the level
			if (bBounce())
			{
				vec3 reflectedDelta = reflect(moveDelta, hit.Normal);
				hit = TryMove(reflectedDelta);
			}
			else
			{
				if (hit.Normal.z < 0.7071f)
				{
					// We hit a slope. Try to follow it.
					vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
					if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
					{
						hit = TryMove(alignedDelta);
						if (hit.Fraction < 1.0f && hit.Normal.z > 0.7071f)
						{
							PhysLanded(hit.Actor, hit.Normal);
							return;
						}
					}

					// adjust velocity along the slope
					if (!bBounce() && !bJustTeleported())
						velocity = (location - oldLocation) / elapsed;

					timeLeft = 0.0f;
				}
				else
				{
					PhysLanded(hit.Actor, hit.Normal);
					timeLeft = 0.0f;
				}
			}
		}
	}
}

void UActor::TickSwimming(float elapsed)
{
	// Only pawns can swim!
	UPawn* pawn = UObject::TryCast<UPawn>(this);

	if (!pawn)
		return;

	if (Region().ZoneNumber == 0)
	{
		CallEvent(this, EventName::FellOutOfWorld);
		return;
	}

	// Save our starting point and state
	OldLocation() = Location();
	bJustTeleported() = false;

	// Update the actor velocity based on the acceleration and zone

	UZoneInfo* zone = Region().Zone;
	// UDecoration* decor = UObject::TryCast<UDecoration>(this);
	UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(this);

	if (dot(Acceleration(), Acceleration()) > 0.0001f)
	{
		float accelRate = pawn->AccelRate() * 0.3f;

		// Acceleration must never exceed the acceleration rate
		float accelSpeed = length(Acceleration());
		vec3 accelDir = Acceleration() * (1.0f / accelSpeed);
		if (accelSpeed > accelRate)
			Acceleration() = accelDir * accelRate;

		float speed = length(Velocity());
		Velocity() = Velocity() - (Velocity() - accelDir * speed) * (zone->ZoneFluidFriction() * elapsed);
	}
	else
	{
		float speed = length(Velocity());
		if (speed > 0.0f)
		{
			float newSpeed = std::max(speed - speed * zone->ZoneFluidFriction() * 2.0f * elapsed, 0.0f);
			Velocity() = Velocity() * (newSpeed / speed);
		}
	}

	Velocity() = Velocity() + Acceleration() * elapsed;

	float maxSpeed = player ? player->WaterSpeed() : pawn->WaterSpeed() * pawn->DesiredSpeed();

	float speed = length(Velocity());
	if (speed > 0.0f && speed > maxSpeed)
		Velocity() = Velocity() * (maxSpeed / speed);

	//float gravityDirection = zone->ZoneGravity().z > 0.0f ? 1.0f : -1.0f;

	float timeLeft = elapsed;
	vec3 vel = Velocity() + zone->ZoneVelocity() * elapsed * 25.0f;
	bool isMoving = (vel.x != 0.0f && vel.y != 0.0f);
	if (isMoving)
	{
		for (int iteration = 0; timeLeft > 0.0f && iteration < 5; iteration++)
		{
			vec3 moveDelta = vel * timeLeft;

			CollisionHit hit = TryMove(moveDelta);
			timeLeft -= timeLeft * hit.Fraction;
			moveDelta = vel * timeLeft;

			if (hit.Fraction < 1.0f)
			{
				if (player && UObject::IsType<UDecoration>(hit.Actor) && UObject::Cast<UDecoration>(hit.Actor)->bPushable() && dot(hit.Normal, moveDelta) < -0.9f)
				{
					// We hit a pushable decoration that is facing our movement direction

					bJustTeleported() = true;
					Velocity() = Velocity() * Mass() / (Mass() + hit.Actor->Mass());
					CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });
					timeLeft = 0.0f;
				}
				else
				{
					// We hit a wall

					CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });

					vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
					if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
					{
						hit = TryMove(alignedDelta);
						timeLeft -= timeLeft * hit.Fraction;
						if (hit.Fraction < 1.0f)
						{
							CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });
						}
					}
					else
					{
						timeLeft = 0.0f;
					}
				}
			}
		}
	}

	if (!bJustTeleported())
		Velocity() = (Location() - OldLocation()) / elapsed;

	if (!Region().Zone->bWaterZone())
	{
		// We moved out of water.
		// Give the player a push
		if (Velocity().z > 0.0f)
			Velocity().z = std::max(Velocity().z, (100.0f + length(Velocity().xy())) * 0.5f);
		if (Physics() == PHYS_Swimming)
			SetPhysics(PHYS_Falling);
	}
}

void UActor::TickFlying(float elapsed)
{
	// Only pawns can fly!
	UPawn* pawn = UObject::TryCast<UPawn>(this);
	if (!pawn)
		return;

	if (Region().ZoneNumber == 0)
	{
		CallEvent(this, EventName::FellOutOfWorld);
		return;
	}

	// Save our starting point and state

	OldLocation() = Location();
	bJustTeleported() = false;

	// Update the actor velocity based on the acceleration and zone

	UZoneInfo* zone = Region().Zone;
	UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(this);

	if (dot(Acceleration(), Acceleration()) > 0.0001f)
	{
		float accelRate = pawn->AccelRate();

		// Acceleration must never exceed the acceleration rate
		float accelSpeed = length(Acceleration());
		vec3 accelDir = Acceleration() * (1.0f / accelSpeed);
		if (accelSpeed > accelRate)
			Acceleration() = accelDir * accelRate;

		float speed = length(Velocity());
		Velocity() = Velocity() - (Velocity() - accelDir * speed) * (zone->ZoneFluidFriction() * elapsed);
	}
	else
	{
		float speed = length(Velocity());
		if (speed > 0.0f)
		{
			float newSpeed = std::max(speed - speed * zone->ZoneFluidFriction() * 2.0f * elapsed, 0.0f);
			Velocity() = Velocity() * (newSpeed / speed);
		}
	}

	Velocity() = Velocity() + Acceleration() * elapsed;

	float maxSpeed = player ? player->AirSpeed() : pawn->AirSpeed() * pawn->DesiredSpeed();

	float speed = length(Velocity());
	if (speed > 0.0f && speed > maxSpeed)
		Velocity() = Velocity() * (maxSpeed / speed);

	float timeLeft = elapsed;
	vec3 vel = Velocity() + zone->ZoneVelocity() * elapsed * 25.0f;
	bool isMoving = (vel.x != 0.0f && vel.y != 0.0f);
	if (isMoving)
	{
		for (int iteration = 0; timeLeft > 0.0f && iteration < 5; iteration++)
		{
			vec3 moveDelta = vel * timeLeft;

			CollisionHit hit = TryMove(moveDelta);
			timeLeft -= timeLeft * hit.Fraction;
			moveDelta = vel * timeLeft;

			if (hit.Fraction < 1.0f)
			{
				// We hit a wall
				CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });

				vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
				if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
				{
					hit = TryMove(alignedDelta);
					timeLeft -= timeLeft * hit.Fraction;
					if (hit.Fraction < 1.0f)
					{
						CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });
					}
				}
				else
				{
					timeLeft = 0.0f;
				}
			}
		}
	}

	if (!bJustTeleported())
		Velocity() = (Location() - OldLocation()) / elapsed;
	Velocity().z = 0.0f;
}

void UActor::TickRotating(float elapsed)
{
	if (bRotateToDesired())
	{
		if (Rotation() != DesiredRotation())
		{
			Rotator rot = Rotation();
			if (bFixedRotationDir())
			{
				rot.Yaw = Rotator::TurnToFixed(rot.Yaw, DesiredRotation().Yaw, (int)(RotationRate().Yaw * elapsed));
				rot.Pitch = Rotator::TurnToFixed(rot.Pitch, DesiredRotation().Pitch, (int)(RotationRate().Pitch * elapsed));
				rot.Roll = Rotator::TurnToFixed(rot.Roll, DesiredRotation().Roll, (int)(RotationRate().Roll * elapsed));
			}
			else
			{
				rot.Yaw = Rotator::TurnToShortest(rot.Yaw, DesiredRotation().Yaw, (int)std::abs(RotationRate().Yaw * elapsed));
				rot.Pitch = Rotator::TurnToShortest(rot.Pitch, DesiredRotation().Pitch, (int)std::abs(RotationRate().Pitch * elapsed));
				rot.Roll = Rotator::TurnToShortest(rot.Roll, DesiredRotation().Roll, (int)std::abs(RotationRate().Roll * elapsed));
			}
			Rotation() = rot;

			if (Rotation() == DesiredRotation())
			{
				CallEvent(this, EventName::EndedRotation);
			}
		}
	}
	else if (bFixedRotationDir())
	{
		Rotation() += RotationRate() * elapsed;
	}
}

void UActor::TickProjectile(float elapsed)
{
	if (Region().ZoneNumber == 0)
	{
		Destroy();
		return;
	}

	UZoneInfo* zone = Region().Zone;
	UProjectile* projectile = UObject::TryCast<UProjectile>(this);
	UPawn* pawn = UObject::TryCast<UPawn>(this);

	if (zone->bWaterZone())
		Velocity() = Velocity() * std::max(1.0f - zone->ZoneFluidFriction() * 0.2f * elapsed, 0.0f);

	Velocity() = Velocity() + Acceleration() * elapsed;

	if (projectile)
	{
		float maxSpeed = projectile->MaxSpeed();
		if (dot(Velocity(), Velocity()) > maxSpeed * maxSpeed)
		{
			Velocity() = normalize(Velocity()) * maxSpeed;
		}
	}

	OldLocation() = Location();
	bJustTeleported() = false;

	CollisionHit hit = TryMove(Velocity() * elapsed);

	if (hit.Fraction < 1.0f && !hit.Actor && !bDeleteMe() && !bJustTeleported())
	{
		CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });
	}

	if (!bBounce() && !bJustTeleported())
		Velocity() = (Location() - OldLocation()) / elapsed;
}

void UActor::TickRolling(float elapsed)
{
	if (Region().ZoneNumber == 0)
	{
		CallEvent(this, EventName::FellOutOfWorld);
		return;
	}

	// Save our starting point and state

	OldLocation() = Location();
	bJustTeleported() = false;

	// Update the actor velocity based on the acceleration and zone

	UZoneInfo* zone = Region().Zone;

	float speed = length(Velocity());
	Velocity() = Velocity() - speed * (normalize(Velocity()) - normalize(Acceleration())) * zone->ZoneGroundFriction() * elapsed;
	Velocity() = Velocity() * (1.0f - zone->ZoneFluidFriction() * elapsed) + Acceleration() * elapsed;


	vec3 moveDelta = (Velocity() + zone->ZoneVelocity() * elapsed * 25.0f) * elapsed;
	CollisionHit hit = TryMove(moveDelta);

	if (hit.Fraction < 1.0f && hit.Normal.z < 0.7071f)
	{
		CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });

		vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
		if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
			TryMove(alignedDelta);
	}

	if (Physics() != PHYS_Rolling)
		return;

	float gravityDirection = zone->ZoneGravity().z > 0.0f ? 1.0f : -1.0f;
	constexpr float stepHeightRatio = 25.0f / 47.5f; // not sure what this should be. Appears that humans have a step height of 25.0 and collision height of 47.5, so I guess I'll use that ratio
	vec3 stepDownDelta(0.0f, 0.0f, gravityDirection * stepHeightRatio * CollisionHeight() * stepDownDeltaFactor);

	// Can we reach the ground from here?
	CollisionHit floorHit = TryMove(stepDownDelta, true);
	if (floorHit.Fraction == 1.0f || floorHit.Normal.z < 0.7071f)
	{
		// No we couldn't. We are falling
		SetPhysics(PHYS_Falling);
		SetBase(nullptr, true);
	}
	else
	{
		// We could reach the ground. Step down there.
		floorHit = TryMove(stepDownDelta);
		if (floorHit.Fraction != 1.0f)
			SetBase(floorHit.Actor, true);
	}

	if (!bJustTeleported())
		Velocity() = (Location() - OldLocation()) / elapsed;
}

void UActor::TickInterpolating(float elapsed)
{
	OldLocation() = Location();

	float timeLeft = elapsed;
	while (timeLeft > 0.0f)
	{
		if (PhysRate() == 0.0f || !bInterpolating())
			break;

		UInterpolationPoint* target = UObject::Cast<UInterpolationPoint>(Target());
		UInterpolationPoint* next = target ? target->Next() : nullptr;
		if (!target || !next)
			break;

		float physAlpha = PhysAlpha();

		if (auto pawn = UObject::TryCast<UPlayerPawn>(this))
		{
			if (engine->LaunchInfo.ue1Version > 219)
			{
				pawn->DesiredFlashScale() = mix(target->ScreenFlashScale(), next->ScreenFlashScale(), physAlpha);
				pawn->DesiredFlashFog() = mix(target->ScreenFlashFog(), next->ScreenFlashFog(), physAlpha);
				pawn->FovAngle() = mix(target->FovModifier(), next->FovModifier(), physAlpha) * Class->GetDefaultObject<UPlayerPawn>()->FovAngle();
				pawn->FlashScale() = vec3(pawn->DesiredFlashScale());
				pawn->FlashFog() = pawn->DesiredFlashFog();
			}
		}

		if (engine->LaunchInfo.ue1Version > 219)
			Level()->TimeDilation() = mix(target->GameSpeedModifier(), next->GameSpeedModifier(), physAlpha);

		float rateModifier = mix(target->RateModifier(), next->RateModifier(), physAlpha);
		float physRate = PhysRate() * rateModifier;
		if (physRate == 0.0f)
			break;

		bool interpolateStart = false, interpolateEnd = false;
		physAlpha += physRate * timeLeft;
		if (physRate < 0.0f && physAlpha < 0.0f)
		{
			timeLeft = physAlpha / physRate;
			physAlpha = 0.0f;
			interpolateStart = true;
		}
		else if (physRate > 0.0f && physAlpha > 1.0f)
		{
			timeLeft = (physAlpha - 1.0f) / physRate;
			physAlpha = 1.0f;
			interpolateEnd = true;
		}
		else
		{
			timeLeft = 0.0f;
		}

		UInterpolationPoint* prev = target->Prev();
		UInterpolationPoint* nextnext = next->Next();
		vec3 location;
		Rotator rotation;
		if (prev && nextnext)
		{
			location = spline(prev->Location(), target->Location(), next->Location(), nextnext->Location(), physAlpha);
			rotation = spline(prev->Rotation(), target->Rotation(), next->Rotation(), nextnext->Rotation(), physAlpha);
		}
		else
		{
			location = mix(target->Location(), next->Location(), physAlpha);
			rotation = mix(target->Rotation(), next->Rotation(), physAlpha);
		}

		PhysAlpha() = physAlpha;
		TryMove(location - Location());
		SetRotation(rotation);

		if (auto pawn = UObject::TryCast<UPawn>(this))
		{
			pawn->ViewRotation() = Rotation();
		}

		if (interpolateStart)
		{
			CallEvent(target, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(this) });
			CallEvent(this, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(target) });

			target = target->Prev();
			if (engine->LaunchInfo.ue1Version > 219)
			{
				while (target && target->bSkipNextPath())
					target = target->Prev();
			}

			Target() = target;
			PhysAlpha() = 1.0f;
		}
		else if (interpolateEnd)
		{
			CallEvent(target, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(this) });
			CallEvent(this, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(target) });

			target = target->Next();
			if (engine->LaunchInfo.ue1Version > 219)
			{
				while (target && target->bSkipNextPath())
					target = target->Next();
			}

			Target() = target;
			PhysAlpha() = 0.0f;
		}
	}

	if (elapsed > 0.0f)
		Velocity() = (Location() - OldLocation()) / elapsed;
}

void UActor::TickMovingBrush(float elapsed)
{
	OldLocation() = Location();

	UMover* mover = UObject::TryCast<UMover>(this);
	if (mover)
	{
		float timeLeft = elapsed;
		while (timeLeft > 0.0f)
		{
			if (!bInterpolating())
				break;

			if (PhysRate() <= 0.0f)
				break;

			float physAlpha = PhysAlpha();
			float physRate = PhysRate();

			physAlpha += physRate * timeLeft;
			if (physAlpha > 1.0f)
			{
				timeLeft = (physAlpha - 1.0f) / physRate;
				physAlpha = 1.0f;
			}
			else
			{
				timeLeft = 0.0f;
			}

			float t = physAlpha;
			if (mover->MoverGlideType() == 1/*MV_GlideByTime*/)
				t = smoothstep(0.0f, 1.0f, t);

			int keyIndex = clamp((int)mover->KeyNum(), 0, 7);
			vec3 oldpos = mover->OldPos();
			vec3 basepos = mover->BasePos();
			vec3 keypos = mover->KeyPos()[keyIndex];
			Rotator oldrot = mover->OldRot();
			Rotator baserot = mover->BaseRot();
			Rotator keyrot = mover->KeyRot()[keyIndex];

			vec3 deltapos = basepos + keypos - oldpos;
			vec3 targetPos = oldpos + deltapos * t;

			Rotator targetRotation = oldrot + (baserot + keyrot - oldrot) * t;

			// LogMessage("Moving brush: " + std::to_string(t) + " key=" + std::to_string(keyIndex) +" keypos=(" + std::to_string(keypos.x) + "," + std::to_string(keypos.y) + "," + std::to_string(keypos.z) + ")");

			if (TryMove(targetPos - Location()).Fraction == 1.0f)
			{
				SetRotation(targetRotation);
				PhysAlpha() = physAlpha;

				if (physAlpha == 1.0f)
				{
					bInterpolating() = false;
					CallEvent(this, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(nullptr) });
				}
			}
		}
	}

	if (elapsed > 0.0f)
		Velocity() = (Location() - OldLocation()) / elapsed;
}

void UActor::TickSpider(float elapsed)
{
}

void UActor::TickTrailer(float elapsed)
{
	if (!Owner())
		return;

	vec3 newLocation = Owner()->Location();

	if (engine->LaunchInfo.ue1Version >= 400 && bTrailerPrePivot())
	{
		newLocation += PrePivot();
	}

	SetLocation(newLocation);

	if ((engine->LaunchInfo.ue1Version < 400 || bTrailerSameRotation()) && DrawType() != DT_Sprite)
	{
		SetRotation(Owner()->Rotation());
	}
}

void UActor::PhysLanded(UActor* hitActor, const vec3& hitNormal)
{
	// landed on the floor
	CallEvent(this, EventName::Landed, { ExpressionValue::VectorValue(hitNormal) });

	if (Physics() == PHYS_Falling) // Landed event might have changed the physics mode
	{
		if (UObject::TryCast<UPawn>(this))
		{
			SetPhysics(PHYS_Walking);
			SetBase(hitActor, true);
		}
		else
		{
			SetPhysics(PHYS_None);
			SetBase(hitActor, true);
			Velocity() = vec3(0.0f);
		}
	}
}

void UActor::SetPhysics(uint8_t newPhysics)
{
	Physics() = newPhysics;
}

void UActor::SetCollision(bool newColActors, bool newBlockActors, bool newBlockPlayers)
{
	XLevel()->Collision.RemoveFromCollision(this);
	bCollideActors() = newColActors;
	bBlockActors() = newBlockActors;
	bBlockPlayers() = newBlockPlayers;
	XLevel()->Collision.AddToCollision(this);
}

bool UActor::SetLocation(const vec3& newLocation)
{
	auto result = CheckLocation(newLocation, CollisionRadius(), CollisionHeight(), bCollideWorld() || bCollideWhenPlacing());
	if (!result.first)
		return false;

	XLevel()->Collision.RemoveFromCollision(this);
	XLevel()->Light.RemoveLight(this);
	Location() = result.second;
	XLevel()->Collision.AddToCollision(this);
	XLevel()->Light.AddLight(this);

	if (Level()->bBegunPlay())
	{
		// Send touch notifications for anything at the new location
		for (UActor* actor : XLevel()->Collision.CollidingActors(Location(), CollisionHeight(), CollisionRadius()))
		{
			if (actor != this && !actor->IsBasedOn(this) && !IsBasedOn(actor) && bCollideActors() && actor->bCollideActors())
			{
				Touch(actor);
			}
		}

		// Untouch everything we aren't overlapping anymore
		if (engine->LaunchInfo.IsUnrealTournament_469())
		{
			for (const auto actor : Touching_UT469())
			{
				if (actor && !IsOverlapping(actor))
					UnTouch(actor);
			}
		}
		else
		{
			for (const auto actor : Touching())
			{
				if (actor && !IsOverlapping(actor))
					UnTouch(actor);
			}
		}
	}

	return true;
}

bool UActor::SetRotation(const Rotator& newRotation)
{
	// To do: return false if there isn't room

	Rotator delta = newRotation - Rotation();
	Rotation() = newRotation;
	TurnBasedActors(delta);
	return true;
}

// carried items and actors on movers should rotate with the actor their based on
void UActor::TurnBasedActors(const Rotator& deltaRotation)
{
	if ((deltaRotation.Yaw & 0xffff) == 0)
		return;
	Coords yawRot = Coords::YawRotation(deltaRotation.YawRadians());
	vec3 baseLoc = Location();
	for (size_t i = 0; i < BasedActors.size(); )
	{
		UActor* basedActor = BasedActors[i];
		if (!basedActor) { i++; continue; }
		vec3 basedLoc = basedActor->Location();
		vec3 rotatedOffset = yawRot * (basedLoc - baseLoc);
		basedActor->TryMove((baseLoc + rotatedOffset) - basedLoc, false, false);
		basedActor->SetRotation(basedActor->Rotation() + deltaRotation);
		if (UPawn* pawn = UObject::TryCast<UPawn>(basedActor))
			pawn->ViewRotation().Yaw += deltaRotation.Yaw;
		// UnrealScript events triggered in TryMove can call methods such as SetBase or Destroy, so need to guard while iterating.
		if (i < BasedActors.size() && BasedActors[i] == basedActor)
			i++;
	}
}

bool UActor::SetCollisionSize(float newRadius, float newHeight)
{
	// To do: return false if there isn't room

	XLevel()->Collision.RemoveFromCollision(this);
	CollisionRadius() = newRadius;
	CollisionHeight() = newHeight;
	XLevel()->Collision.AddToCollision(this);
	return true;
}

UObject* UActor::Trace(vec3& hitLocation, vec3& hitNormal, const vec3& traceEnd, const vec3& traceStart, bool bTraceActors, const vec3& extent)
{
	TraceFlags flags;
	flags.movers = true;
	flags.world = true;
	if (bTraceActors)
	{
		flags.pawns = true;
		flags.others = true;
		flags.onlyProjectiles = true;
	}

	// hack?
	if (IsA("ChallengeHUD"))
	{
		flags.zoneChanges = true;
	}

	CollisionHit hit = XLevel()->Collision.TraceFirstHit(traceStart, traceEnd, this, extent, flags);
	hitNormal = hit.Normal;
	hitLocation = traceStart + (traceEnd - traceStart) * hit.Fraction;
	return hit.Actor;
}

UObject* UActor::Trace(vec3& hitLocation, vec3& hitNormal, const vec3& traceEnd, const vec3& traceStart, bool bTraceActors, const vec3& extent, bool bTraceBSP, uint8_t BSPTraceFlags)
{
	LogUnimplemented("Actor.Trace() [U227 - BSPTraceFlags parameter isn't implemented");
	TraceFlags flags;
	flags.movers = true;
	flags.world = bTraceBSP;
	if (bTraceActors)
	{
		flags.pawns = true;
		flags.others = true;
		flags.onlyProjectiles = true;
	}

	// hack?
	if (IsA("ChallengeHUD"))
	{
		flags.zoneChanges = true;
	}

	CollisionHit hit = XLevel()->Collision.TraceFirstHit(traceStart, traceEnd, this, extent, flags);
	hitNormal = hit.Normal;
	hitLocation = traceStart + (traceEnd - traceStart) * hit.Fraction;
	return hit.Actor;
}

bool UActor::FastTrace(const vec3& traceEnd, const vec3& traceStart)
{
	return !XLevel()->Collision.TraceAnyHit(traceStart, traceEnd, this, false, true, false);
}

bool UActor::TraceSurfHitInfo(vec3& Start, vec3& End, vec3* HitLocation, vec3* HitNormal, UTexture* HitTex, int* HitFlags)
{
	const TraceFlags flags = {
		.movers = true,
		.world = true
	};

	const auto hit = XLevel()->Collision.TraceFirstHit(Start, End, this, vec3(), flags);

	if (!hit.Node)
		return false;

	if (HitLocation)
		*HitLocation = Start + (End - Start) * hit.Fraction;

	if (HitNormal)
		*HitNormal = hit.Normal;

	if (HitTex)
		HitTex = XLevel()->Model->Surfaces[hit.Node->Surf].Material;

	if (HitFlags)
		*HitFlags = hit.Node->NodeFlags;

	return true;
}

bool UActor::TraceThisActor(vec3& TraceEnd, vec3 TraceStart, vec3* HitLocation, vec3* HitNormal, std::optional<vec3> Extent)
{
	TraceFlags flags {
		.pawns = true,
		.movers = true,
		.others = true,
		.world = true
	};

	const auto hit = XLevel()->Collision.TraceFirstHit(TraceStart, TraceEnd, this, Extent ? *Extent : vec3(), flags);

	if (!hit.Node && !hit.Actor)
		return false;

	if (HitLocation)
		*HitLocation = TraceStart + (TraceEnd - TraceStart) * hit.Fraction;

	if (HitNormal)
		*HitNormal = hit.Normal;

	return true;
}

bool UActor::IsBasedOn(UActor* other)
{
	for (UActor* cur = other; cur; cur = cur->ActorBase())
	{
		if (cur == this)
		{
			return true;
		}
	}
	return false;
}

bool UActor::IsOwnedBy(UActor* owner)
{
	for (UActor* cur = this; cur; cur = cur->Owner())
	{
		if (cur == owner)
		{
			return true;
		}
	}
	return false;
}

bool UActor::IsOverlapping(UActor* other)
{
	return XLevel()->Collision.IsOverlapping(this, other);
}

CollisionHit UActor::TryMove(const vec3& delta, bool dryRun, bool isOwnBaseBlocking)
{
	// Static and non-movable objects can't move
	if (bStatic() || !bMovable())
	{
		CollisionHit hit;
		hit.Fraction = 0.0f;
		return hit;
	}

	// Avoid moving if movement is too small as the physics code doesn't like very small numbers
	if (dot(delta, delta) < 0.00000001f)
		return {};

	// Analyze what we will hit if we move as requested and stop if it is the level or a blocking actor
	bool useBlockPlayers = UObject::TryCast<UPlayerPawn>(this) || UObject::TryCast<UProjectile>(this);
	CollisionHit blockingHit;
	CollisionHitList hits;
	if (!Brush())
	{
		hits = XLevel()->Collision.Trace(Location(), Location() + delta, CollisionHeight(), CollisionRadius(), bCollideActors(), bCollideWorld(), false);
		if (bCollideWorld() || bBlockActors() || bBlockPlayers())
		{
			for (auto& hit : hits)
			{
				if (hit.Actor)
				{
					bool isBlocking;
					if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(hit.Actor) || UObject::TryCast<UProjectile>(hit.Actor))
						isBlocking = hit.Actor->bBlockPlayers() && bBlockPlayers();
					else
						isBlocking = hit.Actor->bBlockActors() && bBlockActors();

					// We never hit ourselves or anything moving along with us
					if (isBlocking && (isOwnBaseBlocking || !hit.Actor->IsBasedOn(this)) && !IsBasedOn(hit.Actor))
					{
						blockingHit = hit;
						break;
					}
				}
				else
				{
					blockingHit = hit;
					break;
				}
			}
		}
	}

	if (dryRun)
		return blockingHit;

	vec3 actuallyMoved = delta * blockingHit.Fraction;
	vec3 OldLocation = Location();

	XLevel()->Collision.RemoveFromCollision(this);
	XLevel()->Light.RemoveLight(this);
	Location() += actuallyMoved;
	XLevel()->Collision.AddToCollision(this);
	XLevel()->Light.AddLight(this);

	for (size_t i = 0; i < BasedActors.size(); )
	{
		UActor* basedActor = BasedActors[i];
		basedActor->TryMove(actuallyMoved, false, false);
		// UnrealScript events triggered in TryMove can call methods such as SetBase or Destroy, so need to guard while iterating.
		if (i < BasedActors.size() && BasedActors[i] == basedActor)
			i++;
	}

	// Notify actor of encroachment
	if (Brush() && (bBlockPlayers() || bBlockActors() || bCollideActors()))
	{
		Array<UActor*> encroachingActors = XLevel()->Collision.EncroachingActors(this);
		for (UActor* actor : encroachingActors)
		{
			if (actor == this || actor->Brush())
				continue;

			bool isBlocking;
			if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(actor) || UObject::TryCast<UProjectile>(actor))
				isBlocking = actor->bBlockPlayers() && bBlockPlayers();
			else
				isBlocking = actor->bBlockActors() && bBlockActors();

			if (isBlocking)
			{
				bool stopMovement = CallEvent(this, EventName::EncroachingOn, { ExpressionValue::ObjectValue(actor) }).ToBool();
				if (stopMovement)
				{
					XLevel()->Collision.RemoveFromCollision(this);
					XLevel()->Light.RemoveLight(this);
					Location() = OldLocation;
					XLevel()->Collision.AddToCollision(this);
					XLevel()->Light.AddLight(this);

					CollisionHit hit;
					hit.Fraction = 0.0f;
					return hit;
				}
			}
		}

		for (UActor* actor : encroachingActors)
		{
			if (actor == this)
				continue;

			bool isBlocking;
			if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(actor) || UObject::TryCast<UProjectile>(actor))
				isBlocking = actor->bBlockPlayers() && bBlockPlayers();
			else
				isBlocking = actor->bBlockActors() && bBlockActors();

			if (isBlocking)
				CallEvent(actor, EventName::EncroachedBy, { ExpressionValue::ObjectValue(this) }).ToBool();
		}
	}

	// Send bump notification if we hit an actor
	if (blockingHit.Actor)
	{
		if (!blockingHit.Actor->IsBasedOn(this))
		{
			CallEvent(blockingHit.Actor, EventName::Bump, { ExpressionValue::ObjectValue(this) });
			CallEvent(this, EventName::Bump, { ExpressionValue::ObjectValue(blockingHit.Actor) });
		}
	}

	// Send touch notifications for anything we crossed while moving
	for (auto& hit : hits)
	{
		if (hit.Fraction >= blockingHit.Fraction)
			break;

		if (hit.Actor && !hit.Actor->IsBasedOn(this) && !IsBasedOn(hit.Actor) && bCollideActors() && hit.Actor->bCollideActors())
		{
			// We can't touch stuff we are blocked by
			bool isBlocking;
			if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(hit.Actor) || UObject::TryCast<UProjectile>(hit.Actor))
				isBlocking = hit.Actor->bBlockPlayers() && bBlockPlayers();
			else
				isBlocking = hit.Actor->bBlockActors() && bBlockActors();
			if (!isBlocking)
				Touch(hit.Actor);
		}
	}

	// Untouch everything we aren't overlapping anymore
	if (engine->LaunchInfo.IsUnrealTournament_469())
	{
		for (const auto actor : Touching_UT469())
			if (actor && !IsOverlapping(actor))
				UnTouch(actor);
	}
	else
	{
		for (const auto actor : Touching())
			if (actor && !IsOverlapping(actor))
				UnTouch(actor);
	}

	UpdateActorZone();

	return blockingHit;
}

CollisionHit UActor::TryMoveSmooth(const vec3& delta)
{
	CollisionHit hit = TryMove(delta);
	if (hit.Fraction != 1.0f)
	{
		// We hit a slope. Try to follow it.
		vec3 alignedDelta = (delta - hit.Normal * dot(delta, hit.Normal)) * (1.0f - hit.Fraction);
		if (dot(delta, alignedDelta) >= 0.0f) // Don't end up going backwards
		{
			CollisionHit hit2 = TryMove(alignedDelta);
			return hit2; // XXX: does this break anything?
		}
	}

	return hit;
}

void UActor::Touch(UActor* actor)
{
	// Don't setup touch if any object has been destroyed
	if (bDeleteMe() || actor->bDeleteMe())
		return;

	// Retail UT436 clears a full Touching array before binding the next contact.
	// This matters for clustered respawning pickups: sleeping pickups may retain
	// their overlap, but they must not permanently prevent the next active pickup
	// from receiving Touch. Keep the cleanup symmetric through UnTouch.
	auto clearTouching = [this](UActor* owner)
	{
		Array<UActor*> contacts;
		if (engine->LaunchInfo.IsUnrealTournament_469())
		{
			for (UActor* contact : owner->Touching_UT469())
				if (contact)
					contacts.push_back(contact);
		}
		else
		{
			for (UActor* contact : owner->Touching())
				if (contact)
					contacts.push_back(contact);
		}
		for (UActor* contact : contacts)
			owner->UnTouch(contact);
	};

	if (engine->LaunchInfo.IsUnrealTournament_469())
	{
		auto TouchingArray = Touching_UT469();
		auto TouchingArray2 = actor->Touching_UT469();

		// Do nothing if actors are already touching
		for (int i = 0; i < TouchingArray.size(); i++)
		{
			if (TouchingArray[i] == actor)
				return;
		}

		// Only setup touch if we have room in both arrays
		int slot1 = -1, slot2 = -1;
		for (int i = 0; i < TouchingArray.size(); i++)
		{
			if (slot1 == -1 && TouchingArray[i] == nullptr)
				slot1 = i;
		}
		for (int i = 0; i < TouchingArray2.size(); i++)
		{
			if (slot2 == -1 && TouchingArray2[i] == nullptr)
				slot2 = i;
		}
		if (slot1 == -1)
			clearTouching(this);
		if (slot2 == -1)
			clearTouching(actor);
		if (slot1 == -1 || slot2 == -1)
		{
			TouchingArray = Touching_UT469();
			TouchingArray2 = actor->Touching_UT469();
			slot1 = -1;
			slot2 = -1;
			for (int i = 0; i < TouchingArray.size(); i++)
				if (slot1 == -1 && TouchingArray[i] == nullptr)
					slot1 = i;
			for (int i = 0; i < TouchingArray2.size(); i++)
				if (slot2 == -1 && TouchingArray2[i] == nullptr)
					slot2 = i;
			if (slot1 == -1 || slot2 == -1 || bDeleteMe() || actor->bDeleteMe())
				return;
		}

		// Setup links first so Destroy or recursive Touch calls always finds the touch binding
		TouchingArray[slot1] = actor;
		TouchEventsSent.insert(actor);
		TouchingArray2[slot2] = this;
		actor->TouchEventsSent.erase(this);

		// Notify unrealscript for first actor
		CallEvent(this, EventName::Touch, { ExpressionValue::ObjectValue(actor) });

		// Notify unrealscript for second actor
		if (!actor->bDeleteMe())
		{
			for (int i = 0; i < TouchingArray2.size(); i++)
			{
				if (TouchingArray2[i] == this && !actor->TouchEventsSent.contains(this))
				{
					actor->TouchEventsSent.insert(this);
					CallEvent(actor, EventName::Touch, { ExpressionValue::ObjectValue(this) });
					break;
				}
			}
		}
	}
	else
	{
		auto TouchingArray = Touching();
		auto TouchingArray2 = actor->Touching();

		// Do nothing if actors are already touching
		for (int i = 0; i < TouchingArraySize; i++)
		{
			if (TouchingArray[i] == actor)
				return;
		}

		// Only setup touch if we have room in both arrays
		int slot1 = -1, slot2 = -1;
		for (int i = 0; i < TouchingArraySize; i++)
		{
			if (slot1 == -1 && TouchingArray[i] == nullptr)
				slot1 = i;
			if (slot2 == -1 && TouchingArray2[i] == nullptr)
				slot2 = i;
		}
		if (slot1 == -1)
			clearTouching(this);
		if (slot2 == -1)
			clearTouching(actor);
		if (slot1 == -1 || slot2 == -1)
		{
			TouchingArray = Touching();
			TouchingArray2 = actor->Touching();
			slot1 = -1;
			slot2 = -1;
			for (int i = 0; i < TouchingArraySize; i++)
			{
				if (slot1 == -1 && TouchingArray[i] == nullptr)
					slot1 = i;
				if (slot2 == -1 && TouchingArray2[i] == nullptr)
					slot2 = i;
			}
			if (slot1 == -1 || slot2 == -1 || bDeleteMe() || actor->bDeleteMe())
				return;
		}

		// Setup links first so Destroy or recursive Touch calls always finds the touch binding
		TouchingArray[slot1] = actor;
		TouchEventsSent.insert(actor);
		TouchingArray2[slot2] = this;
		actor->TouchEventsSent.erase(this);

		// Notify unrealscript for first actor
		CallEvent(this, EventName::Touch, { ExpressionValue::ObjectValue(actor) });

		// Notify unrealscript for second actor
		if (!actor->bDeleteMe())
		{
			for (int i = 0; i < TouchingArraySize; i++)
			{
				if (TouchingArray2[i] == this && !actor->TouchEventsSent.contains(this))
				{
					actor->TouchEventsSent.insert(this);
					CallEvent(actor, EventName::Touch, { ExpressionValue::ObjectValue(this) });
					break;
				}
			}
		}
	}
}

void UActor::UnTouch(UActor* actor)
{
	if (engine->LaunchInfo.IsUnrealTournament_469())
	{
		auto TouchingArray = Touching_UT469();
		auto TouchingArray2 = actor->Touching_UT469();
		if (!bDeleteMe())
		{
			for (int i = 0; i < TouchingArray.size(); i++)
			{
				if (TouchingArray[i] == actor)
				{
					TouchingArray[i] = nullptr;
					if (TouchEventsSent.erase(actor) != 0)
						CallEvent(this, EventName::UnTouch, { ExpressionValue::ObjectValue(actor) });
				}
			}
		}
		if (!actor->bDeleteMe())
		{
			for (int i = 0; i < TouchingArray2.size(); i++)
			{
				if (TouchingArray2[i] == this)
				{
					TouchingArray2[i] = nullptr;
					if (actor->TouchEventsSent.erase(this) != 0)
						CallEvent(actor, EventName::UnTouch, { ExpressionValue::ObjectValue(this) });
				}
			}
		}
	}
	else
	{
		auto TouchingArray = Touching();
		auto TouchingArray2 = actor->Touching();
		if (!bDeleteMe())
		{
			for (int i = 0; i < TouchingArraySize; i++)
			{
				if (TouchingArray[i] == actor)
				{
					TouchingArray[i] = nullptr;
					if (TouchEventsSent.erase(actor) != 0)
						CallEvent(this, EventName::UnTouch, { ExpressionValue::ObjectValue(actor) });
				}
			}
		}
		if (!actor->bDeleteMe())
		{
			for (int i = 0; i < TouchingArraySize; i++)
			{
				if (TouchingArray2[i] == this)
				{
					TouchingArray2[i] = nullptr;
					if (actor->TouchEventsSent.erase(this) != 0)
						CallEvent(actor, EventName::UnTouch, { ExpressionValue::ObjectValue(this) });
				}
			}
		}
	}
}

bool UActor::Move(const vec3& delta)
{
	return TryMove(delta).Fraction == 1.0f;
}

bool UActor::MoveSmooth(const vec3& delta)
{
	CollisionHit hit = TryMoveSmooth(delta);
	return hit.Fraction != 1.0f;
}

bool UActor::HasAnim(const NameString& sequence)
{
	return Mesh() && Mesh()->GetSequence(sequence);
}

bool UActor::IsAnimating()
{
	return AnimRate() != 0.0f;
}

void UActor::FinishAnim()
{
	if (bAnimLoop())
	{
		bAnimLoop() = false;
		bAnimFinished() = false;
	}

	if (StateFrame)
		StateFrame->LatentState = LatentRunState::FinishAnim;
}

NameString UActor::GetAnimGroup(const NameString& sequence)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
			return seq->Group;
	}
	return {};
}

// UnrealScript variables controlling animation:
// 
// Tweening means animating (using vertex interpolation) from the last animation's frame to the current animation's first frame
//
// Mesh          - the mesh the animation belongs to
// AnimSequence  - current active animation sequence
// AnimFrame     - how far we've gotten in an animation 0.0 to 1.0 for current animation, negative for interpolation from old animation when tweening
// AnimLast      - end point for AnimFrame (when to stop/loop). It is zero when only tweening (don't play the animation). It is the start of the last frame (1-1/numframes) when playing an animation
// AnimRate      - how far AnimFrame moves in 1 second (AnimFrame += AnimRate * timeElapsed). If negative it is a scale factor used to convert Velocity length to animation speed
// AnimMinRate   - the minimum animation speed when AnimRate is negative (negative AnimRate means it should use length(Velocity) * abs(AnimRate) as the anim speed)
// TweenRate     - how fast to move when AnimFrame is negative (AnimFrame += TweenRate * timeElapsed)
// OldAnimRate   - AnimRate from previous call to PlayAnim/LoopAnim/TweenAnim
// bAnimLoop     - true if the animation should loop when AnimLast is reached
// bAnimNotify   - true if animation notify events should be fired when animating
// bAnimFinished - true if AnimLast was reached and there's no looping

void UActor::PlayAnim(const NameString& sequence, float rate, float tweenTime)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
		{
			SetTweenFromAnimFrame();

			AnimSequence() = sequence;

			if (seq->NumFrames > 1)
			{
				AnimFrame() = tweenTime > 0.0f ? -1.0f / seq->NumFrames : 0.0f;
				AnimLast() = 1.0f - 1.0f / seq->NumFrames;
				AnimRate() = rate * seq->Rate / seq->NumFrames;
				TweenRate() = tweenTime > 0.0f ? 1.0f / (tweenTime * seq->NumFrames) : 0.0f;
				bAnimNotify() = !seq->Notifys.empty();
				OldAnimRate() = AnimRate();
			}
			else
			{
				// Special case for 1 frame animations. Simply keep drawing the animation for 0.1 second (or tween duration, if tweening).

				AnimFrame() = -1.0f;
				AnimLast() = 0.0f;
				AnimRate() = 0.0f;
				TweenRate() = tweenTime > 0.0f ? 1.0f / tweenTime : 10.0f;
				bAnimNotify() = false;
				OldAnimRate() = 0.0f;
				AnimMinRate() = 0.0f;
			}

			bAnimLoop() = false;
			bAnimFinished() = false;
		}
	}
}

void UActor::PlayBlendAnim(const NameString& sequenceName, float rate, float tweenTime, int blendSlot)
{
	LogUnimplemented("Actor.PlayBlendAnim");
	if (blendSlot < 0 || blendSlot > 3)
	{
		LogMessage("Invalid channel for PlayBlendAnim!");
		return;
	}
	if (!Mesh())
	{
		LogMessage("No mesh for PlayBlendAnim");
		return;
	}

	MeshAnimSeq* sequence = Mesh()->GetSequence(sequenceName);
	if (!sequence)
	{
		LogMessage("Sequence not found for PlayBlendAnim");
		return;
	}

	int numFrames = sequence->NumFrames;
	float sequenceRate = sequence->Rate;

	if (BlendAnimSequence()[blendSlot].IsNone())
	{
		tweenTime = 0.0f;
	}

	BlendAnimSequence()[blendSlot] = sequenceName;

	BlendAnimFrame()[blendSlot] = -1.0f / numFrames;

	BlendAnimMinRate()[blendSlot] = (rate * sequenceRate) / numFrames;

	BlendAnimLast()[blendSlot] = 1.0f - (1.0f / numFrames);

	if (BlendAnimLast()[blendSlot] == 0.0f)
	{
		BlendAnimRate()[blendSlot] = 0.0f;
		BlendAnimFrame()[blendSlot] = 0.0f;

		BlendTweenRate()[blendSlot] = (tweenTime <= 0.0f) ? 10.0f : (1.0f / tweenTime);
	}
	else if (tweenTime <= 0.0f)
	{
		if (tweenTime == -1.0f)
		{
			if (BlendAnimMinRate()[blendSlot] <= 0.0f)
			{
				if (BlendAnimMinRate()[blendSlot] == 0.0f)
				{
					BlendTweenRate()[blendSlot] = 1.0f / (numFrames * 0.025f);
				}
				else 
				{
					float speed = length(Velocity());
					float computed = speed * (-BlendAnimMinRate()[blendSlot]);
					float minVal = BlendAnimRate()[blendSlot] * 0.5f;

					BlendTweenRate()[blendSlot] = std::max(computed, minVal);
				}
			}
			else 
			{
				BlendTweenRate()[blendSlot] = BlendAnimMinRate()[blendSlot];
			}
		}
		else 
		{
			BlendTweenRate()[blendSlot] = 0.0f;
			BlendAnimFrame()[blendSlot] = 0.001f;
		}
	}
	else 
	{
		BlendTweenRate()[blendSlot] = 1.0f / (numFrames * tweenTime);
	}
	
	float oldX = SimBlendAnim()[blendSlot].x;
	float oldY = SimBlendAnim()[blendSlot].y;
	float oldZ = SimBlendAnim()[blendSlot].z;
	float oldW = SimBlendAnim()[blendSlot].w;

	SimBlendAnim()[blendSlot].z = BlendAnimFrame()[blendSlot] * 10000.0f;
	SimBlendAnim()[blendSlot].w = BlendAnimRate()[blendSlot] * 10000.0f;
	SimBlendAnim()[blendSlot].x = BlendTweenRate()[blendSlot] * 1000.0f;
	SimBlendAnim()[blendSlot].y = BlendAnimLast()[blendSlot] * 10000.0f;

	if (oldZ == SimBlendAnim()[blendSlot].z && oldW == SimBlendAnim()[blendSlot].w && oldX == SimBlendAnim()[blendSlot].x && oldY == SimBlendAnim()[blendSlot].y)
	{
		SimBlendAnim()[blendSlot].y += 1.0f;
	}

	OldBlendAnimRate()[blendSlot] = BlendAnimRate()[blendSlot];
	BlendAnimMinRate()[blendSlot] = BlendAnimRate()[blendSlot];
}


void UActor::LoopAnim(const NameString& sequence, float rate, float tweenTime, float minRate)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
		{
			if (AnimSequence() == sequence && IsAnimating() && bAnimLoop())
			{
				if (seq->NumFrames > 1)
				{
					AnimRate() = rate * seq->Rate / seq->NumFrames;
					AnimMinRate() = minRate * seq->Rate / seq->NumFrames;
					TweenRate() = tweenTime > 0.0f ? 1.0f / (tweenTime * seq->NumFrames) : 0.0f;
					OldAnimRate() = AnimRate();
				}
			}
			else
			{
				SetTweenFromAnimFrame();

				AnimSequence() = sequence;
				if (seq->NumFrames > 1)
				{
					AnimFrame() = tweenTime > 0.0f ? -1.0f / seq->NumFrames : 0.0f;
					AnimLast() = 1.0f - 1.0f / seq->NumFrames;
					bAnimNotify() = !seq->Notifys.empty();
					AnimRate() = rate * seq->Rate / seq->NumFrames;
					AnimMinRate() = minRate * seq->Rate / seq->NumFrames;
					TweenRate() = tweenTime > 0.0f ? 1.0f / (tweenTime * seq->NumFrames) : 0.0f;
					OldAnimRate() = AnimRate();
				}
				else
				{
					// Special case for 1 frame animations. Simply keep drawing the animation for 0.1 second (or tween duration, if tweening).

					AnimFrame() = -1.0f;
					AnimLast() = 0.0f;
					AnimRate() = 0.0f;
					TweenRate() = tweenTime > 0.0f ? 1.0f / tweenTime : 10.0f;
					bAnimNotify() = false;
					OldAnimRate() = 0.0f;
					AnimMinRate() = 0.0f;
				}
				bAnimFinished() = false;
				bAnimLoop() = true;
			}
		}
	}
}

void UActor::TweenAnim(const NameString& sequence, float tweenTime)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
		{
			SetTweenFromAnimFrame();

			AnimSequence() = sequence;
			AnimFrame() = tweenTime > 0.0f ? -1.0f / seq->NumFrames : 0.0f;
			AnimLast() = 0.0f;
			AnimRate() = 0.0f;
			AnimMinRate() = 0.0f;
			TweenRate() = tweenTime > 0.0f ? 1.0f / (tweenTime * seq->NumFrames) : 0.0f;
			OldAnimRate() = AnimRate();
			bAnimNotify() = false;
			bAnimFinished() = false;
			bAnimLoop() = false;
		}
	}
}

void UActor::TickAnimation(float elapsed)
{
	if (StateFrame && StateFrame->LatentState == LatentRunState::FinishAnim)
	{
		if (!IsAnimating() || AnimFrame() >= AnimLast())
			StateFrame->LatentState = LatentRunState::Continue;
	}

	for (int i = 0; elapsed > 0.0f && i < 10; i++)
	{
		// If AnimFrame is positive we are doing a normal animation. If it is negative we are doing a tween animation.
		float fromAnimTime = AnimFrame();
		if (fromAnimTime >= 0.0f)
		{
			// If AnimRate is positive we are animating at a fixed rate. If it is negative we animate based on velocity (using AnimRate as a speed scale factor)
			float animRate = (AnimRate() >= 0) ? AnimRate() : std::max(AnimMinRate(), -AnimRate() * length(Velocity()));
			if (animRate == 0.0f)
				break;

			// Find what time will we be at the end of the animation
			float toAnimTime = fromAnimTime + animRate * elapsed;

			// Stop at the next notify event, if any
			if (Mesh() && bAnimNotify())
			{
				MeshAnimSeq* seq = Mesh()->GetSequence(AnimSequence());
				if (seq)
				{
					bool foundEvent = false;
					for (const MeshAnimNotify& n : seq->Notifys)
					{
						if (n.Time > fromAnimTime && n.Time <= toAnimTime)
						{
							if (FindEventFunction(this, n.Function))
							{
								toAnimTime = n.Time;
								elapsed -= (toAnimTime - fromAnimTime) / animRate;
								AnimFrame() = toAnimTime;
								foundEvent = true;
								CallEvent(this, n.Function);
								break;
							}
						}
					}
					if (foundEvent)
						continue;
				}
			}

			// Looped animations got their AnimEnd notify event at the AnimLast point, NOT when the loop finishes!
			if (bAnimLoop() && AnimLast() > fromAnimTime && AnimLast() <= toAnimTime)
			{
				toAnimTime = AnimLast();
				elapsed -= (toAnimTime - fromAnimTime) / animRate;
				AnimFrame() = toAnimTime;

				if (StateFrame && StateFrame->LatentState == LatentRunState::FinishAnim)
					StateFrame->LatentState = LatentRunState::Continue;

				CallEvent(this, EventName::AnimEnd);
				continue;
			}

			// Clamp elapsed time to the animation end. This differs for looping animations as they also have to take the last frame into account before looping.
			float animEndTime = bAnimLoop() ? 1.0f : AnimLast();
			if (toAnimTime < fromAnimTime) // This can happen if FinishAnim is called after a looping animation made it past the AnimLast point
			{
				toAnimTime = fromAnimTime;
				animEndTime = fromAnimTime;
				elapsed = 0.0f;
			}
			else if (toAnimTime >= animEndTime)
			{
				elapsed -= (animEndTime - fromAnimTime) / animRate;
				toAnimTime = animEndTime;
			}
			else
			{
				elapsed = 0.0f;
			}

			AnimFrame() = toAnimTime;

			if (toAnimTime == animEndTime)
			{
				if (bAnimLoop())
				{
					AnimFrame() = 0.0f;
				}
				else
				{
					AnimRate() = 0.0f;
					bAnimFinished() = true;
				}
			}

			if (!bAnimLoop() && fromAnimTime < animEndTime && toAnimTime >= animEndTime)
			{
				if (StateFrame && StateFrame->LatentState == LatentRunState::FinishAnim)
					StateFrame->LatentState = LatentRunState::Continue;

				CallEvent(this, EventName::AnimEnd);
			}
		}
		else
		{
			float tweenRate = TweenRate();
			if (tweenRate == 0.0f)
				break;

			float toAnimTime = fromAnimTime + tweenRate * elapsed;

			float animEndTime = 0.0f;
			if (toAnimTime >= animEndTime)
			{
				elapsed -= (animEndTime - fromAnimTime) / tweenRate;
				toAnimTime = animEndTime;
			}
			else
			{
				elapsed = 0.0f;
			}

			AnimFrame() = toAnimTime;

			if (toAnimTime == animEndTime && AnimRate() == 0.0f)
			{
				if (StateFrame && StateFrame->LatentState == LatentRunState::FinishAnim)
					StateFrame->LatentState = LatentRunState::Continue;

				bAnimFinished() = true;
				//engine->LogMessage("CallEvent(AnimEnd) for " + Class->FriendlyName.ToString() + "");
				CallEvent(this, EventName::AnimEnd);
			}
		}
	}
}

void UActor::TickBlendAnimation(float elapsed)
{
	for (int i = 0; elapsed > 0.0f && i < 4; i++)
	{
		if (BlendAnimSequence()[i].IsNone())
			continue;

		if (BlendAnimFrame()[i] >= BlendAnimLast()[i])
			continue;

		float oldFrame = BlendAnimFrame()[i];

		if (BlendAnimFrame()[i] < 0.0f)
		{
			BlendAnimFrame()[i] += elapsed * BlendTweenRate()[i];

			if (BlendAnimFrame()[i] < 0.0f)
				continue;

			BlendAnimFrame()[i] = 0.0f;

			elapsed = (BlendAnimFrame()[i] * elapsed) / (BlendAnimFrame()[i] - oldFrame);
			continue;
		}

		if (BlendAnimRate()[i] < 0.0f)
		{
			float speed = length(Velocity());

			float adjustedRate = -speed * BlendAnimRate()[i];

			float minRate = BlendAnimLast()[i];
			if (adjustedRate > minRate)
				adjustedRate = minRate;

			BlendAnimFrame()[i] += adjustedRate * elapsed;
		}
		else
		{
			BlendAnimFrame()[i] += BlendAnimRate()[i] * elapsed;
		}

		if (BlendAnimFrame()[i] >= BlendAnimLast()[i])
		{
			float endFrame = BlendAnimLast()[i];

			BlendAnimFrame()[i] = endFrame;
			BlendAnimRate()[i] = 0.0f;

			elapsed = ((BlendAnimFrame()[i] - endFrame) * elapsed) / (BlendAnimFrame()[i] - oldFrame);

			if (RemoteRole() < ENetRole::ROLE_SimulatedProxy)
			{
				SimBlendAnim()[i].z = BlendAnimFrame()[i] * 10000.0f;

				float rate = BlendAnimRate()[i] * 5000.0f;
				if (rate > 32767.0f)
					rate = 32767.0f;

				SimBlendAnim()[i].w = rate;
			}
		}
	}
}

void UActor::SetTweenFromAnimFrame()
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(AnimSequence());
		if (seq)
		{
			float animFrame = std::max(AnimFrame(), 0.0f) * seq->NumFrames;
			int frame0 = (int)animFrame;
			int frame1 = frame0 + 1;
			frame0 = frame0 % seq->NumFrames;
			frame1 = frame1 % seq->NumFrames;
			TweenFromAnimFrame.V0 = (seq->StartFrame + frame0) * Mesh()->FrameVerts;
			TweenFromAnimFrame.V1 = (seq->StartFrame + frame1) * Mesh()->FrameVerts;
			TweenFromAnimFrame.T = animFrame - (float)frame0;
		}
		else // For safety. Should never happen.
		{
			TweenFromAnimFrame.V0 = 0;
			TweenFromAnimFrame.V1 = 0;
			TweenFromAnimFrame.T = -1.0f;
		}
	}
}

void UActor::MakeNoise(float loudness)
{
	UPawn* noisePawn = UObject::Cast<UPawn>(Instigator());

	if (!noisePawn || Level()->NetMode() == NM_Client)
		return;

	float currentTime = Level()->TimeSeconds();
	vec3 delta1 = noisePawn->noise1spot() - Location();
	vec3 delta2 = noisePawn->noise2spot() - Location();
	if ((noisePawn->noise1time() > currentTime - 0.2f && dot(delta1, delta1) < 2500.0f && noisePawn->noise1loudness() >= 0.9f * loudness) ||
		(noisePawn->noise2time() > currentTime - 0.2f && dot(delta2, delta2) < 2500.0f && noisePawn->noise2loudness() >= 0.9f * loudness))
	{
		return;
	}

	if (noisePawn->noise1time() < currentTime - 0.18f)
	{
		noisePawn->noise1time() = currentTime;
		noisePawn->noise1spot() = Location();
		noisePawn->noise1loudness() = loudness;
	}
	else if (noisePawn->noise2time() < currentTime - 0.18f)
	{
		noisePawn->noise2time() = currentTime;
		noisePawn->noise2spot() = Location();
		noisePawn->noise2loudness() = loudness;
	}
	else if (dot(delta1, delta1) < 2500.0f)
	{
		noisePawn->noise1time() = currentTime;
		noisePawn->noise1spot() = Location();
		noisePawn->noise1loudness() = loudness;
	}
	else if (noisePawn->noise2loudness() <= loudness)
	{
		noisePawn->noise2time() = currentTime;
		noisePawn->noise2spot() = Location();
		noisePawn->noise2loudness() = loudness;
	}

	for (UPawn* pawn = Level()->PawnList(); pawn != nullptr; pawn = pawn->nextPawn())
	{
		if (pawn != noisePawn && pawn->CanHearNoise(this, loudness))
		{
			CallEvent(pawn, EventName::HearNoise, { ExpressionValue::FloatValue(loudness), ExpressionValue::ObjectValue(this) });
		}
	}
}

bool UActor::PlayerCanSeeMe()
{
	for (UPawn* pawn = Level()->PawnList(); pawn != nullptr; pawn = pawn->nextPawn())
	{
		if (pawn == this)
			continue;

		vec3 L = Location() - pawn->Location();
		float dist2 = dot(L, L);

		// Too far away
		if (dist2 > 500 * 500)
			continue;

		// Without behind view the pawn can only see in a 75 degree cone in front of them
		if (!pawn->bBehindView())
		{
			vec3 viewDirection = Coords::Rotation(pawn->ViewRotation()).XAxis;
			if (dot(viewDirection, L) < 0.2588190451f * dist2)
				continue;
		}

		// Try check for line of sight
		vec3 eyePos = pawn->Location();
		eyePos.z += pawn->BaseEyeHeight();
		if (pawn->FastTrace(Location(), eyePos))
			return true;
	}
	return false;
}

void UActor::UpdateBspInfo()
{
	// Figure out where the actor is visually located in the world
	BBox bbox;
	EDrawType dt = (EDrawType)DrawType();
	if (dt == DT_Mesh && Mesh())
	{
		UMesh* mesh = Mesh();
		Coords rotation = Coords::Rotation(Rotation());
		mat4 objectToWorld = mat4::translate(Location() + PrePivot()) * Coords::Rotation(Rotation()).ToMatrix() * mat4::scale(DrawScale());
		mat4 meshToWorld = objectToWorld * mesh->meshToObject;
		bbox = mesh->BoundingBox.transform(meshToWorld);
	}
	else if ((dt == DT_Sprite || dt == DT_SpriteAnimOnce) && (Texture()))
	{
		vec3 location = Location();
		auto texWidth = Texture()->UsedMipmaps[0].Width;
		auto texHeight = Texture()->UsedMipmaps[0].Height;
		// vec3 extents = vec3(100.0f); // To do: this is wrong. We need the size of a sprite
		vec3 extents = vec3(std::max(texWidth, texHeight) * 0.5f * DrawScale());
		bbox.min = location - extents;
		bbox.max = location + extents;
	}
	else if (dt == DT_Brush && Brush())
	{
		UModel* brush = Brush();
		if (UMover* mover = UObject::TryCast<UMover>(this))
		{
			mat4 objectToWorld = mat4::translate(Location()) * Coords::Rotation(Rotation()).ToMatrix() * mat4::scale(mover->MainScale().Scale) * mat4::translate(-PrePivot());
			bbox = brush->BoundingBox.transform(objectToWorld);
		}
		else
		{
			bbox.min = vec3(0.0f);
			bbox.max = vec3(0.0f);
		}
	}
	else
	{
		bbox.min = vec3(0.0f);
		bbox.max = vec3(0.0f);
	}

	// Is actor still in the bsp tree at the correct location?
	if (!BspInfo.Node || BspInfo.BoundingBox != bbox)
	{
		RemoveFromBspNode();

		BspInfo.BoundingBox = bbox;

		vec3 location = bbox.center();
		vec3 extents = bbox.extents();

		ULevel* level = XLevel();
		BspNode* node = level ? &level->Model->Nodes[0] : nullptr;
		while (node)
		{
			int side = NodeAABBOverlap(location, extents, node);
			if (side == 0 || (side < 0 && node->Front < 0) || (side > 0 && node->Back < 0))
			{
				AddToBspNode(node);
				break;
			}
			else if (side < 0)
			{
				node = &level->Model->Nodes[node->Front];
			}
			else
			{
				node = &level->Model->Nodes[node->Back];
			}
		}
	}
}

void UActor::AddToBspNode(BspNode* node)
{
	BspInfo.Node = node;

	if (node->ActorList)
	{
		node->ActorList->BspInfo.Prev = this;
		BspInfo.Next = node->ActorList;
	}

	node->ActorList = this;
}

void UActor::RemoveFromBspNode()
{
	if (BspInfo.Node)
	{
		if (BspInfo.Next)
		{
			BspInfo.Next->BspInfo.Prev = BspInfo.Prev;
		}
		if (BspInfo.Prev)
		{
			BspInfo.Prev->BspInfo.Next = BspInfo.Next;
		}
		if (BspInfo.Node->ActorList == this)
		{
			BspInfo.Node->ActorList = BspInfo.Next;
		}
		BspInfo.Node = nullptr;
		BspInfo.Prev = nullptr;
		BspInfo.Next = nullptr;
	}
}

// -1 = inside, 0 = intersects, 1 = outside
int UActor::NodeAABBOverlap(const vec3& center, const vec3& extents, BspNode* node)
{
	float e = extents.x * std::abs(node->PlaneX) + extents.y * std::abs(node->PlaneY) + extents.z * std::abs(node->PlaneZ);
	float s = center.x * node->PlaneX + center.y * node->PlaneY + center.z * node->PlaneZ - node->PlaneW;
	if (s - e > 0.0f)
		return -1;
	else if (s + e < 0.0f)
		return 1;
	else
		return 0;
}

UTexture* UActor::GetMultiskin(int index)
{
	if (engine->LaunchInfo.ue1Version > 219 && index >= 0 && index < 8)
		return MultiSkins()[index];
	else
		return nullptr;
}

void UActor::DeusExConBindEvents()
{
	auto mission = UObject::Cast<UConversationList>(engine->GetDeusExMission());
	if (!mission)
		return;

	UClass* clsConListItem = engine->packages->FindClass("ConSys.ConListItem");
	UConListItem* conListItem = nullptr;

	NameString bindName = BindName();
	if (!bindName.IsNone())
	{
		for (UConItem* item = mission->conversations(); item; item = item->Next())
		{
			auto conversation = UObject::Cast<UConversation>(item->ConObject());
			NameString conOwnerName = conversation->conOwnerName();
			if (conOwnerName == bindName)
			{
				NameString name;
				UConListItem* newItem = UObject::Cast<UConListItem>(engine->LevelPackage->NewObject(name, clsConListItem, ObjectFlags::Transient, true));
				newItem->con() = conversation;
				newItem->Next() = conListItem;
				conListItem = newItem;
			}
		}
	}

	NameString barkBindName = BarkBindName();
	if (!barkBindName.IsNone())
	{
		for (UConItem* item = mission->conversations(); item; item = item->Next())
		{
			auto conversation = UObject::Cast<UConversation>(item->ConObject());
			NameString conOwnerName = conversation->conOwnerName();
			if (conOwnerName == barkBindName)
			{
				NameString name;
				UConListItem* newItem = UObject::Cast<UConListItem>(engine->LevelPackage->NewObject(name, clsConListItem, ObjectFlags::Transient, true));
				newItem->con() = conversation;
				newItem->Next() = conListItem;
				conListItem = newItem;
			}
		}
	}

	ConListItems() = conListItem;
}

/////////////////////////////////////////////////////////////////////////////

bool UPawn::ActorReachable(UActor* anActor, bool checkNavpoint)
{
	if (!anActor)
		return false;

	UPawn* aPawn = UObject::TryCast<UPawn>(anActor);
	int allowedReachFlags = 0;
	if (bCanWalk()) allowedReachFlags |= R_WALK;
	if (bCanFly()) allowedReachFlags |= R_FLY;
	if (bCanSwim()) allowedReachFlags |= R_SWIM;
	if (bCanJump()) allowedReachFlags |= R_JUMP;
	if (bCanOpenDoors()) allowedReachFlags |= R_DOOR;
	if (bCanDoSpecial()) allowedReachFlags |= R_SPECIAL;
	if (bIsPlayer()) allowedReachFlags |= R_PLAYERONLY;

	// If actor is not a pawn we assume we can't reach if they are too far away
	if (!aPawn)
	{
		vec3 delta = anActor->Location() - Location();
		float dist2 = dot(delta, delta);
		if (dist2 > 1000.0f * 1000.0f)
			return false;
	}

	// Navpoints may not be reachable at all according to reachspecs
	if (checkNavpoint)
	{
		// Check if we are trying to reach a navigation point.
		// They can also be hiding in an inventory as an inventory (pickup item) can be linked to a navigation point.
		UNavigationPoint* navPoint = UObject::TryCast<UNavigationPoint>(anActor);
		if (UInventory* inventory = UObject::TryCast<UInventory>(anActor))
			navPoint = inventory->myMarker();

		if (navPoint)
		{
			// Check if the navigation point is theoretically reachable at all according to reachspecs.
			bool couldBeReachable = false;
			float radius = CollisionRadius();
			float height = CollisionHeight();
			int rejectedCollision = 0;
			int rejectedPlayerOnly = 0;
			int rejectedCapability = 0;
			int rejectedPruned = 0;
			for (UNavigationPoint* cur = Level()->NavigationPointList(); cur != nullptr; cur = cur->nextNavigationPoint())
			{
				const auto& specs = XLevel()->ReachSpecs;
				for (int index : cur->Paths())
				{
					if (index < 0 || (size_t)index >= specs.size())
						break;
					const LevelReachSpec& reachSpec = specs[index];

					if (reachSpec.endActor != navPoint)
						continue; // Not a path to this nav point

					if (reachSpec.bPruned)
					{
						rejectedPruned++;
						continue;
					}
					if (reachSpec.collisionRadius < radius || reachSpec.collisionHeight < height)
					{
						rejectedCollision++;
						continue; // Skip nav node links that we can't pass through
					}

					if ((cur->bPlayerOnly() || reachSpec.endActor->bPlayerOnly()) && !bIsPlayer())
					{
						rejectedPlayerOnly++;
						continue; // Skip nav nodes only for the player if we aren't one
					}
					if ((reachSpec.reachFlags & ~allowedReachFlags) != 0)
					{
						rejectedCapability++;
						continue;
					}

					couldBeReachable = true;
					break;
				}

				if (couldBeReachable)
					break;

				for (int index : cur->PrunedPaths())
				{
					if (index < 0 || (size_t)index >= specs.size())
						break;
					const LevelReachSpec& reachSpec = specs[index];

					if (reachSpec.endActor != navPoint)
						continue; // Not a path to this nav point

					// PrunedPaths are valid reachability evidence: the edge was
					// removed from ordinary path search because another authored path
					// superseded it, not because the destination became unreachable.
					if (reachSpec.collisionRadius < radius || reachSpec.collisionHeight < height)
					{
						rejectedCollision++;
						continue; // Skip nav node links that we can't pass through
					}

					if ((cur->bPlayerOnly() || reachSpec.endActor->bPlayerOnly()) && !bIsPlayer())
					{
						rejectedPlayerOnly++;
						continue; // Skip nav nodes only for the player if we aren't one
					}
					if ((reachSpec.reachFlags & ~allowedReachFlags) != 0)
					{
						rejectedCapability++;
						continue;
					}

					couldBeReachable = true;
					break;
				}

				if (couldBeReachable)
					break;
			}

			if (!couldBeReachable)
			{
				if (BotBenchmark::IsActive())
				{
					BotBenchmark::Emit("actor_reachable_nav_rejected", {
						{ "seeker", Name.ToString() },
						{ "target", navPoint->Name.ToString() },
						{ "rejected_collision", std::to_string(rejectedCollision) },
						{ "rejected_player_only", std::to_string(rejectedPlayerOnly) },
						{ "rejected_capability", std::to_string(rejectedCapability) },
						{ "rejected_pruned", std::to_string(rejectedPruned) }
					});
				}
				return false;
			}
		}
	}

	// If the actor is in a pain zone and we don't like pain we can't go there
	if (aPawn)
	{
		if (aPawn->FootRegion().Zone->bPainZone() && aPawn->FootRegion().Zone->DamageType() != ReducedDamageType())
			return false;
	}
	else
	{
		if (anActor->Region().Zone->bPainZone() && anActor->Region().Zone->DamageType() != ReducedDamageType())
			return false;
	}

	// If the actor is in the water and we can't swim we can't go there
	if (anActor->Region().Zone->bWaterZone() && !bCanSwim())
		return false;

	vec3 eyePos = Location();
	eyePos.z += BaseEyeHeight();

	// If we can't see the actor we can't go there
	if (!FastTrace(anActor->Location(), eyePos))
		return false;

	// If we can't stand at the actor location we can't go there
	if (!CheckLocation(anActor->Location(), CollisionRadius(), CollisionHeight(), bCollideWorld() || bCollideWhenPlacing()).first)
		return false;

	// Try simulate movement to see if we can get to the actor
	int mode = Physics();
	if (mode == PHYS_Walking)
	{
		// To do: take zone changes into account?

		auto zone = Region().Zone;
		float gravityDirection = zone->ZoneGravity().z > 0.0f ? 1.0f : -1.0f;
		vec3 stepUpDelta(0.0f, 0.0f, -gravityDirection * MaxStepHeight());
		vec3 stepDownDelta(0.0f, 0.0f, gravityDirection * MaxStepHeight() * stepDownDeltaFactor);

		vec3 oldLocation = Location();
		bool reached = false;
		for (int iteration = 0; iteration < 5; iteration++)
		{
			vec3 moveDelta = anActor->Location() - Location();
			moveDelta.z = 0.0f;
			float goalDist2 = dot(moveDelta, moveDelta);
			if (goalDist2 <= 1.0f)
			{
				reached = true;
				break;
			}

			// step up first so we can get past stairs going up
			CollisionHit hit = TryMove(stepUpDelta, true);
			Location() += stepUpDelta * hit.Fraction;

			// move towards goal
			hit = TryMove(moveDelta, true);
			vec3 actuallyMoved = moveDelta * hit.Fraction;
			Location() += actuallyMoved;

			if (hit.Fraction < 1.0f)
			{
				moveDelta = anActor->Location() - Location();
				vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
				if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
				{
					hit = TryMove(alignedDelta, true);
					// Advance the dry-run by the vector that was actually traced. Using
					// the original blocked vector here could move the simulated pawn
					// through geometry and make ActorReachable disagree with MoveToward.
					actuallyMoved = alignedDelta * hit.Fraction;
					Location() += actuallyMoved;
				}
				else
				{
					break;
				}
			}

			// move back down to original vertical position
			hit = TryMove(-stepUpDelta, true);
			Location() -= stepUpDelta * hit.Fraction;

			float moveDist2 = dot(actuallyMoved, actuallyMoved);
			if (moveDist2 <= 1.0f)
				break;
		}

		if (reached)
		{
			// Step down + fall to goal
			vec3 moveDelta = anActor->Location() - Location();
			moveDelta.x = 0.0f;
			moveDelta.y = 0.0f;
			if ((moveDelta.z < -0.1f && gravityDirection == -1.0f) || (moveDelta.z > 0.1f && gravityDirection == 1.0f))
			{
				CollisionHit hit = TryMove(moveDelta, true);
				vec3 actuallyMoved = moveDelta * hit.Fraction;
				Location() += actuallyMoved;
			}

			// Did we get there vertically too?
			reached = std::abs(anActor->Location().z - Location().z) <= CollisionHeight();
		}

		Location() = oldLocation;
		return reached;
	}
	else if (mode == PHYS_Flying || mode == PHYS_Swimming)
	{
		// To do: take zone changes into account?

		vec3 oldLocation = Location();
		bool reached = false;
		for (int iteration = 0; iteration < 5; iteration++)
		{
			vec3 moveDelta = anActor->Location() - Location();
			float goalDist2 = dot(moveDelta, moveDelta);
			if (goalDist2 <= 1.0f)
			{
				reached = true;
				break;
			}

			CollisionHit hit = TryMove(moveDelta, true);
			vec3 actuallyMoved = moveDelta * hit.Fraction;
			Location() += actuallyMoved;

			if (hit.Fraction < 1.0f)
			{
				moveDelta = anActor->Location() - Location();
				vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
				if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
				{
					hit = TryMove(alignedDelta, true);
					actuallyMoved = alignedDelta * hit.Fraction;
					Location() += actuallyMoved;
				}
				else
				{
					break;
				}
			}

			float moveDist2 = dot(actuallyMoved, actuallyMoved);
			if (moveDist2 <= 1.0f)
				break;
		}

		Location() = oldLocation;
		return reached;
	}
	else
	{
		// Hopefully not a physics mode the bots use when calling ActorReachable
		LogUnimplemented("ActorReachable called for unsupported physics mode");
		return false;
	}
}

bool UPawn::PointReachable(vec3 aPoint)
{
	PointRegion pointRegion = XLevel()->Model->FindRegion(aPoint, Level());

	if (!Region().Zone->bWaterZone() && !bCanSwim() && pointRegion.Zone->bWaterZone())
		return false;
	if (!FootRegion().Zone->bPainZone() && pointRegion.Zone->bPainZone() && pointRegion.Zone->DamageType() != ReducedDamageType())
		return false;

	vec3 eyePos = Location();
	eyePos.z += BaseEyeHeight();
	if (!FastTrace(aPoint, eyePos))
		return false;

	return CheckLocation(aPoint, CollisionRadius(), CollisionHeight(), bCollideWorld() || bCollideWhenPlacing()).first;
}

bool UPawn::PickWallAdjust()
{
	auto kneeHeight = CollisionHeight() * 0.45f;

	auto forwards = normalize(Acceleration().xy());

	auto afterJumpCollisionHit = TryMove(vec3(forwards, kneeHeight), true);

	if (afterJumpCollisionHit.Fraction == 1)
	{
		// Obstacle can be jumped over. Attempt jumping.
		bFromWall() = false;
		Velocity().z = JumpZ();
		SetPhysics(PHYS_Falling);
		Destination() = Location() + vec3(forwards, kneeHeight);

		return true;
	}

	// Obstacle cannot be jumped over. Try another direction
	auto direction = Focus() - Location();
	auto rightSideVec = normalize(cross(direction, vec3(0, 0, 1)));
	auto rightSideTest = TryMove(rightSideVec, true);
	if (rightSideTest.Fraction == 1)
	{
		// We can move to right instead
		bFromWall() = true;
		Destination() = Location() + rightSideVec;
		// Focus() = Location() + rightSideVec;

		return true;
	}

	auto leftSideVec = -rightSideVec;
	auto leftSideTest = TryMove(leftSideVec, true);
	if (leftSideTest.Fraction >= 1)
	{
		// We can move to left instead
		bFromWall() = true;
		Destination() = Location() + leftSideVec;
		// Focus() = Location() + leftSideVec;

		return true;
	}

	// Cannot go anywhere from here
	return false;
}

vec3 UPawn::EAdjustJump()
{
	UZoneInfo* zone = FootRegion().Zone;
	const float gravityZ = zone ? zone->ZoneGravity().z : -980.0f;
	const float verticalSpeed = Velocity().z;
	const vec3 delta = Destination() - Location();

	// Solve Destination.Z = Location.Z + Vz*t + 0.5*g*t^2 and use the
	// descending (later) positive root. Scripts set Velocity.Z immediately
	// before EAdjustJump; using JumpZ or Focus here discards impact-jump boost
	// and can steer a strafing bot toward its enemy instead of its jump goal.
	float flightTime = 0.0f;
	if (std::abs(gravityZ) > 0.001f)
	{
		const float discriminant = verticalSpeed * verticalSpeed + 2.0f * gravityZ * delta.z;
		if (discriminant >= 0.0f)
		{
			const float root = std::sqrt(discriminant);
			const float timeA = (-verticalSpeed + root) / gravityZ;
			const float timeB = (-verticalSpeed - root) / gravityZ;
			if (timeA > 0.001f)
				flightTime = timeA;
			if (timeB > flightTime)
				flightTime = timeB;
		}
	}
	else if (std::abs(verticalSpeed) > 0.001f)
	{
		const float linearTime = delta.z / verticalSpeed;
		if (linearTime > 0.001f)
			flightTime = linearTime;
	}

	vec2 horizontalVelocity = Velocity().xy();
	if (flightTime > 0.001f)
		horizontalVelocity = delta.xy() * (1.0f / flightTime);

	const float horizontalSpeed = length(horizontalVelocity);
	const float maxHorizontalSpeed = std::max(GroundSpeed(), 0.0f);
	if (horizontalSpeed > maxHorizontalSpeed && maxHorizontalSpeed > 0.0f)
		horizontalVelocity = horizontalVelocity * (maxHorizontalSpeed / horizontalSpeed);

	if (BotBenchmark::IsActive())
	{
		BotBenchmark::Emit("jump_adjust", {
			{ "actor", Name.ToString() },
			{ "flight_time_seconds", std::to_string(flightTime) },
			{ "horizontal_speed", std::to_string(length(horizontalVelocity)) },
			{ "vertical_speed", std::to_string(verticalSpeed) },
			{ "target", "Destination" }
		});
	}

	return vec3(horizontalVelocity, verticalSpeed);
}

bool UPawn::LineOfSightTo(UActor* other, bool ignoreDistance)
{
	if (!other)
		return false;

	// Additional 227 checks because of Pawn.SightCheckType being a variable there
	// Since we don't have any 227-only fields added yet, this part remains as a proof of concept
	// if (engine->packages->IsUnreal1_227() &&
	// 	(SightCheckType() == EPawnSightCheck::SEE_None ||
	// 	(SightCheckType() == EPawnSightCheck::SEE_PlayersOnly && !Cast<UPawn>(other)->bIsPlayer())))
	// 	return false;

	if (!ignoreDistance && length(Location() - other->Location()) > SightRadius())
		return false;

	vec3 eye_pos = Location();
	eye_pos.z += BaseEyeHeight();

	auto& origin = other->Location();
	auto top = origin + vec3{ 0.f, 0.f, other->CollisionHeight() / 2 };
	auto bottom = origin - vec3{ 0.f, 0.f, other->CollisionHeight() / 2 };

	return FastTrace(origin, eye_pos) || FastTrace(top, eye_pos) || FastTrace(bottom, eye_pos);
}

bool UPawn::CanSee(UActor* other)
{
	if (!other || other->bDeleteMe())
		return false;

	vec3 eye_pos = Location();
	eye_pos.z += BaseEyeHeight();
	vec3 delta = other->Location() - eye_pos;
	float distanceSquared = dot(delta, delta);

	// SeePlayer scales acquisition range by the target Pawn's Visibility. A
	// normal Pawn has Visibility 128; zero is invisible to AI and 255 is almost
	// twice as visible. Non-Pawn actors retain the observer's normal radius.
	float visibilityScale = 1.0f;
	if (UPawn* otherPawn = UObject::TryCast<UPawn>(other))
	{
		if (otherPawn->Visibility() == 0)
			return false;
		visibilityScale = static_cast<float>(otherPawn->Visibility()) / 128.0f;
	}

	float sightDistance = SightRadius() * visibilityScale;
	if (distanceSquared > sightDistance * sightDistance)
		return false;

	// PeripheralVision is the signed cosine threshold. In particular, Godlike
	// UT bots use -0.2 to obtain a field wider than 180 degrees. Use the vector
	// relative to this Pawn rather than the target's absolute world position.
	if (distanceSquared > 0.0001f)
	{
		vec3 forward = Coords::Rotation(Rotation()).XAxis;
		float cosine = dot(normalize(forward), delta * (1.0f / std::sqrt(distanceSquared)));
		if (cosine < PeripheralVision())
			return false;
	}

	// The visibility-scaled range was checked above. Use LineOfSightTo only for
	// its origin/top/bottom occlusion tests so highly visible Pawns are not
	// incorrectly clamped back to the unscaled SightRadius.
	return LineOfSightTo(other, true);
}

void UPawn::TickSight(float elapsed)
{
	if (Role() != ROLE_Authority || Health() <= 0 || bDeleteMe())
		return;

	SightCounter() -= elapsed;
	if (SightCounter() > 0.0f)
		return;

	// Provisional retail-compatible cadence. Pawn.PreBeginPlay initializes a
	// random offset in [0, 0.2), which keeps these scans staggered. Never run a
	// catch-up loop after a long frame: one potentially O(Pawns) pass per Tick.
	static constexpr float sightInterval = 0.20f;
	SightCounter() += sightInterval;
	if (SightCounter() <= 0.0f)
		SightCounter() = sightInterval;

	if (!IsEventEnabled(EventName::SeePlayer) && !IsEventEnabled(EventName::EnemyNotVisible))
		return;

	// UnrealScript callbacks may Destroy a Pawn (and RemovePawn mutates this
	// singly linked list), change Enemy/state, or Enable/Disable a probe. Take a
	// stable pointer snapshot before the first callback and revalidate every
	// pointer and probe before it is used.
	Array<UPawn*> candidates;
	for (UPawn* candidate = Level()->PawnList(); candidate; candidate = candidate->nextPawn())
		candidates.push_back(candidate);

	UPawn* trackedEnemy = Enemy();
	if (trackedEnemy && IsEventEnabled(EventName::EnemyNotVisible))
	{
		bool enemyVisible = !trackedEnemy->bDeleteMe() && trackedEnemy->Health() > 0 && LineOfSightTo(trackedEnemy, true);
		if (enemyVisible)
		{
			LastSeenPos() = trackedEnemy->Location();
			LastSeeingPos() = Location();
			LastSeenTime() = Level()->TimeSeconds();
		}
		else
		{
			if (BotBenchmark::IsActive())
			{
				BotBenchmark::Emit("enemy_not_visible", {
					{ "observer", Name.ToString() },
					{ "enemy", trackedEnemy->Name.ToString() },
					{ "state", GetStateName().ToString() }
				});
			}
			CallEvent(this, EventName::EnemyNotVisible);
		}
	}

	for (UPawn* candidate : candidates)
	{
		if (bDeleteMe() || Health() <= 0 || Role() != ROLE_Authority)
			return;
		if (!IsEventEnabled(EventName::SeePlayer))
			break;
		if (!candidate || candidate == this || candidate->bDeleteMe() || candidate->Health() <= 0)
			continue;
		if (!candidate->bIsPlayer() || candidate->Visibility() == 0)
			continue;
		if (!CanSee(candidate))
			continue;

		if (BotBenchmark::IsActive())
		{
			BotBenchmark::Emit("see_player", {
				{ "observer", Name.ToString() },
				{ "seen", candidate->Name.ToString() },
				{ "state", GetStateName().ToString() }
			});
		}
		CallEvent(this, EventName::SeePlayer, { ExpressionValue::ObjectValue(candidate) });
	}
}

bool UPawn::CanHearNoise(UActor* source, float loudness)
{
	UPawn* noisePawn = UObject::Cast<UPawn>(source->Instigator());
	if (!noisePawn->bIsPlayer() && (!noisePawn->Enemy() || !noisePawn->Enemy()->bIsPlayer()))
	{
		if (!IsA(source->Class->Name) && !source->IsA(Class->Name))
			return false;
	}
	else if (UObject::TryCast<UPlayerPawn>(this))
	{
		return false;
	}

	vec3 delta = Location() - source->Location();
	float dist2 = dot(delta, delta);

	if (!bIsPlayer() || !Level()->Game()->bTeamGame() || !noisePawn->bIsPlayer() ||
		(engine->LaunchInfo.ue1Version > 219 && (!PlayerReplicationInfo() || !noisePawn->PlayerReplicationInfo() || (PlayerReplicationInfo()->Team() != noisePawn->PlayerReplicationInfo()->Team()))))
	{
		if (dist2 > (4000.0f * 4000.0f) * (loudness * loudness))
			return false;

		float perceived = std::min(1200000.f / dist2, 2.0f);
		Stimulus() = loudness * perceived + Alertness() * std::min(0.5f, perceived);
		if (Stimulus() < HearingThreshold())
			return false;
	}
	else if (dist2 > (4000.0f * 4000.0f) * (loudness * loudness))
	{
		return false;
	}

	return !XLevel()->Collision.TraceAnyHit(source->Location(), Location(), source, false, true, false);
}

void UPawn::ClientHearSound(UActor* actor, int id, USound* sound, const vec3& soundLocation, const vec3& parameters)
{
	LogUnimplemented("UPawn.ClientHearSound()");
}

UActor* UPawn::PickAnyTarget(float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart)
{
	UActor* bestActor = nullptr;
	for (UActor* actor : XLevel()->Actors)
	{
		// We are only looking for targets that isn't a pawn (pawn uses PickTarget if it wants a pawn)
		if (!actor || actor == this || UObject::TryCast<UPawn>(actor) || !actor->bProjTarget())
			continue;

		if (CheckIfBestTarget(actor, bestAim, bestDist, FireDir, projStart))
			bestActor = actor;
	}
	return bestActor;
}

UActor* UPawn::PickTarget(float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart)
{
	UActor* bestActor = nullptr;
	UPlayerReplicationInfo* ourPlayerInfo = engine->LaunchInfo.ue1Version > 219 ? PlayerReplicationInfo() : nullptr;
	bool teamGame = ourPlayerInfo && Level()->Game()->bTeamGame();
	for (UPawn* pawn = Level()->PawnList(); pawn != nullptr; pawn = pawn->nextPawn())
	{
		// Skip dead pawns or ourselves
		if (pawn == this || pawn->Health() <= 0)
			continue;

		// Skip team mates
		if (engine->LaunchInfo.ue1Version > 219)
		{
			auto pawnPlayerInfo = pawn->PlayerReplicationInfo();
			if (teamGame && pawnPlayerInfo && ourPlayerInfo->Team() == pawnPlayerInfo->Team())
				continue;
		}

		if (CheckIfBestTarget(pawn, bestAim, bestDist, FireDir, projStart))
			bestActor = pawn;
	}
	return bestActor;
}

bool UPawn::CheckIfBestTarget(UActor* actor, float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart)
{
	// Ignore targets behind us
	vec3 delta = actor->Location() - projStart;
	float angle = dot(FireDir, delta);
	if (angle < 0.0f)
		return false;

	// Skip things too far away
	float distance = length(delta);
	if (distance == 0.0f || distance > 2500.0f)
		return false;

	// Skip if we already have a target closer to the direction we are facing
	angle /= distance;
	if (angle < bestAim)
		return false;

	// Skip if we can't see the target
	if (!LineOfSightTo(actor, false))
		return false;

	// OK, this is better than what we have
	bestAim = angle;
	bestDist = distance;
	return true;
}

UNavigationPoint* UPawn::SetRouteCache(const Array<UNavigationPoint*>& points)
{
	if (engine->LaunchInfo.ue1Version > 219)
	{
		auto cache = RouteCache();
		for (size_t i = 0; i < cache.size(); i++)
			cache[i] = (i < points.size()) ? points[i] : nullptr;
	}
	if (BotBenchmark::IsActive())
	{
		std::ostringstream route;
		for (size_t i = 0; i < points.size(); i++)
		{
			if (i != 0)
				route << '>';
			route << points[i]->Name.ToString();
		}
		BotBenchmark::Emit("route_cache", {
			{ "seeker", Name.ToString() },
			{ "next", points.empty() ? "None" : points.front()->Name.ToString() },
			{ "route", route.str() },
			{ "route_nodes", std::to_string(points.size()) }
		});
	}
	return !points.empty() ? points.front() : nullptr;
}

UActor* UPawn::PathSpecialHandling(const Array<UNavigationPoint*>& bestPath)
{
#if 0
	return SetRouteCache(bestPath);
#else
	IsInPathSpecialHandling = true;
	UActor* oldBestPoint = SetRouteCache(bestPath);
	if (!oldBestPoint)
	{
		IsInPathSpecialHandling = false;
		return nullptr;
	}

	UActor* bestPoint = oldBestPoint;
	const bool handlerEnabled = oldBestPoint->IsEventEnabled(EventName::SpecialHandling);
	bool redirectDirectlyReachable = true;
	bool redirectedThroughPath = false;

	if (handlerEnabled)
	{
		bestPoint = UObject::Cast<UActor>(CallEvent(oldBestPoint, EventName::SpecialHandling, { ExpressionValue::ObjectValue(this) }).ToObject());
		if (!bCanDoSpecial())
			bestPoint = nullptr;
		SpecialGoal() = bestPoint;

		if (bestPoint && bestPoint != oldBestPoint)
		{
			redirectDirectlyReachable = ActorReachable(bestPoint);
			if (!redirectDirectlyReachable)
			{
				redirectedThroughPath = true;
				bestPoint = UObject::Cast<UActor>(FindPathToward(bestPoint, false));
			}
		}
	}
	else
	{
		if (SpecialGoal() == oldBestPoint)
			SpecialGoal() = nullptr;
	}
	if (BotBenchmark::IsActive())
	{
		auto describeActor = [](UActor* actor)
		{
			return actor ? UObject::GetUClassFullName(actor).ToString() + ":" + actor->Name.ToString() : std::string("None");
		};
		BotBenchmark::Emit("route_special_handling", {
			{ "seeker", Name.ToString() },
			{ "original", describeActor(oldBestPoint) },
			{ "handler_enabled", handlerEnabled ? "true" : "false" },
			{ "can_do_special", bCanDoSpecial() ? "true" : "false" },
			{ "redirect_directly_reachable", redirectDirectlyReachable ? "true" : "false" },
			{ "redirected_through_path", redirectedThroughPath ? "true" : "false" },
			{ "result", describeActor(bestPoint) }
		});
	}

	IsInPathSpecialHandling = false;
	return bestPoint;
#endif
}


UPawn::PathSearchResult UPawn::FindPathToEndPoint(UNavigationPoint* start, int maxNodes, const Array<UNavigationPoint*>& endPoints)
{
	PathSearchResult result;
	if (!start || maxNodes <= 0 || endPoints.empty() || (start->bPlayerOnly() && !bIsPlayer()))
		return result;
	const std::unordered_set<UNavigationPoint*> endPointSet(endPoints.begin(), endPoints.end());

	struct NodeState
	{
		int64_t WeightedCost = std::numeric_limits<int64_t>::max();
		int64_t TravelDistance = std::numeric_limits<int64_t>::max();
		UNavigationPoint* NextTowardGoal = nullptr;
		bool Closed = false;
	};

	struct QueueEntry
	{
		int64_t WeightedCost;
		int64_t TravelDistance;
		size_t StableOrder;
		UNavigationPoint* Point;
	};

	struct QueueGreater
	{
		bool operator()(const QueueEntry& a, const QueueEntry& b) const
		{
			if (a.WeightedCost != b.WeightedCost)
				return a.WeightedCost > b.WeightedCost;
			if (a.TravelDistance != b.TravelDistance)
				return a.TravelDistance > b.TravelDistance;
			return a.StableOrder > b.StableOrder;
		}
	};

	const Array<LevelReachSpec>& reachSpecs = XLevel()->ReachSpecs;
	const int radius = (int)CollisionRadius();
	const int height = (int)CollisionHeight();

	int allowedReachFlags = 0;
	if (bCanWalk()) allowedReachFlags |= R_WALK;
	if (bCanFly()) allowedReachFlags |= R_FLY;
	if (bCanSwim()) allowedReachFlags |= R_SWIM;
	if (bCanJump()) allowedReachFlags |= R_JUMP;
	if (bCanOpenDoors()) allowedReachFlags |= R_DOOR;
	if (bCanDoSpecial()) allowedReachFlags |= R_SPECIAL;
	if (bIsPlayer()) allowedReachFlags |= R_PLAYERONLY;

	std::unordered_map<UNavigationPoint*, size_t> stableOrder;
	size_t nextStableOrder = 0;
	for (UNavigationPoint* point = Level()->NavigationPointList(); point; point = point->nextNavigationPoint())
		stableOrder.emplace(point, nextStableOrder++);

	auto getStableOrder = [&](UNavigationPoint* point)
	{
		auto it = stableOrder.find(point);
		return it != stableOrder.end() ? it->second : std::numeric_limits<size_t>::max();
	};

	auto nodeCost = [](UNavigationPoint* point)
	{
		// UE1 navigation costs are non-negative penalties. Treat malformed
		// negative map/mod values as zero so Dijkstra's invariant remains valid.
		return std::max<int64_t>((int64_t)point->cost(), 0);
	};

	std::unordered_map<UNavigationPoint*, NodeState> states;
	std::priority_queue<QueueEntry, std::vector<QueueEntry>, QueueGreater> open;
	NodeState& goalState = states[start];
	goalState.WeightedCost = 0;
	goalState.TravelDistance = 0;
	open.push({ 0, 0, getStableOrder(start), start });

	UNavigationPoint* bestEndPoint = nullptr;
	int64_t bestWeightedCost = std::numeric_limits<int64_t>::max();
	int64_t bestTravelDistance = std::numeric_limits<int64_t>::max();
	int expanded = 0;
	int rejectedCollision = 0;
	int rejectedCapability = 0;
	int rejectedPruned = 0;
	int rejectedPlayerOnly = 0;

	while (!open.empty() && expanded < maxNodes)
	{
		QueueEntry entry = open.top();
		open.pop();

		auto stateIt = states.find(entry.Point);
		if (stateIt == states.end())
			continue;
		NodeState& currentState = stateIt->second;
		if (currentState.Closed || entry.WeightedCost != currentState.WeightedCost || entry.TravelDistance != currentState.TravelDistance)
			continue;
		if (bestEndPoint && entry.WeightedCost > bestWeightedCost)
			break;

		currentState.Closed = true;
		expanded++;

		if (endPointSet.find(entry.Point) != endPointSet.end())
		{
			int64_t entryDistance = (int64_t)std::llround(length(entry.Point->Location() - Location()));
			int64_t candidateTravel = currentState.TravelDistance + entryDistance;
			int64_t candidateWeight = currentState.WeightedCost + entryDistance + nodeCost(entry.Point);
			if (!bestEndPoint || candidateWeight < bestWeightedCost ||
				(candidateWeight == bestWeightedCost && candidateTravel < bestTravelDistance) ||
				(candidateWeight == bestWeightedCost && candidateTravel == bestTravelDistance && getStableOrder(entry.Point) < getStableOrder(bestEndPoint)))
			{
				bestEndPoint = entry.Point;
				bestWeightedCost = candidateWeight;
				bestTravelDistance = candidateTravel;
			}
		}

		for (int specIndex : entry.Point->upstreamPaths())
		{
			if (specIndex < 0)
				break;
			if ((size_t)specIndex >= reachSpecs.size())
				continue;

			const LevelReachSpec& reachSpec = reachSpecs[specIndex];
			UNavigationPoint* predecessor = reachSpec.startActor;
			if (!predecessor || reachSpec.endActor != entry.Point)
				continue;
			if (reachSpec.bPruned)
			{
				rejectedPruned++;
				continue;
			}
			if (reachSpec.collisionRadius < radius || reachSpec.collisionHeight < height)
			{
				rejectedCollision++;
				continue;
			}
			if (predecessor->bPlayerOnly() && !bIsPlayer())
			{
				rejectedPlayerOnly++;
				continue;
			}
			if ((reachSpec.reachFlags & ~allowedReachFlags) != 0)
			{
				rejectedCapability++;
				continue;
			}

			int64_t edgeDistance = std::max<int64_t>((int64_t)reachSpec.distance, 0);
			int64_t candidateTravel = currentState.TravelDistance + edgeDistance;
			int64_t candidateWeight = currentState.WeightedCost + edgeDistance + nodeCost(entry.Point);
			NodeState& predecessorState = states[predecessor];
			if (candidateWeight < predecessorState.WeightedCost ||
				(candidateWeight == predecessorState.WeightedCost && candidateTravel < predecessorState.TravelDistance))
			{
				predecessorState.WeightedCost = candidateWeight;
				predecessorState.TravelDistance = candidateTravel;
				predecessorState.NextTowardGoal = entry.Point;
				predecessorState.Closed = false;
				open.push({ candidateWeight, candidateTravel, getStableOrder(predecessor), predecessor });
			}
		}
	}

	if (bestEndPoint)
	{
		UNavigationPoint* point = bestEndPoint;
		for (int guard = 0; point && guard <= maxNodes; guard++)
		{
			result.Path.push_back(point);
			if (point == start)
				break;
			auto it = states.find(point);
			point = it != states.end() ? it->second.NextTowardGoal : nullptr;
		}

		if (result.Path.empty() || result.Path.back() != start)
		{
			result.Path.clear();
			bestEndPoint = nullptr;
		}
		else
		{
			// Preserve the stock route-cache contract while skipping a directly
			// reachable anchor the Pawn is already touching. Never remove the goal.
			while (result.Path.size() > 1)
			{
				vec3 delta = result.Path.front()->Location() - Location();
				float heightDiff = result.Path.front()->Location().z - Location().z;
				if (dot(delta, delta) >= (float)radius * (float)radius || std::abs(heightDiff) >= (float)height)
					break;
				result.Path.erase(result.Path.begin());
			}
			result.TravelDistance = bestTravelDistance;
			result.WeightedCost = bestWeightedCost;
		}
	}

	if (BotBenchmark::IsActive())
	{
		std::ostringstream route;
		for (size_t i = 0; i < result.Path.size(); i++)
		{
			if (i != 0)
				route << '>';
			route << result.Path[i]->Name.ToString();
		}
		BotBenchmark::Emit("route_search", {
			{ "seeker", Name.ToString() },
			{ "goal", start->Name.ToString() },
			{ "status", result.Path.empty() ? "no_path" : "success" },
			{ "route", route.str() },
			{ "route_nodes", std::to_string(result.Path.size()) },
			{ "travel_distance", std::to_string(result.TravelDistance) },
			{ "weighted_cost", std::to_string(result.WeightedCost) },
			{ "expanded", std::to_string(expanded) },
			{ "endpoints", std::to_string(endPoints.size()) },
			{ "rejected_collision", std::to_string(rejectedCollision) },
			{ "rejected_capability", std::to_string(rejectedCapability) },
			{ "rejected_pruned", std::to_string(rejectedPruned) },
			{ "rejected_player_only", std::to_string(rejectedPlayerOnly) }
		});
	}

	return result;
}

void UPawn::ClearPaths()
{
	for (UNavigationPoint* cur = Level()->NavigationPointList(); cur; cur = cur->nextNavigationPoint())
	{
		cur->bEndPoint() = false;
		if (!engine->LaunchInfo.IsKlingonHonorGuard())
		{
			if (cur->bSpecialCost())
				cur->cost() = CallEvent(cur, "SpecialCost", { ExpressionValue::ObjectValue(this) }).ToInt();
			else
				cur->cost() = cur->ExtraCost();
		}
	}
}

UObject* UPawn::FindRandomDest()
{
	Array<UNavigationPoint*> reachablePoints = FindReachableNavEndPoints(false);

	if (reachablePoints.empty())
		return nullptr;

	// Add every node reachable through a legal authored edge. Keep visited state
	// query-local rather than borrowing NavigationPoint.bEndPoint.
	const Array<LevelReachSpec>& reachSpecs = XLevel()->ReachSpecs;
	const int radius = (int)CollisionRadius();
	const int height = (int)CollisionHeight();
	int allowedReachFlags = 0;
	if (bCanWalk()) allowedReachFlags |= R_WALK;
	if (bCanFly()) allowedReachFlags |= R_FLY;
	if (bCanSwim()) allowedReachFlags |= R_SWIM;
	if (bCanJump()) allowedReachFlags |= R_JUMP;
	if (bCanOpenDoors()) allowedReachFlags |= R_DOOR;
	if (bCanDoSpecial()) allowedReachFlags |= R_SPECIAL;
	if (bIsPlayer()) allowedReachFlags |= R_PLAYERONLY;
	std::unordered_set<UNavigationPoint*> visited(reachablePoints.begin(), reachablePoints.end());
	for (size_t i = 0; i < reachablePoints.size(); i++)
	{
		UNavigationPoint* navPoint = reachablePoints[i];

		for (int specIndex : navPoint->Paths())
		{
			if (specIndex < 0 || (size_t)specIndex >= reachSpecs.size())
				break;
			const LevelReachSpec& reachSpec = reachSpecs[specIndex];

			UNavigationPoint* next = reachSpec.endActor;
			if (!next || visited.find(next) != visited.end())
				continue; // Already processed

			if (reachSpec.collisionRadius < radius || reachSpec.collisionHeight < height || reachSpec.bPruned)
				continue; // Skip nav node links that we can't pass through

			if (next->bPlayerOnly() && !bIsPlayer())
				continue; // Skip nav nodes only for the player if we aren't one
			if ((reachSpec.reachFlags & ~allowedReachFlags) != 0)
				continue;

			visited.insert(next);
			reachablePoints.push_back(next);
		}
	}

	// RAND_MAX + 1 keeps every candidate's interval the same width and avoids
	// the endpoint bias introduced by round().
	const double randomValue = std::rand() / (static_cast<double>(RAND_MAX) + 1.0);
	const size_t index = std::min(static_cast<size_t>(randomValue * reachablePoints.size()), reachablePoints.size() - 1);
	if (BotBenchmark::IsActive())
	{
		BotBenchmark::Emit("random_destination", {
			{ "seeker", Name.ToString() },
			{ "candidates", std::to_string(reachablePoints.size()) },
			{ "selected", reachablePoints[index]->Name.ToString() }
		});
	}
	return reachablePoints[index];
}

UObject* UPawn::FindPathTo(const vec3& aPoint, bool bSinglePath)
{
	return FindPathToward(FindClosestNavPoint(aPoint), bSinglePath);
}

Array<UNavigationPoint*> UPawn::FindReachableNavEndPoints(bool singlePath)
{
	struct Candidate
	{
		UNavigationPoint* Point = nullptr;
		float DistanceSquared = 0.0f;
		size_t StableOrder = 0;
	};

	Array<Candidate> candidates;
	size_t stableOrder = 0;
	for (UNavigationPoint* navPoint = Level()->NavigationPointList(); navPoint; navPoint = navPoint->nextNavigationPoint())
	{
		if (navPoint->bPlayerOnly() && !bIsPlayer())
			continue;
		vec3 delta = navPoint->Location() - Location();
		candidates.push_back({ navPoint, dot(delta, delta), stableOrder++ });
	}

	std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
	{
		if (a.DistanceSquared != b.DistanceSquared)
			return a.DistanceSquared < b.DistanceSquared;
		return a.StableOrder < b.StableOrder;
	});

	Array<UNavigationPoint*> reachable;
	for (const Candidate& candidate : candidates)
	{
		// ActorReachable performs the collision/step/fall simulation and retains
		// UE1's 1000 UU direct-reach contract. Sorting before testing removes map
		// linked-list order bias; retaining every reachable anchor removes the old
		// arbitrary first-eight cap.
		if (!ActorReachable(candidate.Point))
			continue;
		reachable.push_back(candidate.Point);
		if (singlePath)
			break;
	}

	if (BotBenchmark::IsActive())
	{
		BotBenchmark::Emit("route_anchors", {
			{ "seeker", Name.ToString() },
			{ "candidates", std::to_string(candidates.size()) },
			{ "reachable", std::to_string(reachable.size()) },
			{ "single_path", singlePath ? "true" : "false" }
		});
	}
	return reachable;
}

float UPawn::AICanHear(UActor* other, std::optional<float> volume, std::optional<float> radius)
{
	LogUnimplemented("Pawn.AICanHear() [Deus Ex]");
	return 0.0f;
}

float UPawn::AICanSee(UActor* other, std::optional<float> visibility, std::optional<bool> bCheckVisibility, std::optional<bool> bCheckDir, std::optional<bool> bCheckCylinder, std::optional<bool> bCheckLOS)
{
	LogUnimplemented("Pawn.AICanSee() [Deus Ex]");
	return 0.0f;
}

float UPawn::AICanSmell(UActor* other, std::optional<float> smell)
{
	LogUnimplemented("Pawn.AICanSmell() [Deus Ex]");
	return 0.0f;
}

UObject* UPawn::FindPathToward(UObject* anActor, bool singlePath)
{
	if (auto aNavPoint = UObject::TryCast<UNavigationPoint>(anActor))
	{
		Array<UNavigationPoint*> endPoints = FindReachableNavEndPoints(singlePath);
		if (endPoints.empty())
			return SetRouteCache({});
		PathSearchResult path = FindPathToEndPoint(aNavPoint, 1000, endPoints);
		if (!IsInPathSpecialHandling)
			return PathSpecialHandling(path.Path);
		// A SpecialHandling callback may redirect to an unreachable actor. Route
		// that actor normally, but do not recursively invoke SpecialHandling on
		// the redirected first node during the same query.
		return SetRouteCache(path.Path);
	}
	else if (auto actor = UObject::TryCast<UActor>(anActor))
	{
		return FindPathToward(FindClosestNavPoint(actor->Location()), singlePath);
	}
	else
	{
		return SetRouteCache({});
	}
}

UNavigationPoint* UPawn::FindClosestNavPoint(vec3 location)
{
	// Order nav points by distance
	std::vector<std::pair<UNavigationPoint*, float>> navPoints;
	for (UNavigationPoint* navPoint = Level()->NavigationPointList(); navPoint; navPoint = navPoint->nextNavigationPoint())
	{
		if (navPoint->bPlayerOnly() && !bIsPlayer())
			continue; // Skip nav nodes only for the player if we aren't one
		vec3 d = navPoint->Location() - location;
		float distsqr = dot(d, d);
		navPoints.push_back({ navPoint, distsqr });
	}

	std::stable_sort(navPoints.begin(), navPoints.end(), [](const auto& a, const auto& b) { return a.second < b.second; });

	// Find the nearest visible navigation endpoint. The previous 500 UU/four
	// trace caps made valid actor goals fail based solely on map list order.
	for (auto& p : navPoints)
	{
		vec3 eyePos = p.first->Location();
		eyePos.z += BaseEyeHeight();
		if (FastTrace(location, eyePos))
			return p.first;
	}
	return nullptr;
}

UObject* UPawn::FindBestInventoryPath(bool predictRespawns, float& outBestWeight)
{
	// FindBestInventoryPath has no bClearPaths argument in UE1. Refresh the
	// per-pawn navigation penalties before scoring candidates.
	ClearPaths();
	Array<UNavigationPoint*> endPoints = FindReachableNavEndPoints(false);
	if (endPoints.empty())
	{
		outBestWeight = 0.0f;
		return SetRouteCache({});
	}

	float bestWeight = 0.0f;
	UInventorySpot* bestSpot = nullptr;
	Array<UNavigationPoint*> bestPath;
	int64_t bestTravelDistance = 0;
	int64_t bestWeightedCost = 0;
	float bestDesire = 0.0f;
	float bestRespawnRemaining = 0.0f;
	float bestPhysicalETA = 0.0f;
	std::string bestInventoryState;
	std::string bestItemName;
	std::string bestEligibilityReason;

	// Use the fastest advertised movement mode so ETA is deliberately
	// optimistic. That makes respawn prediction conservative: uncertain items
	// are rejected rather than assuming that lifts, detours, or slower movement
	// will buy the bot more time.
	const float etaSpeed = std::max({ GroundSpeed(), AirSpeed(), WaterSpeed() });

	for (UNavigationPoint* navPoint = Level()->NavigationPointList(); navPoint; navPoint = navPoint->nextNavigationPoint())
	{
		auto invSpot = UObject::TryCast<UInventorySpot>(navPoint);
		if (!invSpot)
			continue;
		auto inv = invSpot->markedItem();
		if (!inv)
			continue;

		// Unreal names are case-insensitive. Keep eligibility checks in NameString
		// space; ToString() may canonicalize PickUp as "Pickup" and a normal
		// std::string comparison would incorrectly reject every available item.
		const NameString inventoryStateName = inv->GetStateName();
		const std::string inventoryState = inventoryStateName.ToString();
		const bool isPickup = inventoryStateName == "PickUp";
		const bool isSleeping = inventoryStateName == "Sleeping";

		// Knowledge/provenance limitation: UE1 exposes no dedicated native
		// respawn countdown here. Sleeping inventory is assumed to use Actor's
		// script-visible LatentFloat as the remainder of Sleep(RespawnTime). This
		// matches script latent behavior but has not been verified against the
		// closed retail native implementation, so unsupported states stay rejected.
		const float respawnRemaining = isSleeping ? std::max(inv->LatentFloat(), 0.0f) : 0.0f;
		const bool needsPathForEligibility = isPickup || (isSleeping && predictRespawns);
		PathSearchResult pathResult;
		if (needsPathForEligibility)
			pathResult = FindPathToEndPoint(invSpot, 1000, endPoints);

		const bool pathFound = !pathResult.Path.empty();
		const float physicalETA = pathFound && std::isfinite(etaSpeed) && etaSpeed > 0.0f
			? (float)pathResult.TravelDistance / etaSpeed
			: -1.0f;

		bool eligible = isPickup;
		std::string eligibilityReason = isPickup ? "pickup_available" : "unsupported_state";
		if (isSleeping)
		{
			if (!predictRespawns)
				eligibilityReason = "prediction_disabled";
			else if (!pathFound)
				eligibilityReason = "no_path";
			else if (physicalETA < 0.0f)
				eligibilityReason = "eta_speed_unavailable";
			else if (respawnRemaining <= physicalETA)
			{
				eligible = true;
				eligibilityReason = "respawns_by_eta";
			}
			else
				eligibilityReason = "respawns_after_eta";
		}

		float desire = 0.0f;
		float weight = 0.0f;
		if (eligible && pathFound)
		{
			desire = CallEvent(inv, "BotDesireability", { ExpressionValue::ObjectValue(this) }).ToFloat();
			float distance = std::max((float)pathResult.WeightedCost, 1.0f);
			weight = desire > 0.0f ? desire / distance : 0.0f;

			if (desire > 0.0f && (!bestSpot || weight > bestWeight))
			{
				bestSpot = invSpot;
				bestWeight = weight;
				bestPath = std::move(pathResult.Path);
				bestTravelDistance = pathResult.TravelDistance;
				bestWeightedCost = pathResult.WeightedCost;
				bestDesire = desire;
				bestRespawnRemaining = respawnRemaining;
				bestPhysicalETA = physicalETA;
				bestInventoryState = inventoryState;
				bestItemName = inv->Name.ToString();
				bestEligibilityReason = eligibilityReason;
			}
		}

		if (BotBenchmark::IsActive())
		{
			BotBenchmark::Emit("inventory_path_candidate", {
				{ "seeker", Name.ToString() },
				{ "spot", invSpot->Name.ToString() },
				{ "item", inv->Name.ToString() },
				{ "inventory_state", inventoryState },
				{ "respawn_remaining_seconds", std::to_string(respawnRemaining) },
				{ "physical_eta_seconds", std::to_string(physicalETA) },
				{ "eta_speed", std::to_string(etaSpeed) },
				{ "travel_distance", std::to_string(pathResult.TravelDistance) },
				{ "weighted_cost", std::to_string(pathResult.WeightedCost) },
				{ "path_found", pathFound ? "true" : "false" },
				{ "predict_respawns_requested", predictRespawns ? "true" : "false" },
				{ "eligible", eligible ? "true" : "false" },
				{ "eligibility_reason", eligibilityReason },
				{ "desire", std::to_string(desire) },
				{ "score", std::to_string(weight) },
				{ "timer_source", isSleeping ? "actor_latent_float_unverified_retail" : "none" }
			});
		}
	}

	if (bestSpot)
	{
		if (BotBenchmark::IsActive())
		{
			BotBenchmark::Emit("inventory_path_choice", {
				{ "seeker", Name.ToString() },
				{ "spot", bestSpot->Name.ToString() },
				{ "item", bestItemName },
				{ "inventory_state", bestInventoryState },
				{ "respawn_remaining_seconds", std::to_string(bestRespawnRemaining) },
				{ "physical_eta_seconds", std::to_string(bestPhysicalETA) },
				{ "eligible", "true" },
				{ "eligibility_reason", bestEligibilityReason },
				{ "desire", std::to_string(bestDesire) },
				{ "travel_distance", std::to_string(bestTravelDistance) },
				{ "weighted_cost", std::to_string(bestWeightedCost) },
				{ "score", std::to_string(bestWeight) },
				{ "weight", std::to_string(bestWeight) },
				{ "timer_source", bestInventoryState == "Sleeping" ? "actor_latent_float_unverified_retail" : "none" },
				{ "predict_respawns_requested", predictRespawns ? "true" : "false" }
			});
		}
		outBestWeight = bestWeight;
		return PathSpecialHandling(bestPath);
	}
	else
	{
		if (BotBenchmark::IsActive())
		{
			BotBenchmark::Emit("inventory_path_choice", {
				{ "seeker", Name.ToString() },
				{ "item", "None" },
				{ "eligible", "false" },
				{ "eligibility_reason", "no_scored_candidate" },
				{ "score", "0.000000" },
				{ "predict_respawns_requested", predictRespawns ? "true" : "false" }
			});
		}
		outBestWeight = 0.0f;
		return SetRouteCache({});
	}
}

void UPawn::InitActorZone()
{
	UActor::InitActorZone();

	FootRegion() = FindRegion({ 0.0f, 0.0f, -CollisionHeight() });
	HeadRegion() = FindRegion({ 0.0f, 0.0f, EyeHeight() });

	if (engine->LaunchInfo.ue1Version > 219 && PlayerReplicationInfo())
		PlayerReplicationInfo()->PlayerZone() = Region().Zone;
}

void UPawn::UpdateActorZone()
{
	UActor::UpdateActorZone();

	PointRegion oldfootregion = FootRegion();
	PointRegion newfootregion = FindRegion({ 0.0f, 0.0f, -CollisionHeight() });
	if (oldfootregion.Zone && oldfootregion.Zone != newfootregion.Zone)
	{
		CallEvent(oldfootregion.Zone, EventName::FootZoneChange, { ExpressionValue::ObjectValue(this) });
		if (newfootregion.Zone && newfootregion.Zone->bPainZone())
		{
			// Pain zones, such as lava and slime, should immediately start hurting the pawn upon entering,
			// so set the pawn's PainTime to something quite low.
			// After that, they'll get DamagePerSec damage each second.
			PainTime() = 0.1f;
		}
	}

	FootRegion() = newfootregion;

	PointRegion oldheadregion = HeadRegion();
	PointRegion newheadregion = FindRegion({ 0.0f, 0.0f, EyeHeight() });
	if (oldheadregion.Zone && oldheadregion.Zone != newheadregion.Zone)
	{
		CallEvent(oldheadregion.Zone, EventName::HeadZoneChange, { ExpressionValue::ObjectValue(this) });

		if (newheadregion.Zone && newheadregion.Zone->bWaterZone() && !newheadregion.Zone->bPainZone())
		{
			// If the new zone is also a pain zone, like lava or slime, then by this point PainTime is already set,
			// so don't set it again. Otherwise, cause the pawn to start drowning in UnderWaterTime seconds.
			PainTime() = UnderWaterTime();
		}
	}

	HeadRegion() = newheadregion;

	if (engine->LaunchInfo.ue1Version > 219 && PlayerReplicationInfo())
		PlayerReplicationInfo()->PlayerZone() = Region().Zone;
}

void UPawn::Tick(float elapsed)
{
	MoveTimer() -= elapsed;

	if (StateFrame)
	{
		if (StateFrame->LatentState == LatentRunState::MoveTo)
		{
			TickRotateTo(Focus());
			if (TickMoveTo(Destination(), elapsed))
				StateFrame->LatentState = LatentRunState::Continue;
		}
		else if (StateFrame->LatentState == LatentRunState::MoveToward)
		{
			if (MoveTarget())
			{
				Focus() = MoveTarget()->Location();
				Destination() = MoveTarget()->Location();
				if (bAdvancedTactics())
				{
					// Stock Botpack.Bot implements AlterDestination to add a
					// temporary lateral offset. Recompute it from the target each poll
					// so UpdateTactics can vary the offset without it accumulating.
					CallEvent(this, EventName::AlterDestination);
					if (bDeleteMe() || !MoveTarget())
					{
						StateFrame->LatentState = LatentRunState::Continue;
						return;
					}
				}
				TickRotateTo(Focus());
				if (TickMoveTo(Destination(), elapsed, MoveTarget()))
					StateFrame->LatentState = LatentRunState::Continue;
			}
			else
			{
				StateFrame->LatentState = LatentRunState::Continue;
			}
		}
		else if (StateFrame->LatentState == LatentRunState::StrafeTo)
		{
			TickRotateTo(Focus());
			if (TickMoveTo(Destination(), elapsed))
				StateFrame->LatentState = LatentRunState::Continue;
		}
		else if (StateFrame->LatentState == LatentRunState::StrafeFacing)
		{
			if (engine->LaunchInfo.ue1Version > 219 && FaceTarget())
			{
				// StrafeFacing moves toward Destination while continuously facing
				// the actor supplied by UnrealScript. Keep Focus synchronized with
				// a moving target instead of rotating toward a stale focus vector.
				Focus() = FaceTarget()->Location();
				TickRotateTo(Focus());
				vec3 oldDest = Destination();
				if (TickMoveTo(Destination(), elapsed))
					StateFrame->LatentState = LatentRunState::Continue;
				Destination() = oldDest;
			}
			else
			{
				StateFrame->LatentState = LatentRunState::Continue;
			}
		}
		else if (StateFrame->LatentState == LatentRunState::TurnTo)
		{
			if (TickRotateTo(Focus()))
				StateFrame->LatentState = LatentRunState::Continue;
		}
		else if (StateFrame->LatentState == LatentRunState::TurnToward)
		{
			if (engine->LaunchInfo.ue1Version > 219 && FaceTarget())
			{
				if (TickRotateTo(FaceTarget()->Location()))
					StateFrame->LatentState = LatentRunState::Continue;
			}
			else
			{
				StateFrame->LatentState = LatentRunState::Continue;
			}
		}
		else if (StateFrame->LatentState == LatentRunState::WaitForLanding)
		{
			if (Physics() != PHYS_Falling)
			{
				StateFrame->LatentState = LatentRunState::Continue;
			}
			else
			{
				// To do: need to send a LongFall event if the fall state lasts long enough
			}
		}
	}

	UActor::Tick(elapsed);

	if (bIsPlayer() && Role() >= ROLE_AutonomousProxy)
	{
		if (engine->LaunchInfo.ue1Version < 400 || bViewTarget())
			CallEvent(this, EventName::UpdateEyeHeight, { ExpressionValue::FloatValue(elapsed) });
		else
			ViewRotation() = Rotation();
	}

	if (Weapon())
	{
		Weapon()->Location() = Location();
		Weapon()->UpdateActorZone();
	}

	if (Role() == ROLE_Authority)
	{
		TickSight(elapsed);
		if (bDeleteMe())
			return;

		if (PainTime() > 0.0f)
		{
			PainTime() = std::max(PainTime() - elapsed, 0.0f);
			if (PainTime() == 0.0f)
				CallEvent(this, EventName::PainTimer);
		}
		if (SpeechTime() > 0.0f)
		{
			SpeechTime() = std::max(SpeechTime() - elapsed, 0.0f);
			if (SpeechTime() == 0.0f)
				CallEvent(this, EventName::SpeechTimer);
		}
		if (engine->LaunchInfo.ue1Version >= 436 && bAdvancedTactics())
			CallEvent(this, EventName::UpdateTactics, { ExpressionValue::FloatValue(elapsed) });
	}
}

void UPawn::TickRotating(float elapsed)
{
	if (Physics() == PHYS_Spider)
		return;

	bRotateToDesired() = true;
	bFixedRotationDir() = false;

	if (Rotation() == DesiredRotation())
		return;

	Rotator rot = Rotation();

	if ((DesiredRotation().Yaw & 0xffff) != (rot.Yaw & 0xffff))
	{
		rot.Yaw = Rotator::TurnToShortest(rot.Yaw, DesiredRotation().Yaw, (int)std::abs(RotationRate().Yaw * elapsed));
	}

	if ((DesiredRotation().Pitch & 0xffff) != (rot.Pitch & 0xffff))
	{
		rot.Pitch = DesiredRotation().Pitch & 0xffff;
		if (rot.Pitch < 0x8000)
		{
			rot.Pitch = std::max(rot.Pitch, RotationRate().Pitch);
		}
		else if (rot.Pitch < 0x10000 - RotationRate().Pitch)
		{
			rot.Pitch = 0x10000 - RotationRate().Pitch;
		}
	}

	// To do: apply RotationRate().Roll

	Rotation() = rot;

	if (Rotation() == DesiredRotation())
	{
		CallEvent(this, EventName::EndedRotation);
	}
}

bool UPawn::TickRotateTo(const vec3& target)
{
	if (Physics() == PHYS_Spider)
		return true;

	DesiredRotation() = Rotator::FromVector(target - Location());

	if (Physics() == PHYS_Walking && (!MoveTarget() || !MoveTarget()->IsA("Pawn")))
	{
		DesiredRotation().Pitch = 0;
	}

	int doneAngle = 2000;
	return (std::abs(DesiredRotation().Yaw - (Rotation().Yaw & 0xffff)) < doneAngle) || (std::abs(DesiredRotation().Yaw - (Rotation().Yaw & 0xffff)) > 0xffff - doneAngle);
}

bool UPawn::TickMoveTo(const vec3& target, float elapsed, UActor* targetActor)
{
	if (MoveTimer() < 0.0f)
	{
		Acceleration() = vec3(0.0f);
		return true;
	}

	auto hasArrived = [&](float distanceSquared, float speedSquared, float acceptanceRadius)
	{
		// Complete only when already touching the goal or close enough to reach
		// it during this physics step. The previous velocity^2 * 0.05 test could
		// stop a 300 uu/s bot roughly 67 units early, causing empty arrivals and
		// route oscillation.
		const float stepDistance = std::sqrt(std::max(speedSquared, 0.0f)) * std::max(elapsed, 0.0f);
		const float threshold = std::max(acceptanceRadius, stepDistance + 1.0f);
		return distanceSquared <= threshold * threshold;
	};

	if (Physics() == PHYS_Walking)
	{
		vec2 delta = target.xy() - Location().xy();
		float distSqr = dot(delta, delta);
		float velocitySqr = dot(Velocity().xy(), Velocity().xy());
		float acceptanceRadius = 1.0f;
		bool targetVerticallyReachable = true;
		if (targetActor)
		{
			const float verticalSeparation = std::abs(targetActor->Location().z - Location().z);
			// Non-colliding navigation actors are points to cross, not cylinders to
			// touch. Keep their arrival envelope aligned with route-cache pruning;
			// otherwise MoveToward can complete outside the radius at which the same
			// node is consumed and UnrealScript will select it again every tick.
			const bool canTouchTarget = targetActor->bCollideActors();
			const float verticalReach = canTouchTarget
				? CollisionHeight() + targetActor->CollisionHeight() + MaxStepHeight()
				: CollisionHeight();
			targetVerticallyReachable = verticalSeparation <= verticalReach;
			if (targetVerticallyReachable)
				acceptanceRadius = canTouchTarget
					? std::max(CollisionRadius() + targetActor->CollisionRadius(), 1.0f)
					: std::max(CollisionRadius(), 1.0f);
		}
		if (targetVerticallyReachable && hasArrived(distSqr, velocitySqr, acceptanceRadius))
		{
			Acceleration() = vec3(0.0f);
			return true;
		}

		Acceleration() = vec3(normalize(delta) * AccelRate(), 0.0f);
	}
	else
	{
		vec3 delta = target - Location();
		float distSqr = dot(delta, delta);
		float velocitySqr = dot(Velocity(), Velocity());
		float acceptanceRadius = targetActor
			? (targetActor->bCollideActors()
				? std::max(CollisionRadius() + targetActor->CollisionRadius(), 1.0f)
				: std::max(CollisionRadius(), 1.0f))
			: 1.0f;
		if (hasArrived(distSqr, velocitySqr, acceptanceRadius))
		{
			Acceleration() = vec3(0.0f);
			return true;
		}

		Acceleration() = normalize(delta) * AccelRate();
	}

	return false;
}

void UPawn::MoveTo(const vec3& newDestination, float speed)
{
	MoveTarget() = nullptr;
	bReducedSpeed() = false;
	DesiredSpeed() = clamp(speed, 0.0f, MaxDesiredSpeed());
	Destination() = newDestination;
	Focus() = newDestination;
	SetMoveDuration(newDestination - Location());
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::MoveTo;
}

void UPawn::MoveToward(UActor* newTarget, float speed)
{
	if (!newTarget)
		return;

	MoveTarget() = newTarget;
	Destination() = newTarget->Location();
	Focus() = newTarget->Location();
	bReducedSpeed() = false;
	DesiredSpeed() = clamp(speed, 0.0f, MaxDesiredSpeed());
	if (UObject::TryCast<UPawn>(newTarget))
		MoveTimer() = 1.0f;
	else
		SetMoveDuration(newTarget->Location() - Location());
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::MoveToward;
}

void UPawn::StrafeFacing(const vec3& newDestination, UActor* newTarget)
{
	if (!newTarget)
		return;

	Destination() = newDestination;
	if (engine->LaunchInfo.ue1Version > 219)
	{
		FaceTarget() = newTarget;
		Focus() = newTarget->Location();
	}
	if (BotBenchmark::IsActive())
	{
		BotBenchmark::Emit("strafe_facing_begin", {
			{ "actor", Name.ToString() },
			{ "target", newTarget->Name.ToString() },
			{ "state", GetStateName().ToString() }
		});
	}
	SetMoveDuration(newDestination - Location());
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::StrafeFacing;
}

void UPawn::StrafeTo(const vec3& newDestination, const vec3& newFocus)
{
	MoveTarget() = nullptr;
	bReducedSpeed() = false;
	DesiredSpeed() = bIsPlayer() ? MaxDesiredSpeed() : 0.8f * MaxDesiredSpeed();
	Destination() = newDestination;
	Focus() = newFocus;
	SetMoveDuration(newDestination - Location());
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::StrafeTo;
}

void UPawn::TurnTo(const vec3& newFocus)
{
	MoveTarget() = nullptr;
	Focus() = newFocus;
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::TurnTo;
}

void UPawn::TurnToward(UActor* newTarget)
{
	if (!newTarget)
		return;

	if (engine->LaunchInfo.ue1Version > 219)
		FaceTarget() = newTarget;
	Focus() = newTarget->Location();
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::TurnToward;
}

void UPawn::WaitForLanding()
{
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::WaitForLanding;
}

void UPawn::SetMoveDuration(const vec3& deltaMove)
{
	float scale = DesiredSpeed() * GetSpeed();
	MoveTimer() = scale > 0.0f ? 1.0f + 1.3f * length(deltaMove) / scale : 0.5f;
}

float UPawn::GetSpeed()
{
	switch (Physics())
	{
	case PHYS_Walking:
	case PHYS_Falling:
	case PHYS_Spider:
		return GroundSpeed();
	case PHYS_Flying:
		return AirSpeed();
	case PHYS_Swimming:
		return WaterSpeed();
	default:
		return 0.0f;
	}
}


/////////////////////////////////////////////////////////////////////////////

bool UPlayerPawn::IsPressing(uint8_t KeyNum)
{
	for (auto& activeButtons : engine->activeInputButtons)
	{
		if (activeButtons.second == KeyNum)
			return true;
	}

	return false;
}

void UPlayerPawn::PausedInput(float elapsed)
{
	if (Role() >= ROLE_SimulatedProxy && Player() && !UObject::TryCast<UCamera>(this))
	{
		CallEvent(this, EventName::PlayerInput, { ExpressionValue::FloatValue(elapsed) });
	}
}

void UPlayerPawn::Tick(float elapsed)
{
	UPawn::Tick(elapsed);

	if (Role() >= ROLE_SimulatedProxy)
	{
		if (Player() && !UObject::TryCast<UCamera>(this))
		{
			CallEvent(this, EventName::PlayerInput, { ExpressionValue::FloatValue(elapsed) });
			CallEvent(this, EventName::PlayerTick, { ExpressionValue::FloatValue(elapsed) });
		}
	}

	// TODO: is this the correct place to set this?
	aForward() = 0.0f;

	// XXX: we reset this here to prevent infinite runaway, which eventually breaks mouselook
	// however, this might break looking with the controller?
	aTurn() = 0.0f;
	aLookUp() = 0.0f;
}

void UPlayerPawn::TickRotating(float elapsed)
{
	if (Physics() == PHYS_Spider)
		return;

	Rotator rot = Rotation();

	// To do: apply RotationRate().Roll

	Rotation() = rot;
}

void UPlayerPawn::LoadProperties()
{
	bInvertMouse() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bInvertMouse", true);
	MouseSensitivity() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "MouseSensitivity", 5.0f);
	// TODO: Handle the array property this class has (WeaponPriority)
	DodgeClickTime() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "DodgeClickTime", 0.25f);
	Bob() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "Bob", 0.016f);
	MyAutoAim() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "MyAutoAim", 1.0f);
	if (!engine->LaunchInfo.IsRune())
		Handedness() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "Handedness", -1.0f);
	bLookUpStairs() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bLookUpStairs", false);
	bSnapToLevel() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bSnapToLevel", false);
	bAlwaysMouseLook() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bAlwaysMouseLook", true);
	bKeyboardLook() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bKeyboardLook", false);
	if (engine->LaunchInfo.ue1Version > 219)
	{
		bMaxMouseSmoothing() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bMaxMouseSmoothing", false);
		bNoFlash() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bNoFlash", false);
		bNoVoices() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bNoVoices", false);
		bMessageBeep() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bMessageBeep", true);
		// NetSpeed is missing
		// LanSpeed is missing
		MouseSmoothThreshold() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "MouseSmoothThreshold", 0.16f);
		ngWorldSecret() = IniPropertyConverter<std::string>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "ngWorldSecret", "");

		// SE addition: Store/Load the DefaultFOV setting into/from the user.ini file as well
		DefaultFOV() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "MainFOV", 90.0f);
	}
}

void UPlayerPawn::SaveConfig()
{
	UPawn::SaveConfig();

	// Note: the code below may no longer be needed. SaveConfig() on UObject saves all unrealscript variables marked as config or globalconfig

	// Not sure why are PlayerPawn's config fields not saved with the base SaveConfig(), but whatever. 
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bInvertMouse", IniPropertyConverter<bool>::ToString(bInvertMouse()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "MouseSensitivity", IniPropertyConverter<float>::ToString(MouseSensitivity()));
	// TODO: Handle the array property this class has (WeaponPriority)
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "DodgeClickTime", IniPropertyConverter<float>::ToString(DodgeClickTime()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "Bob", IniPropertyConverter<float>::ToString(Bob()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "MyAutoAim", IniPropertyConverter<float>::ToString(MyAutoAim()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "Handedness", IniPropertyConverter<float>::ToString(Handedness()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bLookUpStairs", IniPropertyConverter<bool>::ToString(bLookUpStairs()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bSnapToLevel", IniPropertyConverter<bool>::ToString(bSnapToLevel()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bAlwaysMouseLook", IniPropertyConverter<bool>::ToString(bAlwaysMouseLook()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bKeyboardLook", IniPropertyConverter<bool>::ToString(bKeyboardLook()));
	if (engine->LaunchInfo.ue1Version > 219)
	{
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bMaxMouseSmoothing", IniPropertyConverter<bool>::ToString(bMaxMouseSmoothing()));
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bNoFlash", IniPropertyConverter<bool>::ToString(bNoFlash()));
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bNoVoices", IniPropertyConverter<bool>::ToString(bNoVoices()));
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bMessageBeep", IniPropertyConverter<bool>::ToString(bMessageBeep()));
		// NetSpeed is missing
		// LanSpeed is missing
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "MouseSmoothThreshold", IniPropertyConverter<float>::ToString(MouseSmoothThreshold()));
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "ngWorldSecret", ngWorldSecret());

		// SE addition: Store/Load the DefaultFOV setting into/from the user.ini file as well
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "MainFOV", IniPropertyConverter<float>::ToString(DefaultFOV()));
	}
}

/////////////////////////////////////////////////////////////////////////////

void ULevelInfo::UpdateActorZone()
{
	// No zone events are sent by LevelInfo actors
	Region() = FindRegion();
}

PointRegion ULevelInfo::GetLocZone(const vec3& pos)
{
	return XLevel()->Model->FindRegion(pos, Level());
}

/////////////////////////////////////////////////////////////////////////////

UObject* UDecal::AttachDecal(float traceDistance, vec3 decalDir)
{
	if (!Texture())
		return nullptr;

	vec3 traceDirection = -Coords::Rotation(Rotation()).XAxis;

	CollisionHitList hits = XLevel()->Collision.TraceDecal(to_dvec3(Location()), 0.0f, to_dvec3(traceDirection), traceDistance, false);
	if (hits.empty()) return nullptr;

	UModel* model = XLevel()->Model;

	// Do not attempt to create a decal if we hit a surface that's invisible or a fake backdrop
	auto& hit = hits.front();
	if (!hit.Node || (model->Surfaces[hit.Node->Surf].PolyFlags & (PF_FakeBackdrop | PF_Invisible)) != 0)
		return nullptr;

	vec3 N = hit.Normal;
	vec3 pos = Location() + traceDirection * hit.Fraction;

	if (dot(decalDir, decalDir) < 0.01f) // decalDir specifies which direction the decal texture faces. If its zero use a random direction
	{
		vec3 randomDir;
		while (true)
		{
			randomDir = vec3((float)(std::rand() / (double)RAND_MAX), (float)(std::rand() / (double)RAND_MAX), (float)(std::rand() / (double)RAND_MAX)) * 2.0f - 1.0f;
			if (dot(randomDir, randomDir) >= 1.0f)
				break;
		}
		decalDir = randomDir;
	}

	vec3 ydir = -(decalDir - dot(decalDir, N) * N);
	if (dot(ydir, ydir) < 0.01f)
		ydir = normalize(cross(N, std::abs(N.x) > std::abs(N.y) ? vec3(0.0f, 1.0f, 0.0f) : vec3(1.0f, 0.0f, 0.0f)));
	else
		ydir = normalize(ydir);
	vec3 xdir = cross(N, ydir);

	float usize = (float)Texture()->USize();
	float vsize = (float)Texture()->VSize();
	xdir *= DrawScale() * usize * 0.5f;
	ydir *= DrawScale() * vsize * 0.5f;

	static Array<vec3> positions;
	static Array<vec2> uvs;
	static Array<float> edgeDistances;

	// Walk all nodes in the same plane
	BspNode* polynode = hit.NodeHead;
	while (true)
	{
		positions.clear();
		uvs.clear();

		// Place decal on the surface plane
		positions.push_back(pos - xdir - ydir);
		positions.push_back(pos + xdir - ydir);
		positions.push_back(pos + xdir + ydir);
		positions.push_back(pos - xdir + ydir);
		uvs.push_back({ 0.0f, 0.0f });
		uvs.push_back({ usize, 0.0f });
		uvs.push_back({ usize, vsize });
		uvs.push_back({ 0.0f, vsize });

		// Clip to BSP node shape
		int vertCount = (int)positions.size();
		BspVert* v = &model->Vertices[polynode->VertPool];
		for (int j = 0; j < polynode->NumVertices; j++)
		{
			const vec3& edgeStart = model->Points[v[j > 0 ? j - 1 : polynode->NumVertices - 1].Vertex];
			const vec3& edgeEnd = model->Points[v[j].Vertex];
			vec3 planeN = cross(N, edgeEnd - edgeStart); // Note: not normalized as we don't need it
			vec4 plane(planeN, -dot(edgeEnd, planeN));

			// Find vertex distances to edge plane
			edgeDistances.clear();
			for (int i = 0; i < vertCount; i++)
				edgeDistances.push_back(dot(plane, vec4(positions[i], 1.0f)));

			// Insert points at the edge for any line crossing the plane
			for (int i = 0; i < vertCount; i++)
			{
				float dist = edgeDistances[i];
				float distNext = edgeDistances[(i + 1) % vertCount];
				if ((dist > 0.0f && distNext < 0.0f) || (distNext > 0.0f && dist < 0.0f))
				{
					vec3 p = positions[i];
					vec3 pNext = positions[(i + 1) % vertCount];
					vec2 uv = uvs[i];
					vec2 uvNext = uvs[(i + 1) % vertCount];

					// Ray/plane intersection
					float t = -dot(vec4(p, 1.0f), plane) / dot(plane.xyz(), pNext - p);
					vec3 pInsert = mix(p, pNext, t);
					vec2 uvInsert = mix(uv, uvNext, t);

					int insertAt = i + 1;
					positions.insert(positions.begin() + insertAt, pInsert);
					uvs.insert(uvs.begin() + insertAt, uvInsert);
					edgeDistances.insert(edgeDistances.begin() + insertAt, 0.0f);
					vertCount++;
				}
			}

			// Remove points outside
			int i = 0;
			while (i < vertCount)
			{
				if (edgeDistances[i] < 0.0f)
				{
					positions.erase(positions.begin() + i);
					uvs.erase(uvs.begin() + i);
					edgeDistances.erase(edgeDistances.begin() + i);
					vertCount--;
				}
				else
				{
					i++;
				}
			}
		}

		// Add to decals list if we still got anything left to render
		if (!positions.empty())
		{
			LevelDecal leveldecal;
			leveldecal.Decal = this;
			leveldecal.Positions = positions;
			leveldecal.UVs = uvs;
			polynode->Decals.push_back(leveldecal);
			Nodes.push_back(polynode);
		}

		if (polynode->Plane < 0) break;
		polynode = &model->Nodes[polynode->Plane];
	}

	return Level();
}

void UDecal::DetachDecal()
{
	for (BspNode* node : Nodes)
	{
		auto& decals = node->Decals;
		auto it = decals.begin();
		while (it != decals.end())
		{
			auto& leveldecal = *it;
			if (leveldecal.Decal == this)
				it = decals.erase(it);
			else
				++it;
		}
	}
	Nodes.clear();
}

/////////////////////////////////////////////////////////////////////////////

void UWarpZoneInfo::Warp(vec3& Loc, vec3& Vel, Rotator& R)
{
	vec3 origin = WarpCoords().Origin;
	mat3 rotate = WarpCoords().ToMatrix();
	mat3 invrotate = mat3::transpose(rotate);

	// Transform from warp space:
	Loc = (invrotate * Loc) + origin;
	Vel = invrotate * Vel;

	// Rotate the rotator
	Rotator newRotation = Rotator::FromVector(rotate * (Coords::Rotation(R).ToMatrix() * vec4(1.0f, 0.0f, 0.0f, 1.0f)).xyz());
	R.Yaw = newRotation.Yaw;
	R.Pitch = newRotation.Pitch;
}

void UWarpZoneInfo::UnWarp(vec3& Loc, vec3& Vel, Rotator& R)
{
	vec3 origin = WarpCoords().Origin;
	mat3 rotate = WarpCoords().ToMatrix();
	mat3 invrotate = mat3::transpose(rotate);

	// Transform to warp space:
	Loc = rotate * (Loc - origin);
	Vel = rotate * Vel;

	// Rotate the rotator
	Rotator newRotation = Rotator::FromVector(invrotate * (Coords::Rotation(R).ToMatrix() * vec4(1.0f, 0.0f, 0.0f, 1.0f)).xyz());
	R.Yaw = newRotation.Yaw;
	R.Pitch = newRotation.Pitch;
}

/////////////////////////////////////////////////////////////////////////////

void UPakPathNodeIterator::BuildPath(vec3& start, vec3& end)
{
	LogUnimplemented("PathNodeIterator.BuildPath()");
	NodeIndex() = 0;
}

void UPakPathNodeIterator::CheckUPak()
{
	// What does this even check?
}

UNavigationPoint* UPakPathNodeIterator::GetFirst()
{
	LogUnimplemented("PathNodeIterator.GetFirst()");
	//return NodePath().front();
	return nullptr;
}

UNavigationPoint* UPakPathNodeIterator::GetPrevious()
{
	LogUnimplemented("PathNodeIterator.GetPrevious()");
	// if (NodeIndex() > 0)
	// 	NodeIndex()--;
	// return NodePath()[NodeIndex()];
	return nullptr;
}

UNavigationPoint* UPakPathNodeIterator::GetCurrent()
{
	LogUnimplemented("PathNodeIterator.GetCurrent()");
	//return NodePath()[NodeIndex()];
	return nullptr;
}

UNavigationPoint* UPakPathNodeIterator::GetNext()
{
	LogUnimplemented("PathNodeIterator.GetNext()");
	// if (NodeIndex() < NodeCount() - 1)
	// 	NodeIndex()++;
	// return NodePath()[NodeIndex()];
	return nullptr;
}

UNavigationPoint* UPakPathNodeIterator::GetLast()
{
	LogUnimplemented("PathNodeIterator.GetLast()");
	//return NodePath().back();
	return nullptr;
}

UNavigationPoint* UPakPathNodeIterator::GetLastVisible()
{
	LogUnimplemented("PathNodeIterator.GetLastVisible()");
	return nullptr;
}

void UPakPawnPathNodeIterator::SetPawn(UPawn* P)
{
	Pawn() = P;
}

/////////////////////////////////////////////////////////////////////////////

void UPlayerPawnExt::InitRootWindow()
{
	auto dxIni = engine->packages->GetIniFile("System");
	NameString dxRootClassName = dxIni->GetValue("Engine.Engine", "Root", "");
	UClass* cls = engine->packages->FindClass(dxRootClassName);
	if (cls)
	{
		engine->dxRootWindow = UObject::Cast<URootWindow>(engine->packages->GetTransientPackage()->NewObject("dxRootWindow", cls, ObjectFlags::Transient));
		RootWindow() = engine->dxRootWindow;
		engine->dxRootWindow->parentPawn() = this;
		engine->dxRootWindow->bIsVisible() = true;
		engine->dxRootWindow->bIsSensitive() = true;
		engine->dxRootWindow->InitWindow();
	}
}

void UPlayerPawnExt::PreRenderWindows(UCanvas* canvas)
{
	engine->render->PreRenderWindows(canvas);
}

void UPlayerPawnExt::PostRenderWindows(UCanvas* canvas)
{
	engine->render->PostRenderWindows(canvas);
}

////////////////////////////////////////////////////////

void UDeusExPlayer::ConBindEvents()
{
	DeusExConBindEvents();
}

UObject* UDeusExPlayer::CreateDataVaultImageNoteObject()
{
	auto cls = engine->packages->FindClass("DeusEx.DataVaultImageNote");
	return engine->packages->GetTransientPackage()->NewObject("DataVaultImageNote", cls, ObjectFlags::Transient);
}

UObject* UDeusExPlayer::CreateDumpLocationObject()
{
	auto cls = engine->packages->FindClass("DeusEx.DumpLocation");
	return engine->packages->GetTransientPackage()->NewObject("DumpLocation", cls, ObjectFlags::Transient);
}

UObject* UDeusExPlayer::CreateGameDirectoryObject()
{
	if (!m_GameDirectory)
	{
		auto cls = engine->packages->FindClass("DeusEx.GameDirectory");
		m_GameDirectory = Cast<UDXGameDirectory>(engine->packages->GetTransientPackage()->NewObject("GameDirectory", cls, ObjectFlags::Transient));
	}

	return m_GameDirectory;
}

UObject* UDeusExPlayer::CreateHistoryEvent()
{
	auto cls = engine->packages->FindClass("ConSys.ConHistoryEvent");
	return engine->packages->GetTransientPackage()->NewObject("ConHistoryEvent", cls, ObjectFlags::Transient);
}

UObject* UDeusExPlayer::CreateHistoryObject()
{
	auto cls = engine->packages->FindClass("ConSys.ConHistory");
	return Cast<UConHistory>(engine->packages->GetTransientPackage()->NewObject("ConHistory", cls, ObjectFlags::Transient));
}

UObject* UDeusExPlayer::CreateLogObject()
{
	auto cls = engine->packages->FindClass("DeusEx.DeusExLog");
	return engine->packages->GetTransientPackage()->NewObject("DeusExLog", cls, ObjectFlags::Transient);
}

void UDeusExPlayer::DeleteSaveGameFiles(std::optional<std::string> saveDirectory)
{
	LogUnimplemented("DeusExPlayer.DeleteSaveGameFiles");
}

std::string UDeusExPlayer::GetDeusExVersion()
{
	return "1.112fm. Surreal Engine Edition!";
}

void UDeusExPlayer::SaveGame(int saveIndex, std::optional<std::string> saveDesc)
{
	engine->SaveGameInfo.SaveGameSlot = saveIndex;
	engine->SaveGameInfo.SaveGameDescription = *saveDesc;
}

NameString UDeusExPlayer::SetBoolFlagFromString(const std::string& flagNameString, bool bValue)
{
	// Not called directly from script
	LogUnimplemented("DeusExPlayer.SetBoolFlagFromString");
	return {};
}

void UDeusExPlayer::UnloadTexture(UObject* Texture)
{
	// Nothing going on here because SE never unloads textures atm. This is just here so it doesn't throw LogUnimplemented.
}

////////////////////////////////////////////////////////

void UScriptedPawn::AddCarcass(const NameString& CarcassName)
{
	if (NumCarcasses() < 4)
	{
		bool carcassSeen = HaveSeenCarcass(CarcassName);
		if (carcassSeen == false)
		{
			Carcasses()[NumCarcasses()] = CarcassName;
			NumCarcasses() = NumCarcasses() + 1;
		}
	}
}

void UScriptedPawn::ConBindEvents()
{
	DeusExConBindEvents();
}

uint8_t UScriptedPawn::GetAllianceType(const NameString& AllianceName)
{
	auto alliex = AlliancesEx();
	EAllianceType result = EAllianceType::ALLIANCE_Neutral;
	for (int i = 0; i < 16; i++)
	{
		if (alliex[i].AllianceName == AllianceName)
		{
			if ((alliex[i].AllianceLevel < 0.0) || (alliex[i].AllianceAgitation >= 1.0))
			{
				result = EAllianceType::ALLIANCE_Hostile;
			}
			else if (alliex[i].AllianceLevel > 0.0)
			{
				result = EAllianceType::ALLIANCE_Friendly;
			}
			break;
		}
	}

	if (bLikesNeutral() && (result == EAllianceType::ALLIANCE_Neutral))
	{
		result = EAllianceType::ALLIANCE_Friendly;
	}
	if (bReverseAlliances())
	{
		if (result == EAllianceType::ALLIANCE_Friendly)
		{
			return (uint8_t)EAllianceType::ALLIANCE_Hostile;
		}
		if (result == EAllianceType::ALLIANCE_Hostile)
		{
			return (uint8_t)EAllianceType::ALLIANCE_Friendly;
		}
	}
	return (uint8_t)result;
}

uint8_t UScriptedPawn::GetPawnAllianceType(UPawn* QueryPawn)
{
	if (UScriptedPawn* qp = UObject::TryCast<UScriptedPawn>(QueryPawn))
	{
		uint8_t othersAlliance = qp->GetAllianceType(Alliance());
		if (othersAlliance == (uint8_t)EAllianceType::ALLIANCE_Hostile)
		{
			return (uint8_t)EAllianceType::ALLIANCE_Hostile;
		}
	}
	return GetAllianceType(QueryPawn->Alliance());
}

bool UScriptedPawn::HaveSeenCarcass(const NameString& CarcassName)
{
	for (int i = 0; i < NumCarcasses(); i++)
	{
		if (Carcasses()[i] == CarcassName)
		{
			return true;
		}
	}
	return false;
}

bool UScriptedPawn::IsValidEnemy(UPawn* TestEnemy, std::optional<bool> bCheckAlliance)
{
	if(!UObject::TryCast<UScriptedPawn>(TestEnemy) || TestEnemy == this || !bBlockSight() || bDeleteMe() || UObject::TryCast<UScriptedPawn>(TestEnemy)->KillCount() < 1)
		return false;
	if (bCheckAlliance)
	{
		uint8_t retval = GetPawnAllianceType(TestEnemy);
		if (retval != (uint8_t)EAllianceType::ALLIANCE_Hostile)
		{
			return false;
		}
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////

void UDeusExDecoration::ConBindEvents()
{
	DeusExConBindEvents();
}
