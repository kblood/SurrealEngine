#pragma once

#include "PropertyOffsets.h"
#include "UDXGameDirectory.h"
#include "UObject.h"
#include "UnrealURL.h"
#include "Math/bbox.h"
#include "PawnFailedNavigationMemory.h"
#include "PawnFallingTwoPlaneSafety.h"
#include "PawnMoveStallWatchdog.h"
#include "PawnPainLedgeRecovery.h"
#include "PawnLedgeTransition.h"
#include "PawnFallingParityRealizedTrace.h"
#include "PawnFallingHazardRuntimeObserver.h"
#include "PawnHazardWaterEgressObserver.h"
#include "PawnWalkingStepPreflight.h"
#include "PawnInventoryReachability.h"
#include "PawnRoutePathCommitProvenance.h"
#include "PawnWalkingHitWallDispatch.h"
#include "PawnWallAdjustRecovery.h"
#include "BotAI/HarmfulZoneEscapeGate.h"
#include "BotAI/FallingHazardRecoveryGate.h"
#include "BotAI/HazardSwimEgressGate.h"
#include "BotAI/HazardSwimEgressLiveSteer.h"
#include "PawnHazardResidenceObserver.h"
#include "PawnExternalImpulseFallWitness.h"

class UTexture;
class UPrimitive;
class UMesh;
class UModel;
class USound;
class UPawn;
class UInventory;
class ULevelInfo;
class UAnimation;
class UPlayer;

enum MoveCallbackEvidenceBits : uint32_t
{
	MoveCallbackBasedActor = 1u << 0,
	MoveCallbackEncroachment = 1u << 1,
	MoveCallbackBump = 1u << 2,
	MoveCallbackTouch = 1u << 3,
	MoveCallbackUnTouch = 1u << 4,
	MoveCallbackRegionChange = 1u << 5,
	MoveCallbackFootRegionChange = 1u << 6,
	MoveCallbackHeadRegionChange = 1u << 7,
	MoveCallbackHitWall = 1u << 8
};

struct MoveCallbackEvidence
{
	uint32_t Mask = 0;
	bool Any() const { return Mask != 0; }
};

class UMusic;
class UGameReplicationInfo;
class UPlayerReplicationInfo;
class UMutator;
class UMenu;
class UPlayerPawn;
class UStatLog;
class USkyZoneInfo;
class ULevelSummary;
class ULevel;
class UInventorySpot;
class UMover;
class UTrigger;
class UWarpZoneInfo;
class UZoneInfo;
class URootWindow;
class UCanvas;
class UFlagBase;
class UViewport;
class PackageManager;
class CollisionHit;
class BspNode;
class CollisionHitList;
struct MeshAnimSeq;
struct XAIParams; // Deus Ex
class UDynamicZoneInfo; // Unreal 227
class U227AnimationNotify;
class U227Projector;
class U227SkeletalMeshInstance;

struct PawnPathEndPointResult
{
	Array<class UNavigationPoint*> Points;
	Array<int32_t> EdgeReachSpecIndexes;
	int32_t RawEndpointCost = 0;
	int32_t AdjustedEndpointCost = 0;
	uint32_t FailedNavigationPenaltyApplications = 0;
};

struct PointRegion
{
	UZoneInfo* Zone;
	int BspLeaf;
	uint8_t ZoneNumber;
};

enum EPhysics
{
	PHYS_None,
	PHYS_Walking,
	PHYS_Falling,
	PHYS_Swimming,
	PHYS_Flying,
	PHYS_Rotating,
	PHYS_Projectile,
	PHYS_Rolling,
	PHYS_Interpolating,
	PHYS_MovingBrush,
	PHYS_Spider,
	PHYS_Trailer
};

enum ENetRole
{
	ROLE_None,
	ROLE_DumbProxy,
	ROLE_SimulatedProxy,
	ROLE_AutonomousProxy,
	ROLE_Authority,
};

enum ESoundSlot
{
	SLOT_None,
	SLOT_Misc,
	SLOT_Pain,
	SLOT_Interact,
	SLOT_Ambient,
	SLOT_Talk,
	SLOT_Interface
};

enum EMusicTransition
{
	MTRAN_None,
	MTRAN_Instant,
	MTRAN_Segue,
	MTRAN_Fade,
	MTRAN_FastFade,
	MTRAN_SlowFade
};

enum ENetMode
{
	NM_Standalone,
	NM_DedicatedServer,
	NM_ListenServer,
	NM_Client
};

enum ELightType
{
	LT_None,
	LT_Steady,
	LT_Pulse,
	LT_Blink,
	LT_Flicker,
	LT_Strobe,
	LT_BackdropLight,
	LT_SubtlePulse,
	LT_TexturePaletteOnce,
	LT_TexturePaletteLoop
};

enum ELightEffect
{
	LE_None,
	LE_TorchWaver,
	LE_FireWaver,
	LE_WateryShimmer,
	LE_Searchlight,
	LE_SlowWave,
	LE_FastWave,
	LE_CloudCast,
	LE_StaticSpot,
	LE_Shock,
	LE_Disco,
	LE_Warp,
	LE_Spotlight,
	LE_NonIncidence,
	LE_Shell,
	LE_OmniBumpMap,
	LE_Interference,
	LE_Cylinder,
	LE_Rotor,
	LE_Unused
};

enum EDrawType
{
	DT_None,
	DT_Sprite,
	DT_Mesh,
	DT_Brush,
	DT_RopeSprite,
	DT_VerticalSprite,
	DT_Terraform,
	DT_SpriteAnimOnce
};

enum ERenderStyle
{
	STY_None,
	STY_Normal,
	STY_Masked,
	STY_Translucent,
	STY_Modulated,
	STY_AlphaBlend
};

enum class ETravelType : uint8_t
{
	TRAVEL_Absolute, // Absolute URL
	TRAVEL_Partial,  // Partial (Carry name, reset server)
	TRAVEL_Relative  // Relative URL
};

// Unreal 227 only
// 227's Pawn class has a SightCheckType variable that will affect LineOfSightTo() function
enum class EPawnSightCheck : uint8_t
{
	SEE_PlayersOnly, // See bIsPlayer pawns only
	SEE_All,		 // See all pawns
	SEE_None		 // Do not do sight checks
};

// Deus Ex
enum class EAIEventState : uint8_t
{
	EAISTATE_Begin,
	EAISTATE_End,
	EAISTATE_Pulse,
	EAISTATE_ChangeBest
};

// Deus Ex
enum class EAIEventType : uint8_t
{
	EAITYPE_Visual,
	EAITYPE_Audio,
	EAITYPE_Olifactory
};

// Deus Ex
enum class EAllianceType : uint8_t
{
	ALLIANCE_Friendly,
	ALLIANCE_Neutral,
	ALLIANCE_Hostile
};

// Deus Ex
enum class EBarkModes : uint8_t
{
	BM_Idle,
	BM_CriticalDamage,
	BM_AreaSecure,
	BM_TargetAcquired,
	BM_TargetLost,
	BM_GoingForAlarm,
	BM_OutOfAmmo,
	BM_Scanning,
	BM_Futz,
	BM_OnFire,
	BM_TearGas,
	BM_Gore,
	BM_Surprise,
	BM_PreAttackSearching,
	BM_PreAttackSighting,
	BM_PostAttackSearching,
	BM_SearchGiveUp,
	BM_AllianceHostile,
	BM_AllianceFriendly
};

// Deus Ex (?)
enum class EDodgeDir : uint8_t
{
	DODGE_None,
	DODGE_Left,
	DODGE_Right,
	DODGE_Forward,
	DODGE_Back,
	DODGE_Active,
	DODGE_Done
};

// Unreal 227
enum class EAmbients : uint8_t
{
	REVERB_PRESET_GENERIC,
	REVERB_PRESET_PADDEDCELL,
	REVERB_PRESET_ROOM,
	REVERB_PRESET_BATHROOM,
	REVERB_PRESET_LIVINGROOM,
	REVERB_PRESET_STONEROOM,
	REVERB_PRESET_AUDITORIUM,
	REVERB_PRESET_CONCERTHALL,
	REVERB_PRESET_CAVE,
	REVERB_PRESET_ARENA,
	REVERB_PRESET_HANGAR,
	REVERB_PRESET_CARPETTEDHALLWAY,
	REVERB_PRESET_HALLWAY,
	REVERB_PRESET_STONECORRIDOR,
	REVERB_PRESET_ALLEY,
	REVERB_PRESET_FOREST,
	REVERB_PRESET_CITY,
	REVERB_PRESET_MOUNTAINS,
	REVERB_PRESET_QUARRY,
	REVERB_PRESET_PLAIN,
	REVERB_PRESET_PARKINGLOT,
	REVERB_PRESET_SEWERPIPE,
	REVERB_PRESET_UNDERWATER,
	REVERB_PRESET_DRUGGED,
	REVERB_PRESET_DIZZY,
	REVERB_PRESET_PSYCHOTIC,
	REVERB_PRESET_CASTLE_SMALLROOM,
	REVERB_PRESET_CASTLE_SHORTPASSAGE,
	REVERB_PRESET_CASTLE_MEDIUMROOM,
	REVERB_PRESET_CASTLE_LONGPASSAGE,
	REVERB_PRESET_CASTLE_LARGEROOM,
	REVERB_PRESET_CASTLE_HALL,
	REVERB_PRESET_CASTLE_CUPBOARD,
	REVERB_PRESET_CASTLE_COURTYARD,
	REVERB_PRESET_CASTLE_ALCOVE,
	REVERB_PRESET_FACTORY_ALCOVE,
	REVERB_PRESET_FACTORY_SHORTPASSAGE,
	REVERB_PRESET_FACTORY_MEDIUMROOM,
	REVERB_PRESET_FACTORY_LONGPASSAGE,
	REVERB_PRESET_FACTORY_LARGEROOM,
	REVERB_PRESET_FACTORY_HALL,
	REVERB_PRESET_FACTORY_CUPBOARD,
	REVERB_PRESET_FACTORY_COURTYARD,
	REVERB_PRESET_FACTORY_SMALLROOM,
	REVERB_PRESET_ICEPALACE_ALCOVE,
	REVERB_PRESET_ICEPALACE_SHORTPASSAGE,
	REVERB_PRESET_ICEPALACE_MEDIUMROOM,
	REVERB_PRESET_ICEPALACE_LONGPASSAGE,
	REVERB_PRESET_ICEPALACE_LARGEROOM,
	REVERB_PRESET_ICEPALACE_HALL,
	REVERB_PRESET_ICEPALACE_CUPBOARD,
	REVERB_PRESET_ICEPALACE_COURTYARD,
	REVERB_PRESET_ICEPALACE_SMALLROOM,
	REVERB_PRESET_SPACESTATION_ALCOVE,
	REVERB_PRESET_SPACESTATION_MEDIUMROOM,
	REVERB_PRESET_SPACESTATION_SHORTPASSAGE,
	REVERB_PRESET_SPACESTATION_LONGPASSAGE,
	REVERB_PRESET_SPACESTATION_LARGEROOM,
	REVERB_PRESET_SPACESTATION_HALL,
	REVERB_PRESET_SPACESTATION_CUPBOARD,
	REVERB_PRESET_SPACESTATION_SMALLROOM,
	REVERB_PRESET_WOODEN_ALCOVE,
	REVERB_PRESET_WOODEN_SHORTPASSAGE,
	REVERB_PRESET_WOODEN_MEDIUMROOM,
	REVERB_PRESET_WOODEN_LONGPASSAGE,
	REVERB_PRESET_WOODEN_LARGEROOM,
	REVERB_PRESET_WOODEN_HALL,
	REVERB_PRESET_WOODEN_CUPBOARD,
	REVERB_PRESET_WOODEN_SMALLROOM,
	REVERB_PRESET_WOODEN_COURTYARD,
	REVERB_PRESET_SPORT_EMPTYSTADIUM,
	REVERB_PRESET_SPORT_SQUASHCOURT,
	REVERB_PRESET_SPORT_SMALLSWIMMINGPOOL,
	REVERB_PRESET_SPORT_LARGESWIMMINGPOOL,
	REVERB_PRESET_SPORT_GYMNASIUM,
	REVERB_PRESET_SPORT_FULLSTADIUM,
	REVERB_PRESET_SPORT_STADIUMTANNOY,
	REVERB_PRESET_PREFAB_WORKSHOP,
	REVERB_PRESET_PREFAB_SCHOOLROOM,
	REVERB_PRESET_PREFAB_PRACTISEROOM,
	REVERB_PRESET_PREFAB_OUTHOUSE,
	REVERB_PRESET_PREFAB_CARAVAN,
	REVERB_PRESET_DOME_TOMB,
	REVERB_PRESET_PIPE_SMALL,
	REVERB_PRESET_DOME_SAINTPAULS,
	REVERB_PRESET_PIPE_LONGTHIN,
	REVERB_PRESET_PIPE_LARGE,
	REVERB_PRESET_PIPE_RESONANT,
	REVERB_PRESET_OUTDOORS_BACKYARD,
	REVERB_PRESET_OUTDOORS_ROLLINGPLAINS,
	REVERB_PRESET_OUTDOORS_DEEPCANYON,
	REVERB_PRESET_OUTDOORS_CREEK,
	REVERB_PRESET_OUTDOORS_VALLEY,
	REVERB_PRESET_MOOD_HEAVEN,
	REVERB_PRESET_MOOD_HELL,
	REVERB_PRESET_MOOD_MEMORY,
	REVERB_PRESET_DRIVING_COMMENTATOR,
	REVERB_PRESET_DRIVING_PITGARAGE,
	REVERB_PRESET_DRIVING_INCAR_RACER,
	REVERB_PRESET_DRIVING_INCAR_SPORTS,
	REVERB_PRESET_DRIVING_INCAR_LUXURY,
	REVERB_PRESET_DRIVING_FULLGRANDSTAND,
	REVERB_PRESET_DRIVING_EMPTYGRANDSTAND,
	REVERB_PRESET_DRIVING_TUNNEL,
	REVERB_PRESET_CITY_STREETS,
	REVERB_PRESET_CITY_SUBWAY,
	REVERB_PRESET_CITY_MUSEUM,
	REVERB_PRESET_CITY_LIBRARY,
	REVERB_PRESET_CITY_UNDERPASS,
	REVERB_PRESET_CITY_ABANDONED,
	REVERB_PRESET_DUSTYROOM,
	REVERB_PRESET_CHAPEL,
	REVERB_PRESET_SMALLWATERROOM,
	REVERB_PRESET_UNDERSLIME,
	REVERB_PRESET_NONE
};

// Unreal 227
enum class EDynZoneInfoType : uint8_t
{
	DZONE_Cube,
	DZONE_Sphere,
	DZONE_Cylinder,
	DZONE_Script
};

// Unreal 227
struct sAnimNotify
{
	NameString AnimName;
	NameString FunctionName;
	int KeyFrame;
	eAnimNotifyEval NotifyEval;
	bool bCallOncePerLoop;
	bool bCalculatedFrame;
	bool bAlreadyCalled;
	int NumFrames;
	float CallKey;
};

// UT 469
enum FontFamily
{
	FF_Arial,	// Arial on Windows, Helvetica on Linux/Mac
	FF_Times,	// Times New Roman on Windows, times on Linux/Mac
	FF_Courier, // Courier New on Windows, courier on Linux/Mac
	FF_Tahoma   // Standard UWindow font, Linux/Mac will use Verdana if requested.
};

enum class EAnimType : uint8_t
{
	AT_Replace,
	AT_Combine,
};

class UActor : public UObject
{
public:
	using UObject::UObject;

	UActor* Spawn(UClass* SpawnClass, std::optional<UActor*> SpawnOwner, std::optional<NameString> SpawnTag, std::optional<vec3> SpawnLocation, std::optional<Rotator> SpawnRotation);
	bool Destroy();
	void InitBase();

	void SetBase(UActor* newBase, bool sendBaseChangeEvent);
	// Re-registers this actor into ActorBase()->BasedActors without firing Attach/BaseChange
	// events. BasedActors is native runtime-only state (never serialized), unlike ActorBase()
	// itself, so a freshly loaded actor whose ActorBase() property already points at another
	// actor needs this to make that relationship work in both directions again.
	void RelinkBasedActor();
	void SetOwner(UActor* newOwner);
	virtual void InitActorZone();
	virtual void UpdateActorZone();
	PointRegion FindRegion(const vec3& offset = vec3(0.0f));

	virtual void Tick(float elapsed);

	void TickAnimation(float elapsed);
	void TickBlendAnimation(float elapsed);

	void TickPhysics(float elapsed);
	void TickWalking(float elapsed);
	void TickFalling(float elapsed);
	void TickSwimming(float elapsed);
	void TickFlying(float elapsed);
	void TickProjectile(float elapsed);
	void TickRolling(float elapsed);
	void TickInterpolating(float elapsed);
	void TickMovingBrush(float elapsed);
	void TickSpider(float elapsed);
	void TickTrailer(float elapsed);

	void PhysLanded(UActor* hitActor, const vec3& hitNormal);

	virtual void TickRotating(float elapsed);

	void SetPhysics(uint8_t newPhysics);
	void SetCollision(bool newColActors, bool newBlockActors, bool newBlockPlayers);

	std::pair<bool, vec3> CheckLocation(vec3 location, float radius, float height, bool check);

	bool SetLocation(const vec3& newLocation);
	bool SetRotation(const Rotator& newRotation);
	bool SetCollisionSize(float newRadius, float newHeight);

	UObject* Trace(vec3& hitLocation, vec3& hitNormal, const vec3& traceEnd, const vec3& traceStart, bool bTraceActors, const vec3& extent);
	// Unreal 227's version of Actor.Trace()
	UObject* Trace(vec3& hitLocation, vec3& hitNormal, const vec3& traceEnd, const vec3& traceStart, bool bTraceActors, const vec3& extent, bool bTraceBSP, uint8_t	BSPTraceFlags);
	bool FastTrace(const vec3& traceEnd, const vec3& traceStart);
	// Unreal 227 - Trace against world and return the wanted information (location, normal, texture and/or polyflags)
	bool TraceSurfHitInfo(vec3& Start, vec3& End, vec3* HitLocation, vec3* HitNormal, UTexture* HitTex, int* HitFlags);
	// Unreal 227 - Perform a single line check with this actor
	bool TraceThisActor(vec3& TraceEnd, vec3 TraceStart, vec3* HitLocation, vec3* HitNormal, std::optional<vec3> Extent);

	CollisionHit ProbeMoveCollision(const vec3& origin, const vec3& delta,
		bool isOwnBaseBlocking = true, CollisionHitList* tracedHits = nullptr);
	CollisionHit TryMove(const vec3 & delta, bool dryRun = false,
		bool isOwnBaseBlocking = true, MoveCallbackEvidence* callbackEvidence = nullptr);
	CollisionHit TryMoveSmooth(const vec3& delta);
	bool Move(const vec3& delta);
	bool MoveSmooth(const vec3& delta);

	bool IsBasedOn(UActor* other);
	bool IsOwnedBy(UActor* owner);
	bool IsOverlapping(UActor* other);

	void Touch(UActor* actor);
	void UnTouch(UActor* actor);

	static const int TouchingArraySize = 4;
	bool TouchEventSent[TouchingArraySize] = {};

	bool HasAnim(const NameString& sequence);
	bool IsAnimating();
	bool IsAnimating_HP(std::optional<NameString> RootBone);
	void FinishAnim();
	void FinishAnim_HP(std::optional<NameString> RootBone);
	NameString GetAnimGroup(const NameString& sequence);
	void PlayAnim(const NameString& sequence, float rate, float tweenTime);
	void PlayBlendAnim(const NameString& sequence, float rate, float tweenTime, int blendSlot);
	void LoopAnim(const NameString& sequence, float rate, float tweenTime, float minRate);
	void TweenAnim(const NameString& sequence, float tweenTime);

	void MakeNoise(float loudness);
	bool PlayerCanSeeMe();

	// Harry Potter
	void PlayAnim_HP(const NameString& Sequence, std::optional<float> Rate, std::optional<float> TweenTime, std::optional<EAnimType> Type, std::optional<NameString> RootBone);
	void LoopAnim_HP(const NameString& Sequence, std::optional<float> Rate, std::optional<float> TweenTime, std::optional<float> MinRate, std::optional<EAnimType> Type, std::optional<NameString> RootBone);
	BoundingBox GetWorldCollisionBox(bool bVisual);
	vec3 GetRenderExtent();
	UActor* CreateAnimChannel(UClass* NewClass, EAnimType Type, const NameString& RootBone, bool bTransient);
	int BoneNumber(const NameString& Bone);
	NameString BoneName(int Bone);
	vec3 BonePos(const NameString& Bone);
	UTexture* CreateTextureFromScreenShot(UViewport* vport);
	UTexture* CreateTextureFromBMP(const std::string& name, const std::string& filename);
	bool SaveObjectAsFile(const std::string& dir, UObject* object);
	bool LoadObjectAsFile(const std::string& dir, UObject* object);
	bool SaveGameSaveInfo(const std::string& dir, UObject* object);
	bool LoadGameSaveInfo(const std::string& dir, UObject* object);
	bool IsOSVer2kOrXP();

	void UpdateBspInfo();
	void AddToBspNode(BspNode* node);
	void RemoveFromBspNode();
	static int NodeAABBOverlap(const vec3& center, const vec3& extents, BspNode* node);

	// The status of the actor in the collision hash
	struct
	{
		bool Inserted = false;
		vec3 Location = { 0.0f };
		vec3 Extents = { 0.0f };
		int CheckCounter = -1;
	} Collision;

	// The status of the actor in the light hash
	struct
	{
		bool Inserted = false;
		vec3 Location = { 0.0f };
		float Radius = 0.0f;
		int CheckCounter = -1;
	} Light;

	// Lights touching this actor
	struct
	{
		bool NeedsUpdate = true;
		vec3 Location = vec3(0.0f);
		Array<UActor*> LightList;
	} LightInfo;

	// Fog between actor and camera
	struct
	{
		vec3 fogcolor = { 0.0f };
		float brightness = -1.0f;
		float fog = -1.0f;
		float radius = -1.0f;
		vec3 location = { 0.0f };
	} FogInfo;

	// Location in the BSP tree
	struct
	{
		BBox BoundingBox;
		BspNode* Node = nullptr;
		UActor* Prev = nullptr;
		UActor* Next = nullptr;
	} BspInfo;

	// Tweening animation state
	struct
	{
		int V0 = 0;
		int V1 = 0;
		float T = -1.0f;
	} TweenFromAnimFrame;

	int LastDrawFrame = -1;

	float SleepTimeLeft = 0.0f;
	vec3 gravityVector;

	// Index in level Actors array
	int Index = -1;

	// Child actor tracking
	Array<UActor*> ChildActors;
	// Based actor tracking
	Array<UActor*> BasedActors;

	void AddChildActor(UActor* actor);
	void RemoveChildActor(UActor* actor);

	void SetTweenFromAnimFrame();

	UTexture* GetMultiskin(int index);

	void DeusExConBindEvents();

	float WorldSoundRadius() { return ((int)SoundRadius() + 1) * 25.0f; }
	float WorldVolumetricRadius() { return ((int)VolumeRadius() + 1) * 25.0f; }
	float WorldLightRadius() { return ((int)LightRadius() + 1) * 25.0f; }

	vec3& Acceleration() { return Value<vec3>(PropOffsets_Actor.Acceleration); }
	uint8_t& AmbientGlow() { return Value<uint8_t>(PropOffsets_Actor.AmbientGlow); }
	USound*& AmbientSound() { return Value<USound*>(PropOffsets_Actor.AmbientSound); }
	float& AnimFrame() { return Value<float>(PropOffsets_Actor.AnimFrame); }
	float& AnimLast() { return Value<float>(PropOffsets_Actor.AnimLast); }
	float& AnimMinRate() { return Value<float>(PropOffsets_Actor.AnimMinRate); }
	float& AnimRate() { return Value<float>(PropOffsets_Actor.AnimRate); }
	NameString& AnimSequence() { return Value<NameString>(PropOffsets_Actor.AnimSequence); }
	NameString& AttachTag() { return Value<NameString>(PropOffsets_Actor.AttachTag); }
	UActor*& ActorBase() { return Value<UActor*>(PropOffsets_Actor.Base); }
	UModel*& Brush() { return Value<UModel*>(PropOffsets_Actor.Brush); }
	float& Buoyancy() { return Value<float>(PropOffsets_Actor.Buoyancy); }
	vec3& ColLocation() { return Value<vec3>(PropOffsets_Actor.ColLocation); }
	float& CollisionHeight() { return Value<float>(PropOffsets_Actor.CollisionHeight); }
	float& CollisionRadius() { return Value<float>(PropOffsets_Actor.CollisionRadius); }
	int& CollisionTag() { return Value<int>(PropOffsets_Actor.CollisionTag); }
	UActor*& Deleted() { return Value<UActor*>(PropOffsets_Actor.Deleted); }
	Rotator& DesiredRotation() { return Value<Rotator>(PropOffsets_Actor.DesiredRotation); }
	uint8_t& DodgeDir() { return Value<uint8_t>(PropOffsets_Actor.DodgeDir); }
	float& DrawScale() { return Value<float>(PropOffsets_Actor.DrawScale); }
	uint8_t& DrawType() { return Value<uint8_t>(PropOffsets_Actor.DrawType); }
	NameString& Event() { return Value<NameString>(PropOffsets_Actor.Event); }
	int& ExtraTag() { return Value<int>(PropOffsets_Actor.ExtraTag); }
	uint8_t& Fatness() { return Value<uint8_t>(PropOffsets_Actor.Fatness); }
	NameString& Group() { return Value<NameString>(PropOffsets_Actor.Group); }
	UActor*& HitActor() { return Value<UActor*>(PropOffsets_Actor.HitActor); }
	NameString& InitialState() { return Value<NameString>(PropOffsets_Actor.InitialState); }
	UPawn*& Instigator() { return Value<UPawn*>(PropOffsets_Actor.Instigator); }
	UInventory*& Inventory() { return Value<UInventory*>(PropOffsets_Actor.Inventory); }
	float& LODBias() { return Value<float>(PropOffsets_Actor.LODBias); }
	UActor*& LatentActor() { return Value<UActor*>(PropOffsets_Actor.LatentActor); }
	uint8_t& LatentByte() { return Value<uint8_t>(PropOffsets_Actor.LatentByte); }
	float& LatentFloat() { return Value<float>(PropOffsets_Actor.LatentFloat); }
	int& LatentInt() { return Value<int>(PropOffsets_Actor.LatentInt); }
	ULevelInfo*& Level() { return Value<ULevelInfo*>(PropOffsets_Actor.Level); }
	float& LifeSpan() { return Value<float>(PropOffsets_Actor.LifeSpan); }
	uint8_t& LightBrightness() { return Value<uint8_t>(PropOffsets_Actor.LightBrightness); }
	uint8_t& LightCone() { return Value<uint8_t>(PropOffsets_Actor.LightCone); }
	uint8_t& LightEffect() { return Value<uint8_t>(PropOffsets_Actor.LightEffect); }
	uint8_t& LightHue() { return Value<uint8_t>(PropOffsets_Actor.LightHue); }
	uint8_t& LightPeriod() { return Value<uint8_t>(PropOffsets_Actor.LightPeriod); }
	uint8_t& LightPhase() { return Value<uint8_t>(PropOffsets_Actor.LightPhase); }
	uint8_t& LightRadius() { return Value<uint8_t>(PropOffsets_Actor.LightRadius); }
	uint8_t& LightSaturation() { return Value<uint8_t>(PropOffsets_Actor.LightSaturation); }
	uint8_t& LightType() { return Value<uint8_t>(PropOffsets_Actor.LightType); }
	int& LightingTag() { return Value<int>(PropOffsets_Actor.LightingTag); }
	vec3& Location() { return Value<vec3>(PropOffsets_Actor.Location); }
	float& Mass() { return Value<float>(PropOffsets_Actor.Mass); }
	UMesh*& Mesh() { return Value<UMesh*>(PropOffsets_Actor.Mesh); }
	uint8_t& MiscNumber() { return Value<uint8_t>(PropOffsets_Actor.MiscNumber); }
	FixedArrayView<UTexture*, 8> MultiSkins() { return FixedArray<UTexture*, 8>(PropOffsets_Actor.MultiSkins); }
	float& NetPriority() { return Value<float>(PropOffsets_Actor.NetPriority); }
	int& NetTag() { return Value<int>(PropOffsets_Actor.NetTag); }
	float& NetUpdateFrequency() { return Value<float>(PropOffsets_Actor.NetUpdateFrequency); }
	float& OddsOfAppearing() { return Value<float>(PropOffsets_Actor.OddsOfAppearing); }
	float& OldAnimRate() { return Value<float>(PropOffsets_Actor.OldAnimRate); }
	vec3& OldLocation() { return Value<vec3>(PropOffsets_Actor.OldLocation); }
	int& OtherTag() { return Value<int>(PropOffsets_Actor.OtherTag); }
	UActor*& Owner() { return Value<UActor*>(PropOffsets_Actor.Owner); }
	UActor*& PendingTouch() { return Value<UActor*>(PropOffsets_Actor.PendingTouch); }
	float& PhysAlpha() { return Value<float>(PropOffsets_Actor.PhysAlpha); }
	float& PhysRate() { return Value<float>(PropOffsets_Actor.PhysRate); }
	uint8_t& Physics() { return Value<uint8_t>(PropOffsets_Actor.Physics); }
	vec3& PrePivot() { return Value<vec3>(PropOffsets_Actor.PrePivot); }
	PointRegion& Region() { return Value<PointRegion>(PropOffsets_Actor.Region); }
	uint8_t& RemoteRole() { return Value<uint8_t>(PropOffsets_Actor.RemoteRole); }
	//URenderIterator*& RenderInterface() { return Value<URenderIterator*>(PropOffsets_Actor.RenderInterface); }
	UClass*& RenderIteratorClass() { return Value<UClass*>(PropOffsets_Actor.RenderIteratorClass); }
	uint8_t& Role() { return Value<uint8_t>(PropOffsets_Actor.Role); }
	Rotator& Rotation() { return Value<Rotator>(PropOffsets_Actor.Rotation); }
	Rotator& RotationRate() { return Value<Rotator>(PropOffsets_Actor.RotationRate); }
	float& ScaleGlow() { return Value<float>(PropOffsets_Actor.ScaleGlow); }
	//Plane& SimAnim() { return Value<Plane>(PropOffsets_Actor.SimAnim); }
	UAnimation*& SkelAnim() { return Value<UAnimation*>(PropOffsets_Actor.SkelAnim); }
	UTexture*& Skin() { return Value<UTexture*>(PropOffsets_Actor.Skin); }
	uint8_t& SoundPitch() { return Value<uint8_t>(PropOffsets_Actor.SoundPitch); }
	uint8_t& SoundRadius() { return Value<uint8_t>(PropOffsets_Actor.SoundRadius); }
	uint8_t& SoundVolume() { return Value<uint8_t>(PropOffsets_Actor.SoundVolume); }
	int& SpecialTag() { return Value<int>(PropOffsets_Actor.SpecialTag); }
	UTexture*& Sprite() { return Value<UTexture*>(PropOffsets_Actor.Sprite); }
	float& SpriteProjForward() { return Value<float>(PropOffsets_Actor.SpriteProjForward); }
	uint8_t& StandingCount() { return Value<uint8_t>(PropOffsets_Actor.StandingCount); }
	ERenderStyle Style() { return static_cast<ERenderStyle>(Value<uint8_t>(PropOffsets_Actor.Style)); }
	NameString& Tag() { return Value<NameString>(PropOffsets_Actor.Tag); }
	UActor*& Target() { return Value<UActor*>(PropOffsets_Actor.Target); }
	UTexture*& Texture() { return Value<UTexture*>(PropOffsets_Actor.Texture); }
	float& TimerCounter() { return Value<float>(PropOffsets_Actor.TimerCounter); }
	float& TimerRate() { return Value<float>(PropOffsets_Actor.TimerRate); }
	FixedArrayView<UActor*, 4> Touching() { return FixedArray<UActor*, 4>(PropOffsets_Actor.Touching); }
	TypedScriptArray<UActor*> Touching_UT469() { return DynamicArray<UActor*>(PropOffsets_Actor.Touching); }
	float& TransientSoundRadius() { return Value<float>(PropOffsets_Actor.TransientSoundRadius); }
	float& TransientSoundVolume() { return Value<float>(PropOffsets_Actor.TransientSoundVolume); }
	float& TweenRate() { return Value<float>(PropOffsets_Actor.TweenRate); }
	vec3& Velocity() { return Value<vec3>(PropOffsets_Actor.Velocity); }
	float& VisibilityHeight() { return Value<float>(PropOffsets_Actor.VisibilityHeight); }
	float& VisibilityRadius() { return Value<float>(PropOffsets_Actor.VisibilityRadius); }
	uint8_t& VolumeBrightness() { return Value<uint8_t>(PropOffsets_Actor.VolumeBrightness); }
	uint8_t& VolumeFog() { return Value<uint8_t>(PropOffsets_Actor.VolumeFog); }
	uint8_t& VolumeRadius() { return Value<uint8_t>(PropOffsets_Actor.VolumeRadius); }
	ULevel*& XLevel() { return Value<ULevel*>(PropOffsets_Actor.XLevel); }
	BitfieldBool bActorShadows() { return BoolValue(PropOffsets_Actor.bActorShadows); }
	BitfieldBool bAlwaysRelevant() { return BoolValue(PropOffsets_Actor.bAlwaysRelevant); }
	BitfieldBool bAlwaysTick() { return BoolValue(PropOffsets_Actor.bAlwaysTick); }
	BitfieldBool bAnimByOwner() { return BoolValue(PropOffsets_Actor.bAnimByOwner); }
	BitfieldBool bAnimFinished() { return BoolValue(PropOffsets_Actor.bAnimFinished); }
	BitfieldBool bAnimLoop() { return BoolValue(PropOffsets_Actor.bAnimLoop); }
	BitfieldBool bAnimNotify() { return BoolValue(PropOffsets_Actor.bAnimNotify); }
	BitfieldBool bAssimilated() { return BoolValue(PropOffsets_Actor.bAssimilated); }
	BitfieldBool bBlockActors() { return BoolValue(PropOffsets_Actor.bBlockActors); }
	BitfieldBool bBlockPlayers() { return BoolValue(PropOffsets_Actor.bBlockPlayers); }
	BitfieldBool bBlockSight() { return BoolValue(PropOffsets_Actor.bBlockSight); }
	BitfieldBool bBounce() { return BoolValue(PropOffsets_Actor.bBounce); }
	BitfieldBool bCanTeleport() { return BoolValue(PropOffsets_Actor.bCanTeleport); }
	BitfieldBool bCarriedItem() { return BoolValue(PropOffsets_Actor.bCarriedItem); }
	BitfieldBool bClientAnim() { return BoolValue(PropOffsets_Actor.bClientAnim); }
	BitfieldBool bClientDemoNetFunc() { return BoolValue(PropOffsets_Actor.bClientDemoNetFunc); }
	BitfieldBool bClientDemoRecording() { return BoolValue(PropOffsets_Actor.bClientDemoRecording); }
	BitfieldBool bCollideActors() { return BoolValue(PropOffsets_Actor.bCollideActors); }
	BitfieldBool bCollideWhenPlacing() { return BoolValue(PropOffsets_Actor.bCollideWhenPlacing); }
	BitfieldBool bCollideWorld() { return BoolValue(PropOffsets_Actor.bCollideWorld); }
	BitfieldBool bCorona() { return BoolValue(PropOffsets_Actor.bCorona); }
	BitfieldBool bDeleteMe() { return BoolValue(PropOffsets_Actor.bDeleteMe); }
	BitfieldBool bDemoRecording() { return BoolValue(PropOffsets_Actor.bDemoRecording); }
	BitfieldBool bDifficulty0() { return BoolValue(PropOffsets_Actor.bDifficulty0); }
	BitfieldBool bDifficulty1() { return BoolValue(PropOffsets_Actor.bDifficulty1); }
	BitfieldBool bDifficulty2() { return BoolValue(PropOffsets_Actor.bDifficulty2); }
	BitfieldBool bDifficulty3() { return BoolValue(PropOffsets_Actor.bDifficulty3); }
	BitfieldBool bDirectional() { return BoolValue(PropOffsets_Actor.bDirectional); }
	BitfieldBool bDynamicLight() { return BoolValue(PropOffsets_Actor.bDynamicLight); }
	BitfieldBool bEdLocked() { return BoolValue(PropOffsets_Actor.bEdLocked); }
	BitfieldBool bEdShouldSnap() { return BoolValue(PropOffsets_Actor.bEdShouldSnap); }
	BitfieldBool bEdSnap() { return BoolValue(PropOffsets_Actor.bEdSnap); }
	BitfieldBool bFilterByVolume() { return BoolValue(PropOffsets_Actor.bFilterByVolume); }
	BitfieldBool bFixedRotationDir() { return BoolValue(PropOffsets_Actor.bFixedRotationDir); }
	BitfieldBool bForcePhysicsUpdate() { return BoolValue(PropOffsets_Actor.bForcePhysicsUpdate); }
	BitfieldBool bForceStasis() { return BoolValue(PropOffsets_Actor.bForceStasis); }
	BitfieldBool bGameRelevant() { return BoolValue(PropOffsets_Actor.bGameRelevant); }
	BitfieldBool bHidden() { return BoolValue(PropOffsets_Actor.bHidden); }
	BitfieldBool bHiddenEd() { return BoolValue(PropOffsets_Actor.bHiddenEd); }
	BitfieldBool bHighDetail() { return BoolValue(PropOffsets_Actor.bHighDetail); }
	BitfieldBool bHighlighted() { return BoolValue(PropOffsets_Actor.bHighlighted); }
	BitfieldBool bHurtEntry() { return BoolValue(PropOffsets_Actor.bHurtEntry); }
	BitfieldBool bInterpolating() { return BoolValue(PropOffsets_Actor.bInterpolating); }
	BitfieldBool bIsItemGoal() { return BoolValue(PropOffsets_Actor.bIsItemGoal); }
	BitfieldBool bIsKillGoal() { return BoolValue(PropOffsets_Actor.bIsKillGoal); }
	BitfieldBool bIsMover() { return BoolValue(PropOffsets_Actor.bIsMover); }
	BitfieldBool bIsPawn() { return BoolValue(PropOffsets_Actor.bIsPawn); }
	BitfieldBool bIsSecretGoal() { return BoolValue(PropOffsets_Actor.bIsSecretGoal); }
	BitfieldBool bJustTeleported() { return BoolValue(PropOffsets_Actor.bJustTeleported); }
	BitfieldBool bLensFlare() { return BoolValue(PropOffsets_Actor.bLensFlare); }
	BitfieldBool bLightChanged() { return BoolValue(PropOffsets_Actor.bLightChanged); }
	BitfieldBool bMemorized() { return BoolValue(PropOffsets_Actor.bMemorized); }
	BitfieldBool bMeshCurvy() { return BoolValue(PropOffsets_Actor.bMeshCurvy); }
	BitfieldBool bMeshEnviroMap() { return BoolValue(PropOffsets_Actor.bMeshEnviroMap); }
	BitfieldBool bMovable() { return BoolValue(PropOffsets_Actor.bMovable); }
	BitfieldBool bNet() { return BoolValue(PropOffsets_Actor.bNet); }
	BitfieldBool bNetFeel() { return BoolValue(PropOffsets_Actor.bNetFeel); }
	BitfieldBool bNetHear() { return BoolValue(PropOffsets_Actor.bNetHear); }
	BitfieldBool bNetInitial() { return BoolValue(PropOffsets_Actor.bNetInitial); }
	BitfieldBool bNetOptional() { return BoolValue(PropOffsets_Actor.bNetOptional); }
	BitfieldBool bNetOwner() { return BoolValue(PropOffsets_Actor.bNetOwner); }
	BitfieldBool bNetRelevant() { return BoolValue(PropOffsets_Actor.bNetRelevant); }
	BitfieldBool bNetSee() { return BoolValue(PropOffsets_Actor.bNetSee); }
	BitfieldBool bNetSpecial() { return BoolValue(PropOffsets_Actor.bNetSpecial); }
	BitfieldBool bNetTemporary() { return BoolValue(PropOffsets_Actor.bNetTemporary); }
	BitfieldBool bNoDelete() { return BoolValue(PropOffsets_Actor.bNoDelete); }
	BitfieldBool bNoSmooth() { return BoolValue(PropOffsets_Actor.bNoSmooth); }
	BitfieldBool bOnlyOwnerSee() { return BoolValue(PropOffsets_Actor.bOnlyOwnerSee); }
	BitfieldBool bOwnerNoSee() { return BoolValue(PropOffsets_Actor.bOwnerNoSee); }
	BitfieldBool bParticles() { return BoolValue(PropOffsets_Actor.bParticles); }
	BitfieldBool bProjTarget() { return BoolValue(PropOffsets_Actor.bProjTarget); }
	BitfieldBool bRandomFrame() { return BoolValue(PropOffsets_Actor.bRandomFrame); }
	BitfieldBool bReplicateInstigator() { return BoolValue(PropOffsets_Actor.bReplicateInstigator); }
	BitfieldBool bRotateToDesired() { return BoolValue(PropOffsets_Actor.bRotateToDesired); }
	BitfieldBool bScriptInitialized() { return BoolValue(PropOffsets_Actor.bScriptInitialized); }
	BitfieldBool bSelected() { return BoolValue(PropOffsets_Actor.bSelected); }
	BitfieldBool bShadowCast() { return BoolValue(PropOffsets_Actor.bShadowCast); }
	BitfieldBool bSimFall() { return BoolValue(PropOffsets_Actor.bSimFall); }
	BitfieldBool bSimulatedPawn() { return BoolValue(PropOffsets_Actor.bSimulatedPawn); }
	BitfieldBool bSinglePlayer() { return BoolValue(PropOffsets_Actor.bSinglePlayer); }
	BitfieldBool bSpecialLit() { return BoolValue(PropOffsets_Actor.bSpecialLit); }
	BitfieldBool bStasis() { return BoolValue(PropOffsets_Actor.bStasis); }
	BitfieldBool bStatic() { return BoolValue(PropOffsets_Actor.bStatic); }
	BitfieldBool bTempEditor() { return BoolValue(PropOffsets_Actor.bTempEditor); }
	BitfieldBool bTicked() { return BoolValue(PropOffsets_Actor.bTicked); }
	BitfieldBool bTimerLoop() { return BoolValue(PropOffsets_Actor.bTimerLoop); }
	BitfieldBool bTrailerPrePivot() { return BoolValue(PropOffsets_Actor.bTrailerPrePivot); }
	BitfieldBool bTrailerSameRotation() { return BoolValue(PropOffsets_Actor.bTrailerSameRotation); }
	BitfieldBool bTravel() { return BoolValue(PropOffsets_Actor.bTravel); }
	BitfieldBool bUnlit() { return BoolValue(PropOffsets_Actor.bUnlit); }

	// Deus Ex exclusive properties
	std::string& BindName() { return Value<std::string>(PropOffsets_Actor.BindName); }
	std::string& BarkBindName() { return Value<std::string>(PropOffsets_Actor.BarkBindName); }

	FixedArrayView<float, 4> BlendAnimLast() { return FixedArray<float, 4>(PropOffsets_Actor.BlendAnimLast); }
	FixedArrayView<float, 4> BlendAnimMinRate() { return FixedArray<float, 4>(PropOffsets_Actor.BlendAnimMinRate); }
	FixedArrayView<float, 4> OldBlendAnimRate() { return FixedArray<float, 4>(PropOffsets_Actor.OldBlendAnimRate); }
	FixedArrayView<vec4, 4> SimBlendAnim() { return FixedArray<vec4, 4>(PropOffsets_Actor.SimBlendAnim); }

	std::string& FamiliarName() { return Value<std::string>(PropOffsets_Actor.FamiliarName); }
	std::string& UnfamiliarName() { return Value<std::string>(PropOffsets_Actor.FamiliarName); }
	UObject*& ConListItems() { return Value<UObject*>(PropOffsets_Actor.ConListItems); }
	float& LastConEndTime() { return Value<float>(PropOffsets_Actor.LastConEndTime); }
	float& ConStartInterval() { return Value<float>(PropOffsets_Actor.ConStartInterval); }

	float& VisUpdateTime() { return Value<float>(PropOffsets_Actor.VisUpdateTime); }
	float& CurrentVisibility() { return Value<float>(PropOffsets_Actor.CurrentVisibility); }
	float& LastVisibility() { return Value<float>(PropOffsets_Actor.LastVisibility); }

	UClass*& SmellClass() { return Value<UClass*>(PropOffsets_Actor.SmellClass); }
	// SmellNode*& LastSmellNode() { return Value<SmellNode*>(PropOffsets_Actor.LastSmellNode); } // SmellNode is not a native class

	BitfieldBool bOwned() { return BoolValue(PropOffsets_Actor.bOwned); }

	FixedArrayView<NameString, 4> BlendAnimSequence() {return FixedArray<NameString, 4>(PropOffsets_Actor.BlendAnimSequence);}
	FixedArrayView<float, 4> BlendAnimFrame() {return FixedArray<float, 4>(PropOffsets_Actor.BlendAnimFrame);}
	FixedArrayView<float, 4> BlendAnimRate() {return FixedArray<float, 4>(PropOffsets_Actor.BlendAnimRate);}
	FixedArrayView<float, 4> BlendTweenRate() {return FixedArray<float, 4>(PropOffsets_Actor.BlendTweenRate);}

	// Unreal 227 exclusive Properties
	BitfieldBool bNetNotify() { return BoolValue(PropOffsets_Actor.bNetNotify); }
	BitfieldBool bHandleOwnCorona() { return BoolValue(PropOffsets_Actor.bHandleOwnCorona); }
	BitfieldBool bRenderMultiEnviroMaps() { return BoolValue(PropOffsets_Actor.bRenderMultiEnviroMaps); }
	BitfieldBool bWorldGeometry() { return BoolValue(PropOffsets_Actor.bWorldGeometry); }
	BitfieldBool bUseMeshCollision() { return BoolValue(PropOffsets_Actor.bUseMeshCollision); }
	BitfieldBool bEditorSelectRender() { return BoolValue(PropOffsets_Actor.bEditorSelectRender); }
	BitfieldBool bNoDynamicShadowCast() { return BoolValue(PropOffsets_Actor.bNoDynamicShadowCast); }
	BitfieldBool bIsInOctree() { return BoolValue(PropOffsets_Actor.bIsInOctree); }
	BitfieldBool bProjectorDecal() { return BoolValue(PropOffsets_Actor.bProjectorDecal); }
	BitfieldBool bUseLitSprite() { return BoolValue(PropOffsets_Actor.bUseLitSprite); }
	BitfieldBool bAlwaysRender() { return BoolValue(PropOffsets_Actor.bAlwaysRender); }

	float& LastRenderedTime() { return Value<float>(PropOffsets_Actor.LastRenderedTime); }
	Color& ActorRenderColor() { return Value<Color>(PropOffsets_Actor.ActorRenderColor); }
	Color& ActorGUnlitColor() { return Value<Color>(PropOffsets_Actor.ActorGUnlitColor); }
	UPrimitive*& CollisionOverride() { return Value<UPrimitive*>(PropOffsets_Actor.CollisionOverride); }
	U227SkeletalMeshInstance*& MeshInstance() { return Value<U227SkeletalMeshInstance*>(PropOffsets_Actor.MeshInstance); }
	vec3*& RelativeLocation() { return Value<vec3*>(PropOffsets_Actor.RelativeLocation); }
	Rotator*& RelativeRotation() { return Value<Rotator*>(PropOffsets_Actor.RelativeRotation); }
	// Pointer type LightDataPtr()
	// Pointer type MeshDataPtr()
	TypedScriptArray<U227Projector*> ProjectorList() { return DynamicArray<U227Projector*>(PropOffsets_Actor.ProjectorList); }
	// Pointer type NetInitialProperties()
	TypedScriptArray<UActor*> RealTouching() { return DynamicArray<UActor*>(PropOffsets_Actor.RealTouching); }

	UClass*& DefaultAnimationNotify() { return Value<UClass*>(PropOffsets_Actor.DefaultAnimationNotify); }
	U227AnimationNotify*& AnimationNotify() { return Value<U227AnimationNotify*>(PropOffsets_Actor.AnimationNotify); }

	BitfieldBool bSkipActorReplication() { return BoolValue(PropOffsets_Actor.bSkipActorReplication); }
	BitfieldBool bRepAnimations() { return BoolValue(PropOffsets_Actor.bRepAnimations); }
	BitfieldBool bRepAmbientSound() { return BoolValue(PropOffsets_Actor.bRepAmbientSound); }
	BitfieldBool bSimulatedPawnRep() { return BoolValue(PropOffsets_Actor.bSimulatedPawnRep); }
	BitfieldBool bRepMesh() { return BoolValue(PropOffsets_Actor.bRepMesh); }

private:
	void AddBasedActor(UActor* actor);
	void RemoveBasedActor(UActor* actor);
	void TurnBasedActors(const Rotator& deltaRotation);
};

class ULight : public UActor
{
public:
	using UActor::UActor;
};

class UDecal : public UActor
{
public:
	using UActor::UActor;

	UObject* AttachDecal(float traceDistance, vec3 decalDir);
	void DetachDecal();

	float& LastRenderedTime() { return Value<float>(PropOffsets_Decal.LastRenderedTime); }
	int& MultiDecalLevel() { return Value<int>(PropOffsets_Decal.MultiDecalLevel); }
	TypedScriptArray<void*> SurfList() { return DynamicArray<void*>(PropOffsets_Decal.SurfList); }

	Array<BspNode*> Nodes;
};

class U227AnimationNotify : public UObject
{
public:
	using UObject::UObject;

	FixedArrayView<sAnimNotify, 255> AnimationNotify() { return FixedArray<sAnimNotify, 255>(PropOffsets_AnimationNotify.AnimationNotify); }
	int& NumNotifies() { return Value<int>(PropOffsets_AnimationNotify.NumNotifies); }
	UActor*& Owner() { return Value<UActor*>(PropOffsets_AnimationNotify.Owner); }
	BitfieldBool bInitialized() { return BoolValue(PropOffsets_AnimationNotify.bInitialized); }
	BitfieldBool bErrorOccured() { return BoolValue(PropOffsets_AnimationNotify.bErrorOccured); }
};

class USpawnNotify : public UActor
{
public:
	using UActor::UActor;

	UClass*& ActorClass() { return Value<UClass*>(PropOffsets_SpawnNotify.ActorClass); }
	USpawnNotify*& Next() { return Value<USpawnNotify*>(PropOffsets_SpawnNotify.Next); }
};

class UInventory : public UActor
{
public:
	using UActor::UActor;

	int& AbsorptionPriority() { return Value<int>(PropOffsets_Inventory.AbsorptionPriority); }
	USound*& ActivateSound() { return Value<USound*>(PropOffsets_Inventory.ActivateSound); }
	int& ArmorAbsorption() { return Value<int>(PropOffsets_Inventory.ArmorAbsorption); }
	uint8_t& AutoSwitchPriority() { return Value<uint8_t>(PropOffsets_Inventory.AutoSwitchPriority); }
	float& BobDamping() { return Value<float>(PropOffsets_Inventory.BobDamping); }
	int& Charge() { return Value<int>(PropOffsets_Inventory.Charge); }
	USound*& DeActivateSound() { return Value<USound*>(PropOffsets_Inventory.DeActivateSound); }
	uint8_t& FlashCount() { return Value<uint8_t>(PropOffsets_Inventory.FlashCount); }
	UTexture*& Icon() { return Value<UTexture*>(PropOffsets_Inventory.Icon); }
	uint8_t& InventoryGroup() { return Value<uint8_t>(PropOffsets_Inventory.InventoryGroup); }
	std::string& ItemArticle() { return Value<std::string>(PropOffsets_Inventory.ItemArticle); }
	UClass*& ItemMessageClass() { return Value<UClass*>(PropOffsets_Inventory.ItemMessageClass); }
	std::string& ItemName() { return Value<std::string>(PropOffsets_Inventory.ItemName); }
	std::string& M_Activated() { return Value<std::string>(PropOffsets_Inventory.M_Activated); }
	std::string& M_Deactivated() { return Value<std::string>(PropOffsets_Inventory.M_Deactivated); }
	std::string& M_Selected() { return Value<std::string>(PropOffsets_Inventory.M_Selected); }
	float& MaxDesireability() { return Value<float>(PropOffsets_Inventory.MaxDesireability); }
	UMesh*& MuzzleFlashMesh() { return Value<UMesh*>(PropOffsets_Inventory.MuzzleFlashMesh); }
	float& MuzzleFlashScale() { return Value<float>(PropOffsets_Inventory.MuzzleFlashScale); }
	uint8_t& MuzzleFlashStyle() { return Value<uint8_t>(PropOffsets_Inventory.MuzzleFlashStyle); }
	UTexture*& MuzzleFlashTexture() { return Value<UTexture*>(PropOffsets_Inventory.MuzzleFlashTexture); }
	UInventory*& NextArmor() { return Value<UInventory*>(PropOffsets_Inventory.NextArmor); }
	uint8_t& OldFlashCount() { return Value<uint8_t>(PropOffsets_Inventory.OldFlashCount); }
	std::string& PickupMessage() { return Value<std::string>(PropOffsets_Inventory.PickupMessage); }
	UClass*& PickupMessageClass() { return Value<UClass*>(PropOffsets_Inventory.PickupMessageClass); }
	USound*& PickupSound() { return Value<USound*>(PropOffsets_Inventory.PickupSound); }
	UMesh*& PickupViewMesh() { return Value<UMesh*>(PropOffsets_Inventory.PickupViewMesh); }
	float& PickupViewScale() { return Value<float>(PropOffsets_Inventory.PickupViewScale); }
	NameString& PlayerLastTouched() { return Value<NameString>(PropOffsets_Inventory.PlayerLastTouched); }
	UMesh*& PlayerViewMesh() { return Value<UMesh*>(PropOffsets_Inventory.PlayerViewMesh); }
	vec3& PlayerViewOffset() { return Value<vec3>(PropOffsets_Inventory.PlayerViewOffset); }
	float& PlayerViewScale() { return Value<float>(PropOffsets_Inventory.PlayerViewScale); }
	NameString& ProtectionType1() { return Value<NameString>(PropOffsets_Inventory.ProtectionType1); }
	NameString& ProtectionType2() { return Value<NameString>(PropOffsets_Inventory.ProtectionType2); }
	USound*& RespawnSound() { return Value<USound*>(PropOffsets_Inventory.RespawnSound); }
	float& RespawnTime() { return Value<float>(PropOffsets_Inventory.RespawnTime); }
	UTexture*& StatusIcon() { return Value<UTexture*>(PropOffsets_Inventory.StatusIcon); }
	UMesh*& ThirdPersonMesh() { return Value<UMesh*>(PropOffsets_Inventory.ThirdPersonMesh); }
	float& ThirdPersonScale() { return Value<float>(PropOffsets_Inventory.ThirdPersonScale); }
	BitfieldBool bActivatable() { return BoolValue(PropOffsets_Inventory.bActivatable); }
	BitfieldBool bActive() { return BoolValue(PropOffsets_Inventory.bActive); }
	BitfieldBool bAmbientGlow() { return BoolValue(PropOffsets_Inventory.bAmbientGlow); }
	BitfieldBool bDisplayableInv() { return BoolValue(PropOffsets_Inventory.bDisplayableInv); }
	BitfieldBool bFirstFrame() { return BoolValue(PropOffsets_Inventory.bFirstFrame); }
	BitfieldBool bHeldItem() { return BoolValue(PropOffsets_Inventory.bHeldItem); }
	BitfieldBool bInstantRespawn() { return BoolValue(PropOffsets_Inventory.bInstantRespawn); }
	BitfieldBool bIsAnArmor() { return BoolValue(PropOffsets_Inventory.bIsAnArmor); }
	BitfieldBool bMuzzleFlashParticles() { return BoolValue(PropOffsets_Inventory.bMuzzleFlashParticles); }
	BitfieldBool bRotatingPickup() { return BoolValue(PropOffsets_Inventory.bRotatingPickup); }
	BitfieldBool bSleepTouch() { return BoolValue(PropOffsets_Inventory.bSleepTouch); }
	BitfieldBool bSteadyFlash3rd() { return BoolValue(PropOffsets_Inventory.bSteadyFlash3rd); }
	BitfieldBool bSteadyToggle() { return BoolValue(PropOffsets_Inventory.bSteadyToggle); }
	BitfieldBool bToggleSteadyFlash() { return BoolValue(PropOffsets_Inventory.bToggleSteadyFlash); }
	BitfieldBool bTossedOut() { return BoolValue(PropOffsets_Inventory.bTossedOut); }
	UInventorySpot*& myMarker() { return Value<UInventorySpot*>(PropOffsets_Inventory.myMarker); }
};

class UInventoryAttachment : public UActor
{
public:
	using UActor::UActor;
	// Empty base class
};

class UWeapon : public UInventory
{
public:
	using UInventory::UInventory;

	float& AIRating() { return Value<float>(PropOffsets_Weapon.AIRating); }
	Rotator& AdjustedAim() { return Value<Rotator>(PropOffsets_Weapon.AdjustedAim); }
	NameString& AltDamageType() { return Value<NameString>(PropOffsets_Weapon.AltDamageType); }
	USound*& AltFireSound() { return Value<USound*>(PropOffsets_Weapon.AltFireSound); }
	UClass*& AltProjectileClass() { return Value<UClass*>(PropOffsets_Weapon.AltProjectileClass); }
	float& AltProjectileSpeed() { return Value<float>(PropOffsets_Weapon.AltProjectileSpeed); }
	float& AltRefireRate() { return Value<float>(PropOffsets_Weapon.AltRefireRate); }
	UClass*& AmmoName() { return Value<UClass*>(PropOffsets_Weapon.AmmoName); }
	//UAmmo*& AmmoType() { return Value<UAmmo*>(PropOffsets_Weapon.AmmoType); }
	USound*& CockingSound() { return Value<USound*>(PropOffsets_Weapon.CockingSound); }
	std::string& DeathMessage() { return Value<std::string>(PropOffsets_Weapon.DeathMessage); }
	vec3& FireOffset() { return Value<vec3>(PropOffsets_Weapon.FireOffset); }
	USound*& FireSound() { return Value<USound*>(PropOffsets_Weapon.FireSound); }
	float& FiringSpeed() { return Value<float>(PropOffsets_Weapon.FiringSpeed); }
	float& FlareOffset() { return Value<float>(PropOffsets_Weapon.FlareOffset); }
	float& FlashC() { return Value<float>(PropOffsets_Weapon.FlashC); }
	float& FlashLength() { return Value<float>(PropOffsets_Weapon.FlashLength); }
	float& FlashO() { return Value<float>(PropOffsets_Weapon.FlashO); }
	int& FlashS() { return Value<int>(PropOffsets_Weapon.FlashS); }
	float& FlashTime() { return Value<float>(PropOffsets_Weapon.FlashTime); }
	float& FlashY() { return Value<float>(PropOffsets_Weapon.FlashY); }
	UTexture*& MFTexture() { return Value<UTexture*>(PropOffsets_Weapon.MFTexture); }
	float& MaxTargetRange() { return Value<float>(PropOffsets_Weapon.MaxTargetRange); }
	std::string& MessageNoAmmo() { return Value<std::string>(PropOffsets_Weapon.MessageNoAmmo); }
	USound*& Misc1Sound() { return Value<USound*>(PropOffsets_Weapon.Misc1Sound); }
	USound*& Misc2Sound() { return Value<USound*>(PropOffsets_Weapon.Misc2Sound); }
	USound*& Misc3Sound() { return Value<USound*>(PropOffsets_Weapon.Misc3Sound); }
	UTexture*& MuzzleFlare() { return Value<UTexture*>(PropOffsets_Weapon.MuzzleFlare); }
	float& MuzzleScale() { return Value<float>(PropOffsets_Weapon.MuzzleScale); }
	NameString& MyDamageType() { return Value<NameString>(PropOffsets_Weapon.MyDamageType); }
	Color& NameColor() { return Value<Color>(PropOffsets_Weapon.NameColor); }
	int& PickupAmmoCount() { return Value<int>(PropOffsets_Weapon.PickupAmmoCount); }
	UClass*& ProjectileClass() { return Value<UClass*>(PropOffsets_Weapon.ProjectileClass); }
	float& ProjectileSpeed() { return Value<float>(PropOffsets_Weapon.ProjectileSpeed); }
	float& RefireRate() { return Value<float>(PropOffsets_Weapon.RefireRate); }
	uint8_t& ReloadCount() { return Value<uint8_t>(PropOffsets_Weapon.ReloadCount); }
	USound*& SelectSound() { return Value<USound*>(PropOffsets_Weapon.SelectSound); }
	float& aimerror() { return Value<float>(PropOffsets_Weapon.aimerror); }
	BitfieldBool bAltInstantHit() { return BoolValue(PropOffsets_Weapon.bAltInstantHit); }
	BitfieldBool bAltWarnTarget() { return BoolValue(PropOffsets_Weapon.bAltWarnTarget); }
	BitfieldBool bCanThrow() { return BoolValue(PropOffsets_Weapon.bCanThrow); }
	BitfieldBool bChangeWeapon() { return BoolValue(PropOffsets_Weapon.bChangeWeapon); }
	BitfieldBool bDrawMuzzleFlash() { return BoolValue(PropOffsets_Weapon.bDrawMuzzleFlash); }
	BitfieldBool bHideWeapon() { return BoolValue(PropOffsets_Weapon.bHideWeapon); }
	BitfieldBool bInstantHit() { return BoolValue(PropOffsets_Weapon.bInstantHit); }
	BitfieldBool bLockedOn() { return BoolValue(PropOffsets_Weapon.bLockedOn); }
	BitfieldBool bMeleeWeapon() { return BoolValue(PropOffsets_Weapon.bMeleeWeapon); }
	uint8_t& bMuzzleFlash() { return Value<uint8_t>(PropOffsets_Weapon.bMuzzleFlash); }
	BitfieldBool bOwnsCrosshair() { return BoolValue(PropOffsets_Weapon.bOwnsCrosshair); }
	BitfieldBool bPointing() { return BoolValue(PropOffsets_Weapon.bPointing); }
	BitfieldBool bRapidFire() { return BoolValue(PropOffsets_Weapon.bRapidFire); }
	BitfieldBool bRecommendAltSplashDamage() { return BoolValue(PropOffsets_Weapon.bRecommendAltSplashDamage); }
	BitfieldBool bRecommendSplashDamage() { return BoolValue(PropOffsets_Weapon.bRecommendSplashDamage); }
	BitfieldBool bSetFlashTime() { return BoolValue(PropOffsets_Weapon.bSetFlashTime); }
	BitfieldBool bSpecialIcon() { return BoolValue(PropOffsets_Weapon.bSpecialIcon); }
	BitfieldBool bSplashDamage() { return BoolValue(PropOffsets_Weapon.bSplashDamage); }
	BitfieldBool bWarnTarget() { return BoolValue(PropOffsets_Weapon.bWarnTarget); }
	BitfieldBool bWeaponStay() { return BoolValue(PropOffsets_Weapon.bWeaponStay); }
	BitfieldBool bWeaponUp() { return BoolValue(PropOffsets_Weapon.bWeaponUp); }
	float& shakemag() { return Value<float>(PropOffsets_Weapon.shakemag); }
	float& shaketime() { return Value<float>(PropOffsets_Weapon.shaketime); }
	float& shakevert() { return Value<float>(PropOffsets_Weapon.shakevert); }
};

class UWeaponMuzzleFlash : public UInventoryAttachment
{
public:
	using UInventoryAttachment::UInventoryAttachment;

	BitfieldBool bConstantMuzzle() { return BoolValue(PropOffsets_WeaponMuzzleFlash.bConstantMuzzle); }
	BitfieldBool bStrobeMuzzle() { return BoolValue(PropOffsets_WeaponMuzzleFlash.bStrobeMuzzle); }
	BitfieldBool bFlashTimer() { return BoolValue(PropOffsets_WeaponMuzzleFlash.bFlashTimer); }
	BitfieldBool bCurrentlyVisible() { return BoolValue(PropOffsets_WeaponMuzzleFlash.bCurrentlyVisible); }
};

class UWeaponAttachment : public UInventoryAttachment
{
public:
	using UInventoryAttachment::UInventoryAttachment;

	BitfieldBool bCopyDisplay() { return BoolValue(PropOffsets_WeaponAttachment.bCopyDisplay); }
	float& LastUpdateTime() { return Value<float>(PropOffsets_WeaponAttachment.LastUpdateTime); }
	UWeaponMuzzleFlash*& MyMuzzleFlash() { return Value<UWeaponMuzzleFlash*>(PropOffsets_WeaponAttachment.MyMuzzleFlash); }
	UWeapon*& WeaponOwner() { return Value<UWeapon*>(PropOffsets_WeaponAttachment.WeaponOwner); }
};

class UNavigationPoint : public UActor
{
public:
	using UActor::UActor;

	// Paths() is an array of LevelReachSpec indexes to navigation points that can be reached from this one.
	// upstreamPaths() is the same as Paths(), except this is in reverse order (when searching from a goal back to initially reachable points).
	// PrunedPaths() are reachable points that have been removed from Paths() as they could already be reached via a different path.
	// If -1 is encountered in any of those arrays it means the end of the list.

	int& ExtraCost() { return Value<int>(PropOffsets_NavigationPoint.ExtraCost); }
	FixedArrayView<int, 16> Paths() { return FixedArray<int, 16>(PropOffsets_NavigationPoint.Paths); }
	FixedArrayView<int, 16> PrunedPaths() { return FixedArray<int, 16>(PropOffsets_NavigationPoint.PrunedPaths); }
	UActor*& RouteCache() { return Value<UActor*>(PropOffsets_NavigationPoint.RouteCache); }
	FixedArrayView<UNavigationPoint*, 16> VisNoReachPaths() { return FixedArray<UNavigationPoint*, 16>(PropOffsets_NavigationPoint.VisNoReachPaths); }
	BitfieldBool bAutoBuilt() { return BoolValue(PropOffsets_NavigationPoint.bAutoBuilt); }
	BitfieldBool bEndPoint() { return BoolValue(PropOffsets_NavigationPoint.bEndPoint); }
	BitfieldBool bEndPointOnly() { return BoolValue(PropOffsets_NavigationPoint.bEndPointOnly); }
	BitfieldBool bNeverUseStrafing() { return BoolValue(PropOffsets_NavigationPoint.bNeverUseStrafing); }
	BitfieldBool bOneWayPath() { return BoolValue(PropOffsets_NavigationPoint.bOneWayPath); }
	BitfieldBool bPlayerOnly() { return BoolValue(PropOffsets_NavigationPoint.bPlayerOnly); }
	BitfieldBool bSpecialCost() { return BoolValue(PropOffsets_NavigationPoint.bSpecialCost); }
	BitfieldBool bTwoWay() { return BoolValue(PropOffsets_NavigationPoint.bTwoWay); }
	int& bestPathWeight() { return Value<int>(PropOffsets_NavigationPoint.bestPathWeight); }
	int& cost() { return Value<int>(PropOffsets_NavigationPoint.cost); }
	UNavigationPoint*& nextNavigationPoint() { return Value<UNavigationPoint*>(PropOffsets_NavigationPoint.nextNavigationPoint); }
	UNavigationPoint*& nextOrdered() { return Value<UNavigationPoint*>(PropOffsets_NavigationPoint.nextOrdered); }
	NameString& ownerTeam() { return Value<NameString>(PropOffsets_NavigationPoint.ownerTeam); }
	UNavigationPoint*& prevOrdered() { return Value<UNavigationPoint*>(PropOffsets_NavigationPoint.prevOrdered); }
	UNavigationPoint*& previousPath() { return Value<UNavigationPoint*>(PropOffsets_NavigationPoint.previousPath); }
	UNavigationPoint*& startPath() { return Value<UNavigationPoint*>(PropOffsets_NavigationPoint.startPath); }
	BitfieldBool taken() { return BoolValue(PropOffsets_NavigationPoint.taken); }
	FixedArrayView<int, 16> upstreamPaths() { return FixedArray<int, 16>(PropOffsets_NavigationPoint.upstreamPaths); }
	int& visitedWeight() { return Value<int>(PropOffsets_NavigationPoint.visitedWeight); }
};

class ULiftExit : public UNavigationPoint
{
public:
	using UNavigationPoint::UNavigationPoint;

	float& LastTriggerTime() { return Value<float>(PropOffsets_LiftExit.LastTriggerTime); }
	NameString& LiftTag() { return Value<NameString>(PropOffsets_LiftExit.LiftTag); }
	NameString& LiftTrigger() { return Value<NameString>(PropOffsets_LiftExit.LiftTrigger); }
	UMover*& MyLift() { return Value<UMover*>(PropOffsets_LiftExit.MyLift); }
	UTrigger*& RecommendedTrigger() { return Value<UTrigger*>(PropOffsets_LiftExit.RecommendedTrigger); }
};

class ULiftCenter : public UNavigationPoint
{
public:
	using UNavigationPoint::UNavigationPoint;

	float& LastTriggerTime() { return Value<float>(PropOffsets_LiftCenter.LastTriggerTime); }
	vec3& LiftOffset() { return Value<vec3>(PropOffsets_LiftCenter.LiftOffset); }
	NameString& LiftTag() { return Value<NameString>(PropOffsets_LiftCenter.LiftTag); }
	NameString& LiftTrigger() { return Value<NameString>(PropOffsets_LiftCenter.LiftTrigger); }
	float& MaxDist2D() { return Value<float>(PropOffsets_LiftCenter.MaxDist2D); }
	float& MaxZDiffAdd() { return Value<float>(PropOffsets_LiftCenter.MaxZDiffAdd); }
	UMover*& MyLift() { return Value<UMover*>(PropOffsets_LiftCenter.MyLift); }
	UTrigger*& RecommendedTrigger() { return Value<UTrigger*>(PropOffsets_LiftCenter.RecommendedTrigger); }
};

class UWarpZoneMarker : public UNavigationPoint
{
public:
	using UNavigationPoint::UNavigationPoint;

	UActor*& TriggerActor() { return Value<UActor*>(PropOffsets_WarpZoneMarker.TriggerActor); }
	UActor*& TriggerActor2() { return Value<UActor*>(PropOffsets_WarpZoneMarker.TriggerActor2); }
	UWarpZoneInfo*& markedWarpZone() { return Value<UWarpZoneInfo*>(PropOffsets_WarpZoneMarker.markedWarpZone); }
};

class UInventorySpot : public UNavigationPoint
{
public:
	using UNavigationPoint::UNavigationPoint;

	UInventory*& markedItem() { return Value<UInventory*>(PropOffsets_InventorySpot.markedItem); }
};

class UTriggerMarker : public UNavigationPoint
{
public:
	using UNavigationPoint::UNavigationPoint;
};

class UButtonMarker : public UNavigationPoint
{
public:
	using UNavigationPoint::UNavigationPoint;
};

class UPlayerStart : public UNavigationPoint
{
public:
	using UNavigationPoint::UNavigationPoint;

	uint8_t& TeamNumber() { return Value<uint8_t>(PropOffsets_PlayerStart.TeamNumber); }
	BitfieldBool bCoopStart() { return BoolValue(PropOffsets_PlayerStart.bCoopStart); }
	BitfieldBool bEnabled() { return BoolValue(PropOffsets_PlayerStart.bEnabled); }
	BitfieldBool bSinglePlayerStart() { return BoolValue(PropOffsets_PlayerStart.bSinglePlayerStart); }
};

class UTeleporter : public UNavigationPoint
{
public:
	using UNavigationPoint::UNavigationPoint;

	float& LastFired() { return Value<float>(PropOffsets_Teleporter.LastFired); }
	NameString& ProductRequired() { return Value<NameString>(PropOffsets_Teleporter.ProductRequired); }
	vec3& TargetVelocity() { return Value<vec3>(PropOffsets_Teleporter.TargetVelocity); }
	UActor*& TriggerActor() { return Value<UActor*>(PropOffsets_Teleporter.TriggerActor); }
	UActor*& TriggerActor2() { return Value<UActor*>(PropOffsets_Teleporter.TriggerActor2); }
	std::string& URL() { return Value<std::string>(PropOffsets_Teleporter.URL); }
	BitfieldBool bChangesVelocity() { return BoolValue(PropOffsets_Teleporter.bChangesVelocity); }
	BitfieldBool bChangesYaw() { return BoolValue(PropOffsets_Teleporter.bChangesYaw); }
	BitfieldBool bEnabled() { return BoolValue(PropOffsets_Teleporter.bEnabled); }
	BitfieldBool bReversesX() { return BoolValue(PropOffsets_Teleporter.bReversesX); }
	BitfieldBool bReversesY() { return BoolValue(PropOffsets_Teleporter.bReversesY); }
	BitfieldBool bReversesZ() { return BoolValue(PropOffsets_Teleporter.bReversesZ); }
};

class UPathNode : public UNavigationPoint
{
public:
	using UNavigationPoint::UNavigationPoint;
};

class UDecoration : public UActor
{
public:
	using UActor::UActor;

	UClass*& EffectWhenDestroyed() { return Value<UClass*>(PropOffsets_Decoration.EffectWhenDestroyed); }
	USound*& EndPushSound() { return Value<USound*>(PropOffsets_Decoration.EndPushSound); }
	USound*& PushSound() { return Value<USound*>(PropOffsets_Decoration.PushSound); }
	BitfieldBool bBobbing() { return BoolValue(PropOffsets_Decoration.bBobbing); }
	BitfieldBool bOnlyTriggerable() { return BoolValue(PropOffsets_Decoration.bOnlyTriggerable); }
	BitfieldBool bPushSoundPlaying() { return BoolValue(PropOffsets_Decoration.bPushSoundPlaying); }
	BitfieldBool bPushable() { return BoolValue(PropOffsets_Decoration.bPushable); }
	BitfieldBool bSplash() { return BoolValue(PropOffsets_Decoration.bSplash); }
	BitfieldBool bWasCarried() { return BoolValue(PropOffsets_Decoration.bWasCarried); }
	UClass*& content2() { return Value<UClass*>(PropOffsets_Decoration.content2); }
	UClass*& content3() { return Value<UClass*>(PropOffsets_Decoration.content3); }
	UClass*& contents() { return Value<UClass*>(PropOffsets_Decoration.contents); }
	int& numLandings() { return Value<int>(PropOffsets_Decoration.numLandings); }
};

class UCarcass : public UDecoration
{
public:
	using UDecoration::UDecoration;

	UPawn*& Bugs() { return Value<UPawn*>(PropOffsets_Carcass.Bugs); }
	int& CumulativeDamage() { return Value<int>(PropOffsets_Carcass.CumulativeDamage); }
	UPlayerReplicationInfo*& PlayerOwner() { return Value<UPlayerReplicationInfo*>(PropOffsets_Carcass.PlayerOwner); }
	BitfieldBool bDecorative() { return BoolValue(PropOffsets_Carcass.bDecorative); }
	BitfieldBool bPlayerCarcass() { return BoolValue(PropOffsets_Carcass.bPlayerCarcass); }
	BitfieldBool bReducedHeight() { return BoolValue(PropOffsets_Carcass.bReducedHeight); }
	BitfieldBool bSlidingCarcass() { return BoolValue(PropOffsets_Carcass.bSlidingCarcass); }
	uint8_t& flies() { return Value<uint8_t>(PropOffsets_Carcass.flies); }
	uint8_t& rats() { return Value<uint8_t>(PropOffsets_Carcass.rats); }
};

class UProjectile : public UActor
{
public:
	using UActor::UActor;

	float& Damage() { return Value<float>(PropOffsets_Projectile.Damage); }
	float& ExploWallOut() { return Value<float>(PropOffsets_Projectile.ExploWallOut); }
	UClass*& ExplosionDecal() { return Value<UClass*>(PropOffsets_Projectile.ExplosionDecal); }
	USound*& ImpactSound() { return Value<USound*>(PropOffsets_Projectile.ImpactSound); }
	float& MaxSpeed() { return Value<float>(PropOffsets_Projectile.MaxSpeed); }
	USound*& MiscSound() { return Value<USound*>(PropOffsets_Projectile.MiscSound); }
	int& MomentumTransfer() { return Value<int>(PropOffsets_Projectile.MomentumTransfer); }
	NameString& MyDamageType() { return Value<NameString>(PropOffsets_Projectile.MyDamageType); }
	USound*& SpawnSound() { return Value<USound*>(PropOffsets_Projectile.SpawnSound); }
	float& speed() { return Value<float>(PropOffsets_Projectile.speed); }
};

class UKeypoint : public UActor
{
public:
	using UActor::UActor;
};

class Ulocationid : public UKeypoint
{
public:
	using UKeypoint::UKeypoint;

	std::string& LocationName() { return Value<std::string>(PropOffsets_locationid.LocationName); }
	Ulocationid*& NextLocation() { return Value<Ulocationid*>(PropOffsets_locationid.NextLocation); }
	float& Radius() { return Value<float>(PropOffsets_locationid.Radius); }
};

class UInterpolationPoint : public UKeypoint
{
public:
	using UKeypoint::UKeypoint;

	float& FovModifier() { return Value<float>(PropOffsets_InterpolationPoint.FovModifier); }
	float& GameSpeedModifier() { return Value<float>(PropOffsets_InterpolationPoint.GameSpeedModifier); }
	UInterpolationPoint*& Next() { return Value<UInterpolationPoint*>(PropOffsets_InterpolationPoint.Next); }
	int& Position() { return Value<int>(PropOffsets_InterpolationPoint.Position); }
	UInterpolationPoint*& Prev() { return Value<UInterpolationPoint*>(PropOffsets_InterpolationPoint.Prev); }
	float& RateModifier() { return Value<float>(PropOffsets_InterpolationPoint.RateModifier); }
	vec3& ScreenFlashFog() { return Value<vec3>(PropOffsets_InterpolationPoint.ScreenFlashFog); }
	float& ScreenFlashScale() { return Value<float>(PropOffsets_InterpolationPoint.ScreenFlashScale); }
	BitfieldBool bEndOfPath() { return BoolValue(PropOffsets_InterpolationPoint.bEndOfPath); }
	BitfieldBool bSkipNextPath() { return BoolValue(PropOffsets_InterpolationPoint.bSkipNextPath); }
};

class UTriggers : public UActor
{
public:
	using UActor::UActor;
};

class UTrigger : public UTriggers
{
public:
	using UTriggers::UTriggers;

	UClass*& ClassProximityType() { return Value<UClass*>(PropOffsets_Trigger.ClassProximityType); }
	float& DamageThreshold() { return Value<float>(PropOffsets_Trigger.DamageThreshold); }
	std::string& Message() { return Value<std::string>(PropOffsets_Trigger.Message); }
	float& ReTriggerDelay() { return Value<float>(PropOffsets_Trigger.ReTriggerDelay); }
	float& RepeatTriggerTime() { return Value<float>(PropOffsets_Trigger.RepeatTriggerTime); }
	UActor*& TriggerActor() { return Value<UActor*>(PropOffsets_Trigger.TriggerActor); }
	UActor*& TriggerActor2() { return Value<UActor*>(PropOffsets_Trigger.TriggerActor2); }
	float& TriggerTime() { return Value<float>(PropOffsets_Trigger.TriggerTime); }
	uint8_t& TriggerType() { return Value<uint8_t>(PropOffsets_Trigger.TriggerType); }
	BitfieldBool bInitiallyActive() { return BoolValue(PropOffsets_Trigger.bInitiallyActive); }
	BitfieldBool bTriggerOnceOnly() { return BoolValue(PropOffsets_Trigger.bTriggerOnceOnly); }
};

class UHUD : public UActor
{
public:
	using UActor::UActor;

	int& Crosshair() { return Value<int>(PropOffsets_HUD.Crosshair); }
	std::string& HUDConfigWindowType() { return Value<std::string>(PropOffsets_HUD.HUDConfigWindowType); }
	UMutator*& HUDMutator() { return Value<UMutator*>(PropOffsets_HUD.HUDMutator); }
	int& HudMode() { return Value<int>(PropOffsets_HUD.HudMode); }
	UMenu*& MainMenu() { return Value<UMenu*>(PropOffsets_HUD.MainMenu); }
	UClass*& MainMenuType() { return Value<UClass*>(PropOffsets_HUD.MainMenuType); }
	UPlayerPawn*& PlayerOwner() { return Value<UPlayerPawn*>(PropOffsets_HUD.PlayerOwner); }
	Color& WhiteColor() { return Value<Color>(PropOffsets_HUD.WhiteColor); }
};

class UMenu : public UActor
{
public:
	using UActor::UActor;

	std::string& CenterString() { return Value<std::string>(PropOffsets_Menu.CenterString); }
	std::string& DisabledString() { return Value<std::string>(PropOffsets_Menu.DisabledString); }
	std::string& EnabledString() { return Value<std::string>(PropOffsets_Menu.EnabledString); }
	FixedArrayView<std::optional<std::string>, 24> HelpMessage() { return FixedArray<std::optional<std::string>, 24>(PropOffsets_Menu.HelpMessage); }
	std::string& LeftString() { return Value<std::string>(PropOffsets_Menu.LeftString); }
	int& MenuLength() { return Value<int>(PropOffsets_Menu.MenuLength); }
	FixedArrayView<std::optional<std::string>, 24> MenuList() { return FixedArray<std::optional<std::string>, 24>(PropOffsets_Menu.MenuList); }
	std::string& MenuTitle() { return Value<std::string>(PropOffsets_Menu.MenuTitle); }
	std::string& NoString() { return Value<std::string>(PropOffsets_Menu.NoString); }
	UMenu*& ParentMenu() { return Value<UMenu*>(PropOffsets_Menu.ParentMenu); }
	UPlayerPawn*& PlayerOwner() { return Value<UPlayerPawn*>(PropOffsets_Menu.PlayerOwner); }
	std::string& RightString() { return Value<std::string>(PropOffsets_Menu.RightString); }
	int& Selection() { return Value<int>(PropOffsets_Menu.Selection); }
	std::string& YesString() { return Value<std::string>(PropOffsets_Menu.YesString); }
	BitfieldBool bConfigChanged() { return BoolValue(PropOffsets_Menu.bConfigChanged); }
	BitfieldBool bExitAllMenus() { return BoolValue(PropOffsets_Menu.bExitAllMenus); }
};

class UInfo : public UActor
{
public:
	using UActor::UActor;
};

class UMutator : public UInfo
{
public:
	using UInfo::UInfo;

	UClass*& DefaultWeapon() { return Value<UClass*>(PropOffsets_Mutator.DefaultWeapon); }
	UMutator*& NextDamageMutator() { return Value<UMutator*>(PropOffsets_Mutator.NextDamageMutator); }
	UMutator*& NextHUDMutator() { return Value<UMutator*>(PropOffsets_Mutator.NextHUDMutator); }
	UMutator*& NextMessageMutator() { return Value<UMutator*>(PropOffsets_Mutator.NextMessageMutator); }
	UMutator*& NextMutator() { return Value<UMutator*>(PropOffsets_Mutator.NextMutator); }
	BitfieldBool bHUDMutator() { return BoolValue(PropOffsets_Mutator.bHUDMutator); }
};

class UGameInfo : public UInfo
{
public:
	using UInfo::UInfo;

	std::string& AdminPassword() { return Value<std::string>(PropOffsets_GameInfo.AdminPassword); }
	float& AutoAim() { return Value<float>(PropOffsets_GameInfo.AutoAim); }
	UMutator*& BaseMutator() { return Value<UMutator*>(PropOffsets_GameInfo.BaseMutator); }
	std::string& BeaconName() { return Value<std::string>(PropOffsets_GameInfo.BeaconName); }
	std::string& BotMenuType() { return Value<std::string>(PropOffsets_GameInfo.BotMenuType); }
	int& CurrentID() { return Value<int>(PropOffsets_GameInfo.CurrentID); }
	UClass*& DMMessageClass() { return Value<UClass*>(PropOffsets_GameInfo.DMMessageClass); }
	UMutator*& DamageMutator() { return Value<UMutator*>(PropOffsets_GameInfo.DamageMutator); }
	UClass*& DeathMessageClass() { return Value<UClass*>(PropOffsets_GameInfo.DeathMessageClass); }
	UClass*& DefaultPlayerClass() { return Value<UClass*>(PropOffsets_GameInfo.DefaultPlayerClass); }
	std::string& DefaultPlayerName() { return Value<std::string>(PropOffsets_GameInfo.DefaultPlayerName); }
	NameString& DefaultPlayerState() { return Value<NameString>(PropOffsets_GameInfo.DefaultPlayerState); }
	UClass*& DefaultWeapon() { return Value<UClass*>(PropOffsets_GameInfo.DefaultWeapon); }
	int& DemoBuild() { return Value<int>(PropOffsets_GameInfo.DemoBuild); }
	int& DemoHasTuts() { return Value<int>(PropOffsets_GameInfo.DemoHasTuts); }
	uint8_t& Difficulty() { return Value<uint8_t>(PropOffsets_GameInfo.Difficulty); }
	std::string& EnabledMutators() { return Value<std::string>(PropOffsets_GameInfo.EnabledMutators); }
	std::string& EnteredMessage() { return Value<std::string>(PropOffsets_GameInfo.EnteredMessage); }
	std::string& FailedPlaceMessage() { return Value<std::string>(PropOffsets_GameInfo.FailedPlaceMessage); }
	std::string& FailedSpawnMessage() { return Value<std::string>(PropOffsets_GameInfo.FailedSpawnMessage); }
	std::string& FailedTeamMessage() { return Value<std::string>(PropOffsets_GameInfo.FailedTeamMessage); }
	UClass*& GameMenuType() { return Value<UClass*>(PropOffsets_GameInfo.GameMenuType); }
	std::string& GameName() { return Value<std::string>(PropOffsets_GameInfo.GameName); }
	std::string& GameOptionsMenuType() { return Value<std::string>(PropOffsets_GameInfo.GameOptionsMenuType); }
	std::string& GamePassword() { return Value<std::string>(PropOffsets_GameInfo.GamePassword); }
	UGameReplicationInfo*& GameReplicationInfo() { return Value<UGameReplicationInfo*>(PropOffsets_GameInfo.GameReplicationInfo); }
	UClass*& GameReplicationInfoClass() { return Value<UClass*>(PropOffsets_GameInfo.GameReplicationInfoClass); }
	float& GameSpeed() { return Value<float>(PropOffsets_GameInfo.GameSpeed); }
	std::string& GameUMenuType() { return Value<std::string>(PropOffsets_GameInfo.GameUMenuType); }
	UClass*& HUDType() { return Value<UClass*>(PropOffsets_GameInfo.HUDType); }
	std::string& IPBanned() { return Value<std::string>(PropOffsets_GameInfo.IPBanned); }
	std::string& IPPolicies() { return Value<std::string>(PropOffsets_GameInfo.IPPolicies); }
	int& ItemGoals() { return Value<int>(PropOffsets_GameInfo.ItemGoals); }
	int& KillGoals() { return Value<int>(PropOffsets_GameInfo.KillGoals); }
	std::string& LeftMessage() { return Value<std::string>(PropOffsets_GameInfo.LeftMessage); }
	UStatLog*& LocalLog() { return Value<UStatLog*>(PropOffsets_GameInfo.LocalLog); }
	std::string& LocalLogFileName() { return Value<std::string>(PropOffsets_GameInfo.LocalLogFileName); }
	UClass*& MapListType() { return Value<UClass*>(PropOffsets_GameInfo.MapListType); }
	std::string& MapPrefix() { return Value<std::string>(PropOffsets_GameInfo.MapPrefix); }
	int& MaxPlayers() { return Value<int>(PropOffsets_GameInfo.MaxPlayers); }
	int& MaxSpectators() { return Value<int>(PropOffsets_GameInfo.MaxSpectators); }
	std::string& MaxedOutMessage() { return Value<std::string>(PropOffsets_GameInfo.MaxedOutMessage); }
	UMutator*& MessageMutator() { return Value<UMutator*>(PropOffsets_GameInfo.MessageMutator); }
	std::string& MultiplayerUMenuType() { return Value<std::string>(PropOffsets_GameInfo.MultiplayerUMenuType); }
	UClass*& MutatorClass() { return Value<UClass*>(PropOffsets_GameInfo.MutatorClass); }
	std::string& NameChangedMessage() { return Value<std::string>(PropOffsets_GameInfo.NameChangedMessage); }
	std::string& NeedPassword() { return Value<std::string>(PropOffsets_GameInfo.NeedPassword); }
	int& NumPlayers() { return Value<int>(PropOffsets_GameInfo.NumPlayers); }
	int& NumSpectators() { return Value<int>(PropOffsets_GameInfo.NumSpectators); }
	std::string& RulesMenuType() { return Value<std::string>(PropOffsets_GameInfo.RulesMenuType); }
	UClass*& ScoreBoardType() { return Value<UClass*>(PropOffsets_GameInfo.ScoreBoardType); }
	int& SecretGoals() { return Value<int>(PropOffsets_GameInfo.SecretGoals); }
	int& SentText() { return Value<int>(PropOffsets_GameInfo.SentText); }
	std::string& ServerLogName() { return Value<std::string>(PropOffsets_GameInfo.ServerLogName); }
	std::string& SettingsMenuType() { return Value<std::string>(PropOffsets_GameInfo.SettingsMenuType); }
	std::string& SpecialDamageString() { return Value<std::string>(PropOffsets_GameInfo.SpecialDamageString); }
	float& StartTime() { return Value<float>(PropOffsets_GameInfo.StartTime); }
	UClass*& StatLogClass() { return Value<UClass*>(PropOffsets_GameInfo.StatLogClass); }
	std::string& SwitchLevelMessage() { return Value<std::string>(PropOffsets_GameInfo.SwitchLevelMessage); }
	UClass*& WaterZoneType() { return Value<UClass*>(PropOffsets_GameInfo.WaterZoneType); }
	UStatLog*& WorldLog() { return Value<UStatLog*>(PropOffsets_GameInfo.WorldLog); }
	std::string& WorldLogFileName() { return Value<std::string>(PropOffsets_GameInfo.WorldLogFileName); }
	std::string& WrongPassword() { return Value<std::string>(PropOffsets_GameInfo.WrongPassword); }
	BitfieldBool bAllowFOV() { return BoolValue(PropOffsets_GameInfo.bAllowFOV); }
	BitfieldBool bAlternateMode() { return BoolValue(PropOffsets_GameInfo.bAlternateMode); }
	BitfieldBool bBatchLocal() { return BoolValue(PropOffsets_GameInfo.bBatchLocal); }
	BitfieldBool bCanChangeSkin() { return BoolValue(PropOffsets_GameInfo.bCanChangeSkin); }
	BitfieldBool bCanViewOthers() { return BoolValue(PropOffsets_GameInfo.bCanViewOthers); }
	BitfieldBool bClassicDeathMessages() { return BoolValue(PropOffsets_GameInfo.bClassicDeathMessages); }
	BitfieldBool bCoopWeaponMode() { return BoolValue(PropOffsets_GameInfo.bCoopWeaponMode); }
	BitfieldBool bDeathMatch() { return BoolValue(PropOffsets_GameInfo.bDeathMatch); }
	BitfieldBool bExternalBatcher() { return BoolValue(PropOffsets_GameInfo.bExternalBatcher); }
	BitfieldBool bGameEnded() { return BoolValue(PropOffsets_GameInfo.bGameEnded); }
	BitfieldBool bHumansOnly() { return BoolValue(PropOffsets_GameInfo.bHumansOnly); }
	BitfieldBool bLocalLog() { return BoolValue(PropOffsets_GameInfo.bLocalLog); }
	BitfieldBool bLoggingGame() { return BoolValue(PropOffsets_GameInfo.bLoggingGame); }
	BitfieldBool bLowGore() { return BoolValue(PropOffsets_GameInfo.bLowGore); }
	BitfieldBool bMuteSpectators() { return BoolValue(PropOffsets_GameInfo.bMuteSpectators); }
	BitfieldBool bNoCheating() { return BoolValue(PropOffsets_GameInfo.bNoCheating); }
	BitfieldBool bNoMonsters() { return BoolValue(PropOffsets_GameInfo.bNoMonsters); }
	BitfieldBool bOverTime() { return BoolValue(PropOffsets_GameInfo.bOverTime); }
	BitfieldBool bPauseable() { return BoolValue(PropOffsets_GameInfo.bPauseable); }
	BitfieldBool bRestartLevel() { return BoolValue(PropOffsets_GameInfo.bRestartLevel); }
	BitfieldBool bTeamGame() { return BoolValue(PropOffsets_GameInfo.bTeamGame); }
	BitfieldBool bVeryLowGore() { return BoolValue(PropOffsets_GameInfo.bVeryLowGore); }
	BitfieldBool bWorldLog() { return BoolValue(PropOffsets_GameInfo.bWorldLog); }

	// 227 Additions
	std::string& LastPreloginIP() { return Value<std::string>(PropOffsets_GameInfo.LastPreloginIP); }
	std::string& LastLoginPlayerNames() { return Value<std::string>(PropOffsets_GameInfo.LastLoginPlayerNames); }
	std::string& LastPreloginIdentity() { return Value<std::string>(PropOffsets_GameInfo.LastPreloginIdentity); }
	std::string& LastPreloginIdent() { return Value<std::string>(PropOffsets_GameInfo.LastPreloginIdent); }
	std::string& MaleGender() { return Value<std::string>(PropOffsets_GameInfo.MaleGender); }
	std::string& FemaleGender() { return Value<std::string>(PropOffsets_GameInfo.FemaleGender); }
	// GameRules and AdminAccessManager are not native classes
	// GameRules& GameRules() { return Value<GameRules>(PropOffsets_GameInfo.GameRules); }
	// AdminAccessManager& AccessManager() { return Value<AdminAccessManager>(PropOffsets_GameInfo.AccessManager); }
	std::string& AccessManagerClass() { return Value<std::string>(PropOffsets_GameInfo.AccessManagerClass); }
	int& BleedingDamageMin() { return Value<int>(PropOffsets_GameInfo.BleedingDamageMin); }
	int& BleedingDamageMax() { return Value<int>(PropOffsets_GameInfo.BleedingDamageMax); }

	BitfieldBool bBleedingEnabled() { return BoolValue(PropOffsets_GameInfo.bBleedingEnabled); }
	BitfieldBool bBleedingDamageEnabled() { return BoolValue(PropOffsets_GameInfo.bBleedingDamageEnabled); }
	BitfieldBool bAllHealthStopsBleeding() { return BoolValue(PropOffsets_GameInfo.bAllHealthStopsBleeding); }
	BitfieldBool bBandagesStopBleeding() { return BoolValue(PropOffsets_GameInfo.bBandagesStopBleeding); }
	BitfieldBool bMessageAdminsAliases() { return BoolValue(PropOffsets_GameInfo.bMessageAdminsAliases); }
	BitfieldBool bLogNewPlayerAliases() { return BoolValue(PropOffsets_GameInfo.bLogNewPlayerAliases); }
	BitfieldBool bLogDownloadsToClient() { return BoolValue(PropOffsets_GameInfo.bLogDownloadsToClient); }
	BitfieldBool bHandleDownloadMessaging() { return BoolValue(PropOffsets_GameInfo.bHandleDownloadMessaging); }
	BitfieldBool bShowRecoilAnimations() { return BoolValue(PropOffsets_GameInfo.bShowRecoilAnimations); }
	BitfieldBool bCastShadow() { return BoolValue(PropOffsets_GameInfo.bCastShadow); }
	BitfieldBool bDecoShadows() { return BoolValue(PropOffsets_GameInfo.bDecoShadows); }
	BitfieldBool bCastProjectorShadows() { return BoolValue(PropOffsets_GameInfo.bCastProjectorShadows); }
	BitfieldBool bUseRealtimeShadow() { return BoolValue(PropOffsets_GameInfo.bUseRealtimeShadow); }
	BitfieldBool bNoWalkInAir() { return BoolValue(PropOffsets_GameInfo.bNoWalkInAir); }
	BitfieldBool bProjectorDecals() { return BoolValue(PropOffsets_GameInfo.bProjectorDecals); }
	BitfieldBool bIsSavedGame() { return BoolValue(PropOffsets_GameInfo.bIsSavedGame); }
	BitfieldBool bAlwaysEnhancedSightCheck() { return BoolValue(PropOffsets_GameInfo.bAlwaysEnhancedSightCheck); }
};

class USavedMove : public UInfo
{
public:
	using UInfo::UInfo;

	float& Delta() { return Value<float>(PropOffsets_SavedMove.Delta); }
	uint8_t& DodgeMove() { return Value<uint8_t>(PropOffsets_SavedMove.DodgeMove); }
	USavedMove*& NextMove() { return Value<USavedMove*>(PropOffsets_SavedMove.NextMove); }
	float& TimeStamp() { return Value<float>(PropOffsets_SavedMove.TimeStamp); }
	BitfieldBool bAltFire() { return BoolValue(PropOffsets_SavedMove.bAltFire); }
	BitfieldBool bDuck() { return BoolValue(PropOffsets_SavedMove.bDuck); }
	BitfieldBool bFire() { return BoolValue(PropOffsets_SavedMove.bFire); }
	BitfieldBool bForceAltFire() { return BoolValue(PropOffsets_SavedMove.bForceAltFire); }
	BitfieldBool bForceFire() { return BoolValue(PropOffsets_SavedMove.bForceFire); }
	BitfieldBool bPressedJump() { return BoolValue(PropOffsets_SavedMove.bPressedJump); }
	BitfieldBool bRun() { return BoolValue(PropOffsets_SavedMove.bRun); }
};

class UInternetInfo : public UInfo
{
public:
	using UInfo::UInfo;
};

class UZoneInfo : public UInfo
{
public:
	using UInfo::UInfo;

	uint8_t& AmbientBrightness() { return Value<uint8_t>(PropOffsets_ZoneInfo.AmbientBrightness); }
	uint8_t& AmbientHue() { return Value<uint8_t>(PropOffsets_ZoneInfo.AmbientHue); }
	uint8_t& AmbientSaturation() { return Value<uint8_t>(PropOffsets_ZoneInfo.AmbientSaturation); }
	int& CutoffHz() { return Value<int>(PropOffsets_ZoneInfo.CutoffHz); }
	int& DamagePerSec() { return Value<int>(PropOffsets_ZoneInfo.DamagePerSec); }
	std::string& DamageString() { return Value<std::string>(PropOffsets_ZoneInfo.DamageString); }
	NameString& DamageType() { return Value<NameString>(PropOffsets_ZoneInfo.DamageType); }
	FixedArrayView<uint8_t, 6> Delay() { return FixedArray<uint8_t, 6>(PropOffsets_ZoneInfo.Delay); }
	UClass*& EntryActor() { return Value<UClass*>(PropOffsets_ZoneInfo.EntryActor); }
	USound*& EntrySound() { return Value<USound*>(PropOffsets_ZoneInfo.EntrySound); }
	UTexture*& EnvironmentMap() { return Value<UTexture*>(PropOffsets_ZoneInfo.EnvironmentMap); }
	UClass*& ExitActor() { return Value<UClass*>(PropOffsets_ZoneInfo.ExitActor); }
	USound*& ExitSound() { return Value<USound*>(PropOffsets_ZoneInfo.ExitSound); }
	Color& FogColor() { return Value<Color>(PropOffsets_ZoneInfo.FogColor); }
	float& FogDistance() { return Value<float>(PropOffsets_ZoneInfo.FogDistance); }
	FixedArrayView<uint8_t, 6> Gain() { return FixedArray<uint8_t, 6>(PropOffsets_ZoneInfo.Gain); }
	FixedArrayView<UTexture*, 12> LensFlare() { return FixedArray<UTexture*, 12>(PropOffsets_ZoneInfo.LensFlare); }
	FixedArrayView<float, 12> LensFlareOffset() { return FixedArray<float, 12>(PropOffsets_ZoneInfo.LensFlareOffset); }
	FixedArrayView<float, 12> LensFlareScale() { return FixedArray<float, 12>(PropOffsets_ZoneInfo.LensFlareScale); }
	uint8_t& MasterGain() { return Value<uint8_t>(PropOffsets_ZoneInfo.MasterGain); }
	int& MaxCarcasses() { return Value<int>(PropOffsets_ZoneInfo.MaxCarcasses); }
	uint8_t& MaxLightCount() { return Value<uint8_t>(PropOffsets_ZoneInfo.MaxLightCount); }
	int& MaxLightingPolyCount() { return Value<int>(PropOffsets_ZoneInfo.MaxLightingPolyCount); }
	uint8_t& MinLightCount() { return Value<uint8_t>(PropOffsets_ZoneInfo.MinLightCount); }
	int& MinLightingPolyCount() { return Value<int>(PropOffsets_ZoneInfo.MinLightingPolyCount); }
	int& NumCarcasses() { return Value<int>(PropOffsets_ZoneInfo.NumCarcasses); }
	USkyZoneInfo*& SkyZone() { return Value<USkyZoneInfo*>(PropOffsets_ZoneInfo.SkyZone); }
	float& SpeedOfSound() { return Value<float>(PropOffsets_ZoneInfo.SpeedOfSound); }
	float& TexUPanSpeed() { return Value<float>(PropOffsets_ZoneInfo.TexUPanSpeed); }
	float& TexVPanSpeed() { return Value<float>(PropOffsets_ZoneInfo.TexVPanSpeed); }
	vec3& ViewFlash() { return Value<vec3>(PropOffsets_ZoneInfo.ViewFlash); }
	vec3& ViewFog() { return Value<vec3>(PropOffsets_ZoneInfo.ViewFog); }
	float& ZoneFluidFriction() { return Value<float>(PropOffsets_ZoneInfo.ZoneFluidFriction); }
	vec3& ZoneGravity() { return Value<vec3>(PropOffsets_ZoneInfo.ZoneGravity); }
	float& ZoneGroundFriction() { return Value<float>(PropOffsets_ZoneInfo.ZoneGroundFriction); }
	std::string& ZoneName() { return Value<std::string>(PropOffsets_ZoneInfo.ZoneName); }
	int& ZonePlayerCount() { return Value<int>(PropOffsets_ZoneInfo.ZonePlayerCount); }
	NameString& ZonePlayerEvent() { return Value<NameString>(PropOffsets_ZoneInfo.ZonePlayerEvent); }
	NameString& ZoneTag() { return Value<NameString>(PropOffsets_ZoneInfo.ZoneTag); }
	float& ZoneTerminalVelocity() { return Value<float>(PropOffsets_ZoneInfo.ZoneTerminalVelocity); }
	vec3& ZoneVelocity() { return Value<vec3>(PropOffsets_ZoneInfo.ZoneVelocity); }
	BitfieldBool bBounceVelocity() { return BoolValue(PropOffsets_ZoneInfo.bBounceVelocity); }
	BitfieldBool bDestructive() { return BoolValue(PropOffsets_ZoneInfo.bDestructive); }
	BitfieldBool bFogZone() { return BoolValue(PropOffsets_ZoneInfo.bFogZone); }
	BitfieldBool bGravityZone() { return BoolValue(PropOffsets_ZoneInfo.bGravityZone); }
	BitfieldBool bKillZone() { return BoolValue(PropOffsets_ZoneInfo.bKillZone); }
	BitfieldBool bMoveProjectiles() { return BoolValue(PropOffsets_ZoneInfo.bMoveProjectiles); }
	BitfieldBool bNeutralZone() { return BoolValue(PropOffsets_ZoneInfo.bNeutralZone); }
	BitfieldBool bNoInventory() { return BoolValue(PropOffsets_ZoneInfo.bNoInventory); }
	BitfieldBool bPainZone() { return BoolValue(PropOffsets_ZoneInfo.bPainZone); }
	BitfieldBool bRaytraceReverb() { return BoolValue(PropOffsets_ZoneInfo.bRaytraceReverb); }
	BitfieldBool bReverbZone() { return BoolValue(PropOffsets_ZoneInfo.bReverbZone); }
	BitfieldBool bWaterZone() { return BoolValue(PropOffsets_ZoneInfo.bWaterZone); }
	Ulocationid*& locationid() { return Value<Ulocationid*>(PropOffsets_ZoneInfo.locationid); }
	// 227 additions
	NameString& SkyZoneInfoTag() { return Value<NameString>(PropOffsets_ZoneInfo.SkyZoneInfoTag); }
	NameString& SkyZoneInfoLevelID() { return Value<NameString>(PropOffsets_ZoneInfo.SkyZoneInfoLevelID); }
	float& MinWalkableZ() { return Value<float>(PropOffsets_ZoneInfo.MinWalkableZ); }
	BitfieldBool bDistanceFog() { return BoolValue(PropOffsets_ZoneInfo.bDistanceFog); }
	BitfieldBool bDistanceFogClips() { return BoolValue(PropOffsets_ZoneInfo.bDistanceFogClips); }
	BitfieldBool bRepZoneProperties() { return BoolValue(PropOffsets_ZoneInfo.bRepZoneProperties); }
	BitfieldBool bZoneBasedFog() { return BoolValue(PropOffsets_ZoneInfo.bZoneBasedFog); }
	float& DirtyShadowLevel() { return Value<float>(PropOffsets_ZoneInfo.DirtyShadowLevel); }
	EAmbients EFXAmbients() { return static_cast<EAmbients>(Value<uint8_t>(PropOffsets_ZoneInfo.EFXAmbients)); }
	vec3& EnvironmentColor() { return Value<vec3>(PropOffsets_ZoneInfo.EnvironmentColor); }
	float& EnvironmentUScale() { return Value<float>(PropOffsets_ZoneInfo.EnvironmentUScale); }
	float& EnvironmentVScale() { return Value<float>(PropOffsets_ZoneInfo.EnvironmentVScale); }
	float& FadeTime() { return Value<float>(PropOffsets_ZoneInfo.FadeTime); }
	float& FogDistanceStart() { return Value<float>(PropOffsets_ZoneInfo.FogDistanceStart); }
	FixedArrayView<uint8_t, 4> LightMapDetailLevels() { return FixedArray<uint8_t, 4>(PropOffsets_ZoneInfo.LightMapDetailLevels); }
	float& ZoneTimeDilation() { return Value<float>(PropOffsets_ZoneInfo.ZoneTimeDilation); }
	// VisibilityNotify& VisNotify() { return Value<VisibilityNotify>(PropOffsets_ZoneInfo.VisNotify); }
};

class ULevelInfo : public UZoneInfo
{
public:
	using UZoneInfo::UZoneInfo;

	void UpdateActorZone() override;

	// Unreal 227 addition
	PointRegion GetLocZone(const vec3& pos);

	UnrealURL URL;

	FixedArrayView<int, 8> AIProfile() { return FixedArray<int, 8>(PropOffsets_LevelInfo.AIProfile); }
	std::string& Author() { return Value<std::string>(PropOffsets_LevelInfo.Author); }
	float& AvgAITime() { return Value<float>(PropOffsets_LevelInfo.AvgAITime); }
	float& Brightness() { return Value<float>(PropOffsets_LevelInfo.Brightness); }
	uint8_t& CdTrack() { return Value<uint8_t>(PropOffsets_LevelInfo.CdTrack); }
	std::string& ComputerName() { return Value<std::string>(PropOffsets_LevelInfo.ComputerName); }
	int& Day() { return Value<int>(PropOffsets_LevelInfo.Day); }
	int& DayOfWeek() { return Value<int>(PropOffsets_LevelInfo.DayOfWeek); }
	UClass*& DefaultGameType() { return Value<UClass*>(PropOffsets_LevelInfo.DefaultGameType); }
	UTexture*& DefaultTexture() { return Value<UTexture*>(PropOffsets_LevelInfo.DefaultTexture); }
	std::string& EngineVersion() { return Value<std::string>(PropOffsets_LevelInfo.EngineVersion); }
	UGameInfo*& Game() { return Value<UGameInfo*>(PropOffsets_LevelInfo.Game); }
	int& Hour() { return Value<int>(PropOffsets_LevelInfo.Hour); }
	int& HubStackLevel() { return Value<int>(PropOffsets_LevelInfo.HubStackLevel); }
	std::string& IdealPlayerCount() { return Value<std::string>(PropOffsets_LevelInfo.IdealPlayerCount); }
	uint8_t& LevelAction() { return Value<uint8_t>(PropOffsets_LevelInfo.LevelAction); }
	std::string& LevelEnterText() { return Value<std::string>(PropOffsets_LevelInfo.LevelEnterText); }
	std::string& LocalizedPkg() { return Value<std::string>(PropOffsets_LevelInfo.LocalizedPkg); }
	int& Millisecond() { return Value<int>(PropOffsets_LevelInfo.Millisecond); }
	std::string& MinNetVersion() { return Value<std::string>(PropOffsets_LevelInfo.MinNetVersion); }
	int& Minute() { return Value<int>(PropOffsets_LevelInfo.Minute); }
	int& Month() { return Value<int>(PropOffsets_LevelInfo.Month); }
	UNavigationPoint*& NavigationPointList() { return Value<UNavigationPoint*>(PropOffsets_LevelInfo.NavigationPointList); }
	uint8_t& NetMode() { return Value<uint8_t>(PropOffsets_LevelInfo.NetMode); }
	float& NextSwitchCountdown() { return Value<float>(PropOffsets_LevelInfo.NextSwitchCountdown); }
	std::string& NextURL() { return Value<std::string>(PropOffsets_LevelInfo.NextURL); }
	std::string& Pauser() { return Value<std::string>(PropOffsets_LevelInfo.Pauser); }
	UPawn*& PawnList() { return Value<UPawn*>(PropOffsets_LevelInfo.PawnList); }
	float& PlayerDoppler() { return Value<float>(PropOffsets_LevelInfo.PlayerDoppler); }
	int& RecommendedEnemies() { return Value<int>(PropOffsets_LevelInfo.RecommendedEnemies); }
	int& RecommendedTeammates() { return Value<int>(PropOffsets_LevelInfo.RecommendedTeammates); }
	UTexture*& Screenshot() { return Value<UTexture*>(PropOffsets_LevelInfo.Screenshot); }
	int& Second() { return Value<int>(PropOffsets_LevelInfo.Second); }
	UMusic*& Song() { return Value<UMusic*>(PropOffsets_LevelInfo.Song); }
	uint8_t& SongSection() { return Value<uint8_t>(PropOffsets_LevelInfo.SongSection); }
	USpawnNotify*& SpawnNotify() { return Value<USpawnNotify*>(PropOffsets_LevelInfo.SpawnNotify); }
	ULevelSummary*& Summary() { return Value<ULevelSummary*>(PropOffsets_LevelInfo.Summary); }
	float& TimeDilation() { return Value<float>(PropOffsets_LevelInfo.TimeDilation); }
	float& TimeSeconds() { return Value<float>(PropOffsets_LevelInfo.TimeSeconds); }
	std::string& Title() { return Value<std::string>(PropOffsets_LevelInfo.Title); }
	std::string& VisibleGroups() { return Value<std::string>(PropOffsets_LevelInfo.VisibleGroups); }
	int& Year() { return Value<int>(PropOffsets_LevelInfo.Year); }
	BitfieldBool bAggressiveLOD() { return BoolValue(PropOffsets_LevelInfo.bAggressiveLOD); }
	BitfieldBool bAllowFOV() { return BoolValue(PropOffsets_LevelInfo.bAllowFOV); }
	BitfieldBool bBegunPlay() { return BoolValue(PropOffsets_LevelInfo.bBegunPlay); }
	BitfieldBool bCheckWalkSurfaces() { return BoolValue(PropOffsets_LevelInfo.bCheckWalkSurfaces); }
	BitfieldBool bDropDetail() { return BoolValue(PropOffsets_LevelInfo.bDropDetail); }
	BitfieldBool bHighDetailMode() { return BoolValue(PropOffsets_LevelInfo.bHighDetailMode); }
	BitfieldBool bHumansOnly() { return BoolValue(PropOffsets_LevelInfo.bHumansOnly); }
	BitfieldBool bLonePlayer() { return BoolValue(PropOffsets_LevelInfo.bLonePlayer); }
	BitfieldBool bLowRes() { return BoolValue(PropOffsets_LevelInfo.bLowRes); }
	BitfieldBool bNeverPrecache() { return BoolValue(PropOffsets_LevelInfo.bNeverPrecache); }
	BitfieldBool bNextItems() { return BoolValue(PropOffsets_LevelInfo.bNextItems); }
	BitfieldBool bNoCheating() { return BoolValue(PropOffsets_LevelInfo.bNoCheating); }
	BitfieldBool bPlayersOnly() { return BoolValue(PropOffsets_LevelInfo.bPlayersOnly); }
	BitfieldBool bStartup() { return BoolValue(PropOffsets_LevelInfo.bStartup); }

	// 227 exclusive properties
	BitfieldBool bSupportsRealCrouching() { return BoolValue(PropOffsets_LevelInfo.bSupportsRealCrouching); }
	int& EdBuildOpt() { return Value<int>(PropOffsets_LevelInfo.EdBuildOpt); }
	UMusic*& backup_Song() { return Value<UMusic*>(PropOffsets_LevelInfo.backup_Song); }
	uint8_t& backup_SongSection() { return Value<uint8_t>(PropOffsets_LevelInfo.backup_SongSection); }
	UTexture*& WhiteTexture() { return Value<UTexture*>(PropOffsets_LevelInfo.WhiteTexture); }
	UTexture*& TemplateLightTex() { return Value<UTexture*>(PropOffsets_LevelInfo.TemplateLightTex); }
	std::string& EngineSubVersion() { return Value<std::string>(PropOffsets_LevelInfo.EngineSubVersion); }
	// FootStepManager is not a native class
	TypedScriptArray<UObject*> ObjList() { return DynamicArray<UObject*>(PropOffsets_LevelInfo.ObjList); }
	UDynamicZoneInfo*& DynamicZonesList() { return Value<UDynamicZoneInfo*>(PropOffsets_LevelInfo.DynamicZonesList); }
};

class UDynamicZoneInfo : public UZoneInfo
{
	using UZoneInfo::UZoneInfo;

	UDynamicZoneInfo*& NextDynamicZone() { return Value<UDynamicZoneInfo*>(PropOffsets_DynamicZoneInfo.NextDynamicZone); }
	EDynZoneInfoType ZoneAreaType() { return static_cast<EDynZoneInfoType>(Value<uint8_t>(PropOffsets_DynamicZoneInfo.ZoneAreaType)); }
	vec3*& BoxMin() { return Value<vec3*>(PropOffsets_DynamicZoneInfo.BoxMin); }
	vec3*& BoxMax() { return Value<vec3*>(PropOffsets_DynamicZoneInfo.BoxMax); }
	float& CylinderSize() { return Value<float>(PropOffsets_DynamicZoneInfo.CylinderSize); }
	float& SphereSize() { return Value<float>(PropOffsets_DynamicZoneInfo.SphereSize); }
	UZoneInfo*& MatchOnlyZone() { return Value<UZoneInfo*>(PropOffsets_DynamicZoneInfo.MatchOnlyZone); }
	BitfieldBool bUseRelativeToRotation() { return BoolValue(PropOffsets_DynamicZoneInfo.bUseRelativeToRotation); }
	BitfieldBool bMovesForceTouchUpdate() { return BoolValue(PropOffsets_DynamicZoneInfo.bMovesForceTouchUpdate); }
	BitfieldBool bUpdateTouchers() { return BoolValue(PropOffsets_DynamicZoneInfo.bUpdateTouchers); }
	vec3*& OldPose() { return Value<vec3*>(PropOffsets_DynamicZoneInfo.OldPose); }
};

class UWarpZoneInfo : public UZoneInfo
{
public:
	using UZoneInfo::UZoneInfo;

	void Warp(vec3& Loc, vec3& Vel, Rotator& R);
	void UnWarp(vec3& Loc, vec3& Vel, Rotator& R); // Warp but in reverse?
	
	std::string& Destinations() { return Value<std::string>(PropOffsets_WarpZoneInfo.Destinations); }
	UWarpZoneInfo*& OtherSideActor() { return Value<UWarpZoneInfo*>(PropOffsets_WarpZoneInfo.OtherSideActor); }
	UObject*& OtherSideLevel() { return Value<UObject*>(PropOffsets_WarpZoneInfo.OtherSideLevel); }
	std::string& OtherSideURL() { return Value<std::string>(PropOffsets_WarpZoneInfo.OtherSideURL); }
	NameString& ThisTag() { return Value<NameString>(PropOffsets_WarpZoneInfo.ThisTag); }
	Coords& WarpCoords() { return Value<Coords>(PropOffsets_WarpZoneInfo.WarpCoords); }
	BitfieldBool bNoTeleFrag() { return BoolValue(PropOffsets_WarpZoneInfo.bNoTeleFrag); }
	int& iWarpZone() { return Value<int>(PropOffsets_WarpZoneInfo.iWarpZone); }
	int& numDestinations() { return Value<int>(PropOffsets_WarpZoneInfo.numDestinations); }
};

class USkyZoneInfo : public UZoneInfo
{
public:
	using UZoneInfo::UZoneInfo;
};

class UReplicationInfo : public UInfo
{
public:
	using UInfo::UInfo;
};

class UPlayerReplicationInfo : public UReplicationInfo
{
public:
	using UReplicationInfo::UReplicationInfo;

	float& Deaths() { return Value<float>(PropOffsets_PlayerReplicationInfo.Deaths); }
	UDecoration*& HasFlag() { return Value<UDecoration*>(PropOffsets_PlayerReplicationInfo.HasFlag); }
	std::string& OldName() { return Value<std::string>(PropOffsets_PlayerReplicationInfo.OldName); }
	uint8_t& PacketLoss() { return Value<uint8_t>(PropOffsets_PlayerReplicationInfo.PacketLoss); }
	int& Ping() { return Value<int>(PropOffsets_PlayerReplicationInfo.Ping); }
	int& PlayerID() { return Value<int>(PropOffsets_PlayerReplicationInfo.PlayerID); }
	Ulocationid*& PlayerLocation() { return Value<Ulocationid*>(PropOffsets_PlayerReplicationInfo.PlayerLocation); }
	std::string& PlayerName() { return Value<std::string>(PropOffsets_PlayerReplicationInfo.PlayerName); }
	UZoneInfo*& PlayerZone() { return Value<UZoneInfo*>(PropOffsets_PlayerReplicationInfo.PlayerZone); }
	float& Score() { return Value<float>(PropOffsets_PlayerReplicationInfo.Score); }
	int& StartTime() { return Value<int>(PropOffsets_PlayerReplicationInfo.StartTime); }
	UTexture*& TalkTexture() { return Value<UTexture*>(PropOffsets_PlayerReplicationInfo.TalkTexture); }
	uint8_t& Team() { return Value<uint8_t>(PropOffsets_PlayerReplicationInfo.Team); }
	int& TeamID() { return Value<int>(PropOffsets_PlayerReplicationInfo.TeamID); }
	std::string& TeamName() { return Value<std::string>(PropOffsets_PlayerReplicationInfo.TeamName); }
	int& TimeAcc() { return Value<int>(PropOffsets_PlayerReplicationInfo.TimeAcc); }
	UClass*& VoiceType() { return Value<UClass*>(PropOffsets_PlayerReplicationInfo.VoiceType); }
	BitfieldBool bAdmin() { return BoolValue(PropOffsets_PlayerReplicationInfo.bAdmin); }
	BitfieldBool bFeigningDeath() { return BoolValue(PropOffsets_PlayerReplicationInfo.bFeigningDeath); }
	BitfieldBool bIsABot() { return BoolValue(PropOffsets_PlayerReplicationInfo.bIsABot); }
	BitfieldBool bIsFemale() { return BoolValue(PropOffsets_PlayerReplicationInfo.bIsFemale); }
	BitfieldBool bIsSpectator() { return BoolValue(PropOffsets_PlayerReplicationInfo.bIsSpectator); }
	BitfieldBool bWaitingPlayer() { return BoolValue(PropOffsets_PlayerReplicationInfo.bWaitingPlayer); }
};

class UGameReplicationInfo : public UReplicationInfo
{
public:
	using UReplicationInfo::UReplicationInfo;

	std::string& AdminEmail() { return Value<std::string>(PropOffsets_GameReplicationInfo.AdminEmail); }
	std::string& AdminName() { return Value<std::string>(PropOffsets_GameReplicationInfo.AdminName); }
	int& ElapsedTime() { return Value<int>(PropOffsets_GameReplicationInfo.ElapsedTime); }
	std::string& GameClass() { return Value<std::string>(PropOffsets_GameReplicationInfo.GameClass); }
	std::string& GameEndedComments() { return Value<std::string>(PropOffsets_GameReplicationInfo.GameEndedComments); }
	std::string& GameName() { return Value<std::string>(PropOffsets_GameReplicationInfo.GameName); }
	std::string& MOTDLine1() { return Value<std::string>(PropOffsets_GameReplicationInfo.MOTDLine1); }
	std::string& MOTDLine2() { return Value<std::string>(PropOffsets_GameReplicationInfo.MOTDLine2); }
	std::string& MOTDLine3() { return Value<std::string>(PropOffsets_GameReplicationInfo.MOTDLine3); }
	std::string& MOTDLine4() { return Value<std::string>(PropOffsets_GameReplicationInfo.MOTDLine4); }
	int& NumPlayers() { return Value<int>(PropOffsets_GameReplicationInfo.NumPlayers); }
	UPlayerReplicationInfo*& PRIArray() { return Value<UPlayerReplicationInfo*>(PropOffsets_GameReplicationInfo.PRIArray); }
	int& Region() { return Value<int>(PropOffsets_GameReplicationInfo.Region); }
	int& RemainingMinute() { return Value<int>(PropOffsets_GameReplicationInfo.RemainingMinute); }
	int& RemainingTime() { return Value<int>(PropOffsets_GameReplicationInfo.RemainingTime); }
	float& SecondCount() { return Value<float>(PropOffsets_GameReplicationInfo.SecondCount); }
	std::string& ServerName() { return Value<std::string>(PropOffsets_GameReplicationInfo.ServerName); }
	std::string& ShortName() { return Value<std::string>(PropOffsets_GameReplicationInfo.ShortName); }
	int& SumFrags() { return Value<int>(PropOffsets_GameReplicationInfo.SumFrags); }
	float& UpdateTimer() { return Value<float>(PropOffsets_GameReplicationInfo.UpdateTimer); }
	BitfieldBool bClassicDeathMessages() { return BoolValue(PropOffsets_GameReplicationInfo.bClassicDeathMessages); }
	BitfieldBool bStopCountDown() { return BoolValue(PropOffsets_GameReplicationInfo.bStopCountDown); }
	BitfieldBool bTeamGame() { return BoolValue(PropOffsets_GameReplicationInfo.bTeamGame); }
};

class UStatLog : public UInfo
{
public:
	using UInfo::UInfo;

	int& Context() { return Value<int>(PropOffsets_StatLog.Context); }
	std::string& DecoderRingURL() { return Value<std::string>(PropOffsets_StatLog.DecoderRingURL); }
	std::string& GameCreator() { return Value<std::string>(PropOffsets_StatLog.GameCreator); }
	std::string& GameCreatorURL() { return Value<std::string>(PropOffsets_StatLog.GameCreatorURL); }
	std::string& GameName() { return Value<std::string>(PropOffsets_StatLog.GameName); }
	std::string& LocalBatcherParams() { return Value<std::string>(PropOffsets_StatLog.LocalBatcherParams); }
	std::string& LocalBatcherURL() { return Value<std::string>(PropOffsets_StatLog.LocalBatcherURL); }
	std::string& LocalLogDir() { return Value<std::string>(PropOffsets_StatLog.LocalLogDir); }
	std::string& LocalStandard() { return Value<std::string>(PropOffsets_StatLog.LocalStandard); }
	std::string& LocalStatsURL() { return Value<std::string>(PropOffsets_StatLog.LocalStatsURL); }
	std::string& LogInfoURL() { return Value<std::string>(PropOffsets_StatLog.LogInfoURL); }
	std::string& LogVersion() { return Value<std::string>(PropOffsets_StatLog.LogVersion); }
	float& TimeStamp() { return Value<float>(PropOffsets_StatLog.TimeStamp); }
	std::string& WorldBatcherParams() { return Value<std::string>(PropOffsets_StatLog.WorldBatcherParams); }
	std::string& WorldBatcherURL() { return Value<std::string>(PropOffsets_StatLog.WorldBatcherURL); }
	std::string& WorldLogDir() { return Value<std::string>(PropOffsets_StatLog.WorldLogDir); }
	std::string& WorldStandard() { return Value<std::string>(PropOffsets_StatLog.WorldStandard); }
	std::string& WorldStatsURL() { return Value<std::string>(PropOffsets_StatLog.WorldStatsURL); }
	BitfieldBool bWorld() { return BoolValue(PropOffsets_StatLog.bWorld); }
	BitfieldBool bWorldBatcherError() { return BoolValue(PropOffsets_StatLog.bWorldBatcherError); }
};

class UStatLogFile : public UStatLog
{
public:
	using UStatLog::UStatLog;

	int& LogAr() { return Value<int>(PropOffsets_StatLogFile.LogAr); }
	std::string& StatLogFile() { return Value<std::string>(PropOffsets_StatLogFile.StatLogFile); }
	std::string& StatLogFinal() { return Value<std::string>(PropOffsets_StatLogFile.StatLogFinal); }
	BitfieldBool bWatermark() { return BoolValue(PropOffsets_StatLogFile.bWatermark); }
};

class UBrush : public UActor
{
public:
	using UActor::UActor;

	Color& BrushColor() { return Value<Color>(PropOffsets_Brush.BrushColor); }
	uint8_t& CsgOper() { return Value<uint8_t>(PropOffsets_Brush.CsgOper); }
	Scale& MainScale() { return Value<Scale>(PropOffsets_Brush.MainScale); }
	int& PolyFlags() { return Value<int>(PropOffsets_Brush.PolyFlags); }
	vec3& PostPivot() { return Value<vec3>(PropOffsets_Brush.PostPivot); }
	Scale& PostScale() { return Value<Scale>(PropOffsets_Brush.PostScale); }
	Scale& TempScale() { return Value<Scale>(PropOffsets_Brush.TempScale); }
	UObject*& UnusedLightMesh() { return Value<UObject*>(PropOffsets_Brush.UnusedLightMesh); }
	BitfieldBool bColored() { return BoolValue(PropOffsets_Brush.bColored); }
};

class UMover : public UBrush
{
public:
	using UBrush::UBrush;

	vec3& BasePos() { return Value<vec3>(PropOffsets_Mover.BasePos); }
	Rotator& BaseRot() { return Value<Rotator>(PropOffsets_Mover.BaseRot); }
	uint8_t& BrushRaytraceKey() { return Value<uint8_t>(PropOffsets_Mover.BrushRaytraceKey); }
	NameString& BumpEvent() { return Value<NameString>(PropOffsets_Mover.BumpEvent); }
	uint8_t& BumpType() { return Value<uint8_t>(PropOffsets_Mover.BumpType); }
	int& ClientUpdate() { return Value<int>(PropOffsets_Mover.ClientUpdate); }
	USound*& ClosedSound() { return Value<USound*>(PropOffsets_Mover.ClosedSound); }
	USound*& ClosingSound() { return Value<USound*>(PropOffsets_Mover.ClosingSound); }
	float& DamageThreshold() { return Value<float>(PropOffsets_Mover.DamageThreshold); }
	float& DelayTime() { return Value<float>(PropOffsets_Mover.DelayTime); }
	int& EncroachDamage() { return Value<int>(PropOffsets_Mover.EncroachDamage); }
	UMover*& Follower() { return Value<UMover*>(PropOffsets_Mover.Follower); }
	uint8_t& KeyNum() { return Value<uint8_t>(PropOffsets_Mover.KeyNum); }
	FixedArrayView<vec3, 8> KeyPos() { return FixedArray<vec3, 8>(PropOffsets_Mover.KeyPos); }
	FixedArrayView<Rotator, 8> KeyRot() { return FixedArray<Rotator, 8>(PropOffsets_Mover.KeyRot); }
	UMover*& Leader() { return Value<UMover*>(PropOffsets_Mover.Leader); }
	USound*& MoveAmbientSound() { return Value<USound*>(PropOffsets_Mover.MoveAmbientSound); }
	float& MoveTime() { return Value<float>(PropOffsets_Mover.MoveTime); }
	uint8_t& MoverEncroachType() { return Value<uint8_t>(PropOffsets_Mover.MoverEncroachType); }
	uint8_t& MoverGlideType() { return Value<uint8_t>(PropOffsets_Mover.MoverGlideType); }
	uint8_t& NumKeys() { return Value<uint8_t>(PropOffsets_Mover.NumKeys); }
	vec3& OldPos() { return Value<vec3>(PropOffsets_Mover.OldPos); }
	vec3& OldPrePivot() { return Value<vec3>(PropOffsets_Mover.OldPrePivot); }
	Rotator& OldRot() { return Value<Rotator>(PropOffsets_Mover.OldRot); }
	USound*& OpenedSound() { return Value<USound*>(PropOffsets_Mover.OpenedSound); }
	USound*& OpeningSound() { return Value<USound*>(PropOffsets_Mover.OpeningSound); }
	float& OtherTime() { return Value<float>(PropOffsets_Mover.OtherTime); }
	NameString& PlayerBumpEvent() { return Value<NameString>(PropOffsets_Mover.PlayerBumpEvent); }
	uint8_t& PrevKeyNum() { return Value<uint8_t>(PropOffsets_Mover.PrevKeyNum); }
	vec3& RealPosition() { return Value<vec3>(PropOffsets_Mover.RealPosition); }
	Rotator& RealRotation() { return Value<Rotator>(PropOffsets_Mover.RealRotation); }
	UTrigger*& RecommendedTrigger() { return Value<UTrigger*>(PropOffsets_Mover.RecommendedTrigger); }
	NameString& ReturnGroup() { return Value<NameString>(PropOffsets_Mover.ReturnGroup); }
	vec3& SavedPos() { return Value<vec3>(PropOffsets_Mover.SavedPos); }
	Rotator& SavedRot() { return Value<Rotator>(PropOffsets_Mover.SavedRot); }
	UActor*& SavedTrigger() { return Value<UActor*>(PropOffsets_Mover.SavedTrigger); }
	vec3& SimInterpolate() { return Value<vec3>(PropOffsets_Mover.SimInterpolate); }
	vec3& SimOldPos() { return Value<vec3>(PropOffsets_Mover.SimOldPos); }
	int& SimOldRotPitch() { return Value<int>(PropOffsets_Mover.SimOldRotPitch); }
	int& SimOldRotRoll() { return Value<int>(PropOffsets_Mover.SimOldRotRoll); }
	int& SimOldRotYaw() { return Value<int>(PropOffsets_Mover.SimOldRotYaw); }
	float& StayOpenTime() { return Value<float>(PropOffsets_Mover.StayOpenTime); }
	UActor*& TriggerActor() { return Value<UActor*>(PropOffsets_Mover.TriggerActor); }
	UActor*& TriggerActor2() { return Value<UActor*>(PropOffsets_Mover.TriggerActor2); }
	UPawn*& WaitingPawn() { return Value<UPawn*>(PropOffsets_Mover.WaitingPawn); }
	uint8_t& WorldRaytraceKey() { return Value<uint8_t>(PropOffsets_Mover.WorldRaytraceKey); }
	BitfieldBool bClientPause() { return BoolValue(PropOffsets_Mover.bClientPause); }
	BitfieldBool bDamageTriggered() { return BoolValue(PropOffsets_Mover.bDamageTriggered); }
	BitfieldBool bDelaying() { return BoolValue(PropOffsets_Mover.bDelaying); }
	BitfieldBool bDynamicLightMover() { return BoolValue(PropOffsets_Mover.bDynamicLightMover); }
	BitfieldBool bOpening() { return BoolValue(PropOffsets_Mover.bOpening); }
	BitfieldBool bPlayerOnly() { return BoolValue(PropOffsets_Mover.bPlayerOnly); }
	BitfieldBool bSlave() { return BoolValue(PropOffsets_Mover.bSlave); }
	BitfieldBool bTriggerOnceOnly() { return BoolValue(PropOffsets_Mover.bTriggerOnceOnly); }
	BitfieldBool bUseTriggered() { return BoolValue(PropOffsets_Mover.bUseTriggered); }
	UNavigationPoint*& myMarker() { return Value<UNavigationPoint*>(PropOffsets_Mover.myMarker); }
	int& numTriggerEvents() { return Value<int>(PropOffsets_Mover.numTriggerEvents); }
};

class UPawn : public UActor
{
public:
	using UActor::UActor;

	void Tick(float elapsed) override;
	void TickRotating(float elapsed) override;

	void InitActorZone() override;
	void UpdateActorZone() override;
	void ObserveHarmfulZoneEscapeBoundary(UZoneInfo* oldZone, UZoneInfo* newZone,
		bool footBoundary);
	void BeginHazardSwimEgressFallingTick();
	void CaptureHazardSwimEgressFallingAnchorBeforePhysicsMove(bool directMove = true);
	void CaptureHazardSwimEgressAnchorBeforePhysicsMove();
	void ObserveHazardSwimEgressAfterPhysicsMove();
	void AdvanceHazardSwimEgressLiveSteer();
	void ObserveHazardSwimEgressLiveSteerShadowDecision(
		const BotAI::HazardSwimEgressLiveSteerDecision& decision);
	void EndHazardSwimEgressSwimSession();
	void EndHazardSwimEgressRun();
	void RecordHazardSwimEgressDeath();
	void EndHazardResidenceRun();
	void RecordHazardResidenceDeath();

	void MoveTo(const vec3& newDestination, float speed);
	void MoveToward(UActor* newTarget, float speed);
	void StrafeFacing(const vec3& newDestination, UActor* newTarget);
	void StrafeTo(const vec3& newDestination, const vec3& newFocus);
	void TurnTo(const vec3& newFocus);
	void TurnToward(UActor* newTarget);
	void WaitForLanding();
	void SetMoveDuration(const vec3& deltaMove);
	float GetSpeed();

	bool TickRotateTo(const vec3& target);
	bool TickMoveTo(const vec3& target, float elapsed, UActor* targetActor = nullptr);
	void RecordPainLedgeVeto(const vec3& origin, const vec2& unsafeDirection);
	void ObserveFallingSeamEscapeShadow(const vec3& requestedRemainingDelta,
		const vec3& actualDisplacement, const vec3& firstHitNormal,
		const vec3& secondHitNormal, bool normalDownwardGravity);
	void ObserveWalkingStepPreflightShadow(const vec3& stepUpDelta,
		const vec3& forwardDelta, const vec3& stepDownDelta, int walkingIteration,
		uint64_t invocationToken);
	bool ConfirmWalkingStepPreflightShadow(int walkingIteration, uint64_t invocationToken,
		PawnMovement::LedgeTransition transition);
	void RecordWalkingStepPreflightPositiveDpsVetoOutcome(bool applied);
	bool RecordWalkingHitWallDispatch(const CollisionHit& hit,
		const vec3& velocityBeforeCollision, float minHitWallBeforeCallback,
		int physicsBeforeCallback, PawnMovement::WalkingHitWallContactPhase contactPhase,
		PawnMovement::WalkingHitWallBlockerKind blockerBeforeCallback,
		bool callbackDispatched);
	void SetWalkingHitWallFixtureContactLimit(uint32_t limit)
	{
		WalkingHitWallFixtureContactLimit = limit;
		WalkingHitWallFixtureContactCount = 0;
	}
	void ClearWalkingHitWallFixtureContactLimit()
	{
		WalkingHitWallFixtureContactLimit = 0;
		WalkingHitWallFixtureContactCount = 0;
	}
	std::vector<PawnMovement::WalkingHitWallDispatchDiagnosticRecord>
		DrainWalkingHitWallDispatchDiagnostics();
	void QueueWalkingStepPreflightPositiveDpsVetoAction(
		PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord record);
	void ArmFallingParityRealizedTrace(int walkingIteration, uint64_t invocationToken);
	bool HasActiveFallingParityRealizedModel() const
	{
		return FallingParityRealizedTrace.ModelActive;
	}
	bool HasActiveFallingParityRealizedLifecycle() const
	{
		return FallingParityRealizedTrace.LifecycleActive;
	}
	bool HasFallingParityRealizedContinuity()
	{
		return FallingParityRealizedTrace.LifecycleActive
			&& !bDeleteMe() && Physics() == PHYS_Falling && !bJustTeleported()
			&& FallingParityRealizedTrace.Correlation.LifeGeneration
				== WalkingStepPreflightLifeGeneration;
	}
	void RecordFallingParityRealizedStep(
		PawnMovement::FallingParityRealizedOutcome outcome,
		const PawnMovement::FallingParityRealizedRecord& evidence);
	void ObserveFallingParityRealizedPain();
	void FinishFallingParityRealizedTrace(
		PawnMovement::FallingParityRealizedOutcome outcome);
	std::vector<PawnMovement::FallingParityRealizedRecord>
		DrainFallingParityRealizedRecords();
	void QueueFallingHazardForecastSource(
		PawnMovement::FallingHazardForecastSource source);
	void EnsureFallingHazardGeneration(float physicsSliceElapsed,
		const vec3& acceleration);
	bool PrepareFallingHazardSweep(PawnMovement::FallingHazardSweepLeg leg,
		const vec3& origin, const vec3& delta, float elapsedContribution);
	bool BeginFallingHazardTryMove();
	void EndFallingHazardTryMove();
	void LatchPendingFallingHazardSweepGeometry(
		const CollisionHit& hit, const MoveCallbackEvidence& callbacks);
	void CommitPendingFallingHazardSweepAtCenterBoundary(
		const PointRegion& center, bool actorLeavingCallbackDispatched);
	void CommitPendingFallingHazardSweepAtFootBoundary();
	void RecoverFallingHazardCallbackReturn();
	void CancelPendingFallingHazardSweep(bool callbackBoundary);
	std::optional<PawnMovement::FallingHazardForecastContinuationSeed>
		FinishFallingHazardCallbackBoundary();
	void ArmFallingHazardContinuation(
		PawnMovement::FallingHazardForecastSource source,
		const PawnMovement::FallingHazardForecastContinuationSeed& continuation,
		float physicsSliceElapsed, const vec3& acceleration);
	void CaptureFallingHazardAlignedCommandWitness(bool staticWorldCollision);
	PawnMovement::FallingHazardAlignedCommandProvenance
		ConsumeFallingHazardAlignedCommandWitness();
	void FinishFallingHazardLanding(const CollisionHit& hit,
		bool ditchSupportUnknown = false);
	void FinishFallingHazardDeath();
	bool AdvanceFallingHazardRecovery(float elapsed);
	void RecordFallingHazardRecoveryHarmfulEntry();
	const PawnMovement::FallingHazardRuntimeCounters&
		FallingHazardRuntimeCounterValues() const;
	std::vector<PawnMovement::FallingHazardDiagnosticRecord>
		DrainFallingHazardDiagnostics();
	std::vector<PawnMovement::HazardWaterEgressDiagnosticRecord>
		DrainHazardWaterEgressDiagnostics();
	uint64_t HazardWaterEgressDiagnosticOverflowCount() const;
	uint64_t BeginWalkingStepPreflightInvocation()
	{
		return ++WalkingStepPreflightInvocationSequence;
	}
	bool HasWalkingStepPreflightConfirmation(int walkingIteration,
		uint64_t invocationToken) const
	{
		return WalkingStepPreflightPendingConfirmation
			&& WalkingStepPreflightPendingIteration == walkingIteration
			&& WalkingStepPreflightPendingInvocation == invocationToken;
	}
	void EndWalkingStepPreflightLife();
	std::vector<PawnMovement::WalkingStepPreflightDiagnosticRecord>
		DrainWalkingStepPreflightDiagnostics();
	std::vector<PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord>
		DrainWalkingStepPreflightPositiveDpsVetoActions();
	void RecordInventoryDirectReachSupportObservation(
		PawnMovement::InventoryDirectReachSupportDiagnosticRecord record);
	std::vector<PawnMovement::InventoryDirectReachSupportDiagnosticRecord>
		DrainInventoryDirectReachSupportDiagnostics();
	uint64_t PainLedgeVetoCount() const { return PainLedgeVetoCountValue; }
	uint64_t PainLedgeRepeatVetoCount() const { return PainLedgeRepeatVetoCountValue; }
	uint64_t PainLedgeRecoveryAttemptCount() const { return PainLedgeRecoveryAttemptCountValue; }
	uint64_t PainLedgeRecoveryEscapeCount() const { return PainLedgeRecoveryEscapeCountValue; }
	uint64_t WallAdjustCallCount() const { return WallAdjustCallCountValue; }
	uint64_t WallAdjustRepeatCount() const { return WallAdjustRepeatCountValue; }
	uint64_t WallAdjustRecoveryAttemptCount() const { return WallAdjustRecoveryAttemptCountValue; }
	uint64_t WallAdjustRecoverySuccessCount() const { return WallAdjustRecoverySuccessCountValue; }
	uint64_t WallAdjustForcedReplanCount() const { return WallAdjustForcedReplanCountValue; }
	uint64_t MoveStallDetectionCount() const { return MoveStallDetectionCountValue; }
	uint64_t MoveStallEpisodeResetCount() const { return MoveStallEpisodeResetCountValue; }
	uint64_t MoveStallForcedReplanCount() const { return MoveStallForcedReplanCountValue; }
	uint64_t MoveStallNavigationForcedReplanCount() const { return MoveStallNavigationForcedReplanCountValue; }
	uint64_t MoveStallTargetlessMoveToTimeoutCount() const { return MoveStallTargetlessMoveToTimeoutCountValue; }
	uint64_t MoveStallDirectActorMoveTowardTimeoutCount() const
	{
		return MoveStallDirectActorMoveTowardTimeoutCountValue;
	}
	double MoveStallEligibleSeconds() const { return MoveStallEligibleSecondsValue; }
	uint64_t MoveStallRecoveryEpisodeStartCount() const { return MoveStallRecoveryEpisodeStartCountValue; }
	uint64_t MoveStallRecoveryClearedWithin2SecondsCount() const
	{
		return MoveStallRecoveryClearedWithin2SecondsCountValue;
	}
	uint64_t MoveStallRecoveryClearedAfter2SecondsWithin5SecondsCount() const
	{
		return MoveStallRecoveryClearedAfter2SecondsWithin5SecondsCountValue;
	}
	uint64_t MoveStallRecoveryReplannedWithin5SecondsCount() const
	{
		return MoveStallRecoveryReplannedWithin5SecondsCountValue;
	}
	uint64_t MoveStallRecoveryMissed5SecondDeadlineCount() const
	{
		return MoveStallRecoveryMissed5SecondDeadlineCountValue;
	}
	uint64_t MoveStallRecoveryExcludedIntentionalStopCount() const
	{
		return MoveStallRecoveryExcludedIntentionalStopCountValue;
	}
	uint64_t MoveStallRecoveryCensoredLifeBoundaryCount() const
	{
		return MoveStallRecoveryCensoredLifeBoundaryCountValue;
	}
	uint64_t MoveStallRecoveryCensoredRunEndCount() const
	{
		return MoveStallRecoveryCensoredRunEndCountValue;
	}
	uint64_t MoveStallRecoveryUnknownCount() const { return MoveStallRecoveryUnknownCountValue; }
	uint64_t MoveStallRecoveryEpisodeRecordOverflowCount() const
	{
		return MoveStallRecoveryEpisodeRecordOverflowCountValue;
	}
	uint64_t MoveStallRecoveryDecisionRecordOverflowCount() const
	{
		return MoveStallRecoveryDecisionRecordOverflowCountValue;
	}
	std::vector<PawnMoveStallRecoveryEpisodeRecord>
		DrainMoveStallRecoveryEpisodeRecords();
	std::vector<PawnMoveStallRecoveryDecisionRecord>
		DrainMoveStallRecoveryDecisionRecords();
	void EndMoveStallRecoveryLife();
	void EndMoveStallRecoveryRun();
	uint64_t FailedNavigationAvoidanceActivationCount() const { return FailedNavigationAvoidanceActivationCountValue; }
	uint64_t FailedNavigationSafeguardSuppressionCount() const { return FailedNavigationSafeguardSuppressionCountValue; }
	uint64_t FailedNavigationRoutePenaltyApplicationCount() const { return FailedNavigationRoutePenaltyApplicationCountValue; }
	uint64_t HarmfulZoneEscapeEpisodeCount() const { return HarmfulZoneEscapeEpisodeCountValue; }
	uint64_t HarmfulZoneEscapeCenterEntryCount() const { return HarmfulZoneEscapeCenterEntryCountValue; }
	uint64_t HarmfulZoneEscapeFootEntryCount() const { return HarmfulZoneEscapeFootEntryCountValue; }
	uint64_t HarmfulZoneEscapeRecoveryAttemptCount() const { return HarmfulZoneEscapeRecoveryAttemptCountValue; }
	uint64_t HarmfulZoneEscapeSuccessfulEscapeCount() const { return HarmfulZoneEscapeSuccessfulEscapeCountValue; }
	uint64_t HarmfulZoneEscapeForcedReplanCount() const { return HarmfulZoneEscapeForcedReplanCountValue; }
	uint64_t HarmfulZoneEscapeNoSafeCandidateCount() const { return HarmfulZoneEscapeNoSafeCandidateCountValue; }
	uint64_t HazardSwimEgressEpisodeCount() const { return HazardSwimEgressEpisodeCountValue; }
	uint64_t HazardSwimEgressEligibleCount() const { return HazardSwimEgressEligibleCountValue; }
	uint64_t HazardSwimEgressAuthorizedCount() const { return HazardSwimEgressAuthorizedCountValue; }
	uint64_t HazardSwimEgressDebouncedCount() const { return HazardSwimEgressDebouncedCountValue; }
	uint64_t HazardSwimEgressNoAnchorRejectedCount() const { return HazardSwimEgressNoAnchorRejectedCountValue; }
	uint64_t HazardSwimEgressExitCount() const { return HazardSwimEgressExitCountValue; }
	uint64_t HazardSwimEgressDeathsBeforeExitCount() const { return HazardSwimEgressDeathsBeforeExitCountValue; }
	uint64_t HazardSwimEgressForcedReplanCount() const { return HazardSwimEgressForcedReplanCountValue; }
	uint64_t HazardSwimEgressForcedReplanSameCommandReissuedCount() const
	{
		return HazardSwimEgressForcedReplanSameCommandReissuedCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanDifferentCommandIssuedCount() const
	{
		return HazardSwimEgressForcedReplanDifferentCommandIssuedCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanHazardClearedBeforeCommandCount() const
	{
		return HazardSwimEgressForcedReplanHazardClearedBeforeCommandCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanFellBeforeCommandCount() const
	{
		return HazardSwimEgressForcedReplanFellBeforeCommandCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanDiedBeforeCommandCount() const
	{
		return HazardSwimEgressForcedReplanDiedBeforeCommandCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanLifeBoundaryCensoredCount() const
	{
		return HazardSwimEgressForcedReplanLifeBoundaryCensoredCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanRunEndCensoredCount() const
	{
		return HazardSwimEgressForcedReplanRunEndCensoredCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanEpisodeAbandonedCount() const
	{
		return HazardSwimEgressForcedReplanEpisodeAbandonedCountValue;
	}
	uint64_t HazardSwimEgressFallingPreMoveAnchorCaptureCount() const
	{
		return HazardSwimEgressFallingPreMoveAnchorCaptureCountValue;
	}
	uint64_t HazardSwimEgressFallingPreMoveAnchorUseCount() const
	{
		return HazardSwimEgressFallingPreMoveAnchorUseCountValue;
	}
	uint64_t HazardSwimEgressLiveApplyCount() const { return HazardSwimEgressLiveApplyCountValue; }
	uint64_t HazardSwimEgressLiveActiveTickCount() const { return HazardSwimEgressLiveActiveTickCountValue; }
	uint64_t HazardSwimEgressLiveProbeRejectedCount() const { return HazardSwimEgressLiveProbeRejectedCountValue; }
	uint64_t HazardSwimEgressLiveSuccessfulExitCount() const { return HazardSwimEgressLiveSuccessfulExitCountValue; }
	uint64_t HazardSwimEgressLiveShadowCandidateCount() const { return HazardSwimEgressLiveShadowCandidateCountValue; }
	uint64_t HazardSwimEgressLiveShadowFallingTerminalCount() const { return HazardSwimEgressLiveShadowFallingTerminalCountValue; }
	uint64_t HazardSwimEgressLiveShadowHazardClearedTerminalCount() const { return HazardSwimEgressLiveShadowHazardClearedTerminalCountValue; }
	uint64_t HazardSwimEgressLiveShadowProbeBlockedTerminalCount() const { return HazardSwimEgressLiveShadowProbeBlockedTerminalCountValue; }
	uint64_t HazardSwimEgressDirectNavProbeCount() const { return HazardSwimEgressDirectNavProbeCountValue; }
	uint64_t HazardSwimEgressDirectNavSafeCandidateCount() const { return HazardSwimEgressDirectNavSafeCandidateCountValue; }
	uint64_t HazardResidenceEpisodeCount() const { return HazardResidenceEpisodeCountValue; }
	uint64_t HazardResidenceClearedCount() const { return HazardResidenceClearedCountValue; }
	uint64_t HazardResidenceDeathCount() const { return HazardResidenceDeathCountValue; }
	uint64_t HazardResidenceLifeBoundaryCensoredCount() const { return HazardResidenceLifeBoundaryCensoredCountValue; }
	uint64_t HazardResidenceRunEndCensoredCount() const { return HazardResidenceRunEndCensoredCountValue; }
	uint64_t HazardResidenceUnknownCount() const { return HazardResidenceUnknownCountValue; }
	uint64_t HazardResidenceReentryCount() const { return HazardResidenceReentryCountValue; }
	uint64_t HazardResidenceCommandChangeCount() const { return HazardResidenceCommandChangeCountValue; }
	uint64_t HazardResidenceCandidateObservedCount() const { return HazardResidenceCandidateObservedCountValue; }
	uint64_t HazardResidenceCandidateOtherCommandCount() const { return HazardResidenceCandidateOtherCommandCountValue; }
	uint64_t FallingHazardRecoveryPromotionCount() const { return FallingHazardRecoveryPromotionCountValue; }
	uint64_t FallingHazardRecoveryAdvanceCount() const { return FallingHazardRecoveryAdvanceCountValue; }
	uint64_t FallingHazardRecoveryContextRejectedCount() const { return FallingHazardRecoveryContextRejectedCountValue; }
	uint64_t FallingHazardRecoveryNoActiveFallEpisodeCount() const { return FallingHazardRecoveryNoActiveFallEpisodeCountValue; }
	uint64_t FallingHazardRecoveryNoPrefixCount() const { return FallingHazardRecoveryNoPrefixCountValue; }
	uint64_t FallingHazardRecoveryEligibleCount() const { return FallingHazardRecoveryEligibleCountValue; }
	uint64_t FallingHazardRecoveryAnchorRejectedCount() const { return FallingHazardRecoveryAnchorRejectedCountValue; }
	uint64_t FallingHazardRecoveryProbeRejectedCount() const { return FallingHazardRecoveryProbeRejectedCountValue; }
	uint64_t FallingHazardRecoveryLiveApplyCount() const { return FallingHazardRecoveryLiveApplyCountValue; }
	uint64_t FallingHazardRecoveryLiveActiveTickCount() const { return FallingHazardRecoveryLiveActiveTickCountValue; }
	uint64_t FallingHazardRecoverySafeLandingCount() const { return FallingHazardRecoverySafeLandingCountValue; }
	uint64_t FallingHazardRecoveryHarmfulEntryCount() const { return FallingHazardRecoveryHarmfulEntryCountValue; }
	uint64_t FallingHazardRecoveryDeathCount() const { return FallingHazardRecoveryDeathCountValue; }
	uint64_t FallingHazardRecoveryTimeoutCount() const { return FallingHazardRecoveryTimeoutCountValue; }
	uint64_t ExternalImpulseFallHarmfulWitnessCount() const { return ExternalImpulseFallHarmfulWitnessCountValue; }
	uint64_t ExternalImpulseFallNoAirControlCount() const { return ExternalImpulseFallNoAirControlCountValue; }
	uint64_t ExternalImpulseFallAlternativesTestedCount() const { return ExternalImpulseFallAlternativesTestedCountValue; }
	uint64_t ExternalImpulseFallCertifiedCount() const { return ExternalImpulseFallCertifiedCountValue; }
	uint64_t ExternalImpulseFallUncertifiedCount() const { return ExternalImpulseFallUncertifiedCountValue; }
	const std::string& HazardSwimEgressDirectNavBestCandidateName() const
	{
		return HazardSwimEgress.DirectNavBestCandidateName;
	}
	bool HasHazardSwimEgressAnchor() const { return HazardSwimEgress.AnchorKnown; }
	const char* HazardSwimEgressAnchorSourceName() const;
	uint64_t FallingSeamDetectionCount() const { return FallingSeamDetectionCountValue; }
	uint64_t HorizontalCornerCandidateProbeCount() const { return HorizontalCornerCandidateProbeCountValue; }
	uint64_t HorizontalCornerAuthorizedEscapeCount() const { return HorizontalCornerAuthorizedEscapeCountValue; }
	uint64_t HorizontalCornerTargetProgressRejectCount() const { return HorizontalCornerTargetProgressRejectCountValue; }
	uint64_t HorizontalCornerUnknownOrUnsafeSupportCount() const { return HorizontalCornerUnknownOrUnsafeSupportCountValue; }
	uint64_t FallingSeamEpisodeCount() const { return FallingSeamEpisodeCountValue; }
	uint64_t FallingSeamInvalidGeometryRejectCount() const { return FallingSeamInvalidGeometryRejectCountValue; }
	uint64_t FallingSeamAuthorizableEpisodeCount() const { return FallingSeamAuthorizableEpisodeCountValue; }
	uint64_t HorizontalCornerAuthorizedCandidateCount() const { return HorizontalCornerAuthorizedCandidateCountValue; }
	uint64_t HorizontalCornerBlockedSweepCandidateCount() const { return HorizontalCornerBlockedSweepCandidateCountValue; }
	uint64_t HorizontalCornerNoStaticWalkableSupportCandidateCount() const { return HorizontalCornerNoStaticWalkableSupportCandidateCountValue; }
	uint64_t HorizontalCornerPainSupportCandidateCount() const { return HorizontalCornerPainSupportCandidateCountValue; }
	uint64_t HorizontalCornerNoActiveMovementIntentOrTargetCandidateCount() const { return HorizontalCornerNoActiveMovementIntentOrTargetCandidateCountValue; }
	uint64_t HorizontalCornerTrueTargetRegressionCandidateCount() const { return HorizontalCornerTrueTargetRegressionCandidateCountValue; }
	uint64_t HorizontalCornerUnknownEvidenceCandidateCount() const { return HorizontalCornerUnknownEvidenceCandidateCountValue; }
	uint64_t WalkingStepPreflightObservationCount() const { return WalkingStepPreflightObservationCountValue; }
	uint64_t WalkingStepPreflightUnsupportedEndpointCount() const { return WalkingStepPreflightUnsupportedEndpointCountValue; }
	uint64_t WalkingStepPreflightNoDecisionCount() const { return WalkingStepPreflightNoDecisionCountValue; }
	uint64_t WalkingStepPreflightProvisionalAuthorizationCount() const { return WalkingStepPreflightProvisionalAuthorizationCountValue; }
	uint64_t WalkingStepPreflightAuthorizationCount() const { return WalkingStepPreflightAuthorizationCountValue; }
	uint64_t WalkingStepPreflightAuthorizableEpisodeCount() const { return WalkingStepPreflightAuthorizableEpisodeCountValue; }
	uint64_t WalkingStepPreflightDiagnosticOverflowCount() const { return WalkingStepPreflightDiagnosticOverflowCountValue; }
	uint64_t WalkingHitWallDispatchObservationCount() const { return WalkingHitWallDispatchObservationCountValue; }
	uint64_t WalkingHitWallDispatchLegacyZBandCount() const { return WalkingHitWallDispatchLegacyZBandCountValue; }
	uint64_t WalkingHitWallDispatchMinHitWallCount() const { return WalkingHitWallDispatchMinHitWallCountValue; }
	uint64_t WalkingHitWallDispatchDisagreementCount() const { return WalkingHitWallDispatchDisagreementCountValue; }
	uint64_t WalkingHitWallDispatchCallbackCount() const { return WalkingHitWallDispatchCallbackCountValue; }
	uint64_t WalkingHitWallDispatchDiagnosticOverflowCount() const { return WalkingHitWallDispatchDiagnosticOverflowCountValue; }
	uint64_t WalkingStepPreflightPositiveDpsVetoEligibleCount() const { return WalkingStepPreflightPositiveDpsVetoEligibleCountValue; }
	uint64_t WalkingStepPreflightPositiveDpsVetoAppliedCount() const { return WalkingStepPreflightPositiveDpsVetoAppliedCountValue; }
	uint64_t WalkingStepPreflightPositiveDpsVetoDebouncedCount() const { return WalkingStepPreflightPositiveDpsVetoDebouncedCountValue; }
	uint64_t WalkingStepPreflightPositiveDpsVetoForcedReplanCount() const { return WalkingStepPreflightPositiveDpsVetoForcedReplanCountValue; }
	uint64_t WalkingStepPreflightPositiveDpsVetoRollbackRejectedCount() const { return WalkingStepPreflightPositiveDpsVetoRollbackRejectedCountValue; }
	uint64_t WalkingStepPreflightPositiveDpsVetoActionOverflowCount() const { return WalkingStepPreflightPositiveDpsVetoActionOverflowCountValue; }
	uint64_t InventoryDirectReachSupportObservationCount() const { return InventoryDirectReachSupportObservationCountValue; }
	uint64_t InventoryDirectReachSupportSafeSupportedCount() const { return InventoryDirectReachSupportSafeSupportedCountValue; }
	uint64_t InventoryDirectReachSupportSafeUnsupportedNoObservedHazardCount() const { return InventoryDirectReachSupportSafeUnsupportedNoObservedHazardCountValue; }
	uint64_t InventoryDirectReachSupportUnsafeHarmfulFootZoneCount() const { return InventoryDirectReachSupportUnsafeHarmfulFootZoneCountValue; }
	uint64_t InventoryDirectReachSupportUnsafeUnsupportedOverHarmfulCount() const { return InventoryDirectReachSupportUnsafeUnsupportedOverHarmfulCountValue; }
	uint64_t InventoryDirectReachSupportUnavailableCount() const { return InventoryDirectReachSupportUnavailableCountValue; }
	uint64_t InventoryDirectReachSupportDiagnosticOverflowCount() const { return InventoryDirectReachSupportDiagnosticOverflowCountValue; }
	uint64_t FallingParityRealizedEpisodeCount() const { return FallingParityRealizedEpisodeCountValue; }
	uint64_t FallingParityRealizedStepCount() const { return FallingParityRealizedStepCountValue; }
	uint64_t FallingParityRealizedMatchedStepCount() const { return FallingParityRealizedMatchedStepCountValue; }
	uint64_t FallingParityRealizedMatchedLandingStepCount() const { return FallingParityRealizedMatchedLandingStepCountValue; }
	uint64_t FallingParityRealizedMismatchCount() const { return FallingParityRealizedMismatchCountValue; }
	uint64_t FallingParityRealizedUnknownCount() const { return FallingParityRealizedUnknownCountValue; }
	uint64_t FallingParityRealizedCallbackBarrierCount() const { return FallingParityRealizedCallbackBarrierCountValue; }
	uint64_t FallingParityRealizedPainEntryCount() const { return FallingParityRealizedPainEntryCountValue; }
	uint64_t FallingParityRealizedDeathCount() const { return FallingParityRealizedDeathCountValue; }
	uint64_t FallingParityRealizedLandingCount() const { return FallingParityRealizedLandingCountValue; }
	uint64_t FallingParityRealizedContinuityLossCount() const { return FallingParityRealizedContinuityLossCountValue; }
	uint64_t FallingParityRealizedRecordOverflowCount() const { return FallingParityRealizedRecordOverflowCountValue; }
	const std::array<uint64_t, PawnMovement::WalkingStepPreflightReasonCount>&
		WalkingStepPreflightReasonCounts() const { return WalkingStepPreflightReasonCountValues; }

	// Returns true if any of the several points of other is visible (origin, top, bottom)
	// ignoreDistance is a Deus Ex only parameter, it is always false on Unreal.
	bool LineOfSightTo(UActor* other, bool ignoreDistance);
	// Similar to LineOfSightTo() but takes the Pawn's peripheral vision into account (SightRadius and PeripheralVision)
	bool CanSee(UActor* other);
	bool CanHearNoise(UActor* source, float loudness);
	bool ActorReachable(UActor* anActor, bool checkNavpoint = false);
	bool PointReachable(vec3 aPoint);

	void ClientHearSound(UActor* actor, int id, USound* sound, const vec3& soundLocation, const vec3& parameters);

	// If the obstruction is jumpable, start jumping and keep the destination
	// Otherwise try rotating destination 90 degrees to left and right
	bool PickWallAdjust();

	vec3 EAdjustJump();

	UActor* PickAnyTarget(float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart);
	UActor* PickTarget(float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart);
	bool CheckIfBestTarget(UActor* actor, float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart);

	UActor* PathSpecialHandling(const PawnPathEndPointResult& result,
		PawnMovement::RoutePathCommitOrigin origin);
	UNavigationPoint* SetRouteCache(const Array<UNavigationPoint*>& points);
	PawnPathEndPointResult FindPathToEndPoint(UNavigationPoint* start, int maxNodes);
	UNavigationPoint* CommitRoutePathCache(const PawnPathEndPointResult& result,
		PawnMovement::RoutePathCommitOrigin origin);
	std::vector<PawnMovement::RoutePathCommitRecord> DrainRoutePathCommitRecords();
	uint64_t RoutePathCommitOverflowCount() const { return RoutePathCommitOverflowCountValue; }

	void ClearPaths();
	UObject* FindRandomDest();
	UObject* FindPathTo(const vec3& aPoint, bool bSinglePath);
	UObject* FindPathToward(UObject* anActor, bool singlePath);
	UObject* FindBestInventoryPath(bool predictRespawns, float& outBestWeight);
	UNavigationPoint* FindClosestNavPoint(vec3 location);
	bool MarkReachableNavEndPoints();

	// Deus Ex AI functions
	float AICanHear(UActor* other, std::optional<float> volume, std::optional<float> radius);
	float AICanSee(UActor* other, std::optional<float> visibility, std::optional<bool> bCheckVisibility, std::optional<bool> bCheckDir, std::optional<bool> bCheckCylinder, std::optional<bool> bCheckLOS);
	float AICanSmell(UActor* other, std::optional<float> smell);

	float& AccelRate() { return Value<float>(PropOffsets_Pawn.AccelRate); }
	float& AirControl() { return Value<float>(PropOffsets_Pawn.AirControl); }
	float& AirSpeed() { return Value<float>(PropOffsets_Pawn.AirSpeed); }
	NameString& AlarmTag() { return Value<NameString>(PropOffsets_Pawn.AlarmTag); }
	float& Alertness() { return Value<float>(PropOffsets_Pawn.Alertness); }
	uint8_t& AttitudeToPlayer() { return Value<uint8_t>(PropOffsets_Pawn.AttitudeToPlayer); }
	float& AvgPhysicsTime() { return Value<float>(PropOffsets_Pawn.AvgPhysicsTime); }
	float& BaseEyeHeight() { return Value<float>(PropOffsets_Pawn.BaseEyeHeight); }
	float& CombatStyle() { return Value<float>(PropOffsets_Pawn.CombatStyle); }
	float& DamageScaling() { return Value<float>(PropOffsets_Pawn.DamageScaling); }
	float& DesiredSpeed() { return Value<float>(PropOffsets_Pawn.DesiredSpeed); }
	vec3& Destination() { return Value<vec3>(PropOffsets_Pawn.Destination); }
	USound*& Die() { return Value<USound*>(PropOffsets_Pawn.Die); }
	int& DieCount() { return Value<int>(PropOffsets_Pawn.DieCount); }
	UClass*& DropWhenKilled() { return Value<UClass*>(PropOffsets_Pawn.DropWhenKilled); }
	UPawn*& Enemy() { return Value<UPawn*>(PropOffsets_Pawn.Enemy); }
	float& EyeHeight() { return Value<float>(PropOffsets_Pawn.EyeHeight); }
	UActor*& FaceTarget() { return Value<UActor*>(PropOffsets_Pawn.FaceTarget); }
	vec3& Floor() { return Value<vec3>(PropOffsets_Pawn.Floor); }
	vec3& Focus() { return Value<vec3>(PropOffsets_Pawn.Focus); }
	PointRegion& FootRegion() { return Value<PointRegion>(PropOffsets_Pawn.FootRegion); }
	float& FovAngle() { return Value<float>(PropOffsets_Pawn.FovAngle); }
	float& GroundSpeed() { return Value<float>(PropOffsets_Pawn.GroundSpeed); }
	PointRegion& HeadRegion() { return Value<PointRegion>(PropOffsets_Pawn.HeadRegion); }
	int& Health() { return Value<int>(PropOffsets_Pawn.Health); }
	float& HearingThreshold() { return Value<float>(PropOffsets_Pawn.HearingThreshold); }
	USound*& HitSound1() { return Value<USound*>(PropOffsets_Pawn.HitSound1); }
	USound*& HitSound2() { return Value<USound*>(PropOffsets_Pawn.HitSound2); }
	uint8_t& Intelligence() { return Value<uint8_t>(PropOffsets_Pawn.Intelligence); }
	int& ItemCount() { return Value<int>(PropOffsets_Pawn.ItemCount); }
	float& JumpZ() { return Value<float>(PropOffsets_Pawn.JumpZ); }
	int& KillCount() { return Value<int>(PropOffsets_Pawn.KillCount); }
	USound*& Land() { return Value<USound*>(PropOffsets_Pawn.Land); }
	float& LastPainSound() { return Value<float>(PropOffsets_Pawn.LastPainSound); }
	vec3& LastSeeingPos() { return Value<vec3>(PropOffsets_Pawn.LastSeeingPos); }
	vec3& LastSeenPos() { return Value<vec3>(PropOffsets_Pawn.LastSeenPos); }
	float& LastSeenTime() { return Value<float>(PropOffsets_Pawn.LastSeenTime); }
	float& MaxDesiredSpeed() { return Value<float>(PropOffsets_Pawn.MaxDesiredSpeed); }
	float& MaxStepHeight() { return Value<float>(PropOffsets_Pawn.MaxStepHeight); }
	float& MeleeRange() { return Value<float>(PropOffsets_Pawn.MeleeRange); }
	std::string& MenuName() { return Value<std::string>(PropOffsets_Pawn.MenuName); }
	float& MinHitWall() { return Value<float>(PropOffsets_Pawn.MinHitWall); }
	UActor*& MoveTarget() { return Value<UActor*>(PropOffsets_Pawn.MoveTarget); }
	float& MoveTimer() { return Value<float>(PropOffsets_Pawn.MoveTimer); }
	std::string& NameArticle() { return Value<std::string>(PropOffsets_Pawn.NameArticle); }
	NameString& NextLabel() { return Value<NameString>(PropOffsets_Pawn.NextLabel); }
	NameString& NextState() { return Value<NameString>(PropOffsets_Pawn.NextState); }
	float& OldMessageTime() { return Value<float>(PropOffsets_Pawn.OldMessageTime); }
	float& OrthoZoom() { return Value<float>(PropOffsets_Pawn.OrthoZoom); }
	float& PainTime() { return Value<float>(PropOffsets_Pawn.PainTime); }
	UWeapon*& PendingWeapon() { return Value<UWeapon*>(PropOffsets_Pawn.PendingWeapon); }
	float& PeripheralVision() { return Value<float>(PropOffsets_Pawn.PeripheralVision); }
	NameString& PlayerReStartState() { return Value<NameString>(PropOffsets_Pawn.PlayerReStartState); }
	UPlayerReplicationInfo*& PlayerReplicationInfo() { return Value<UPlayerReplicationInfo*>(PropOffsets_Pawn.PlayerReplicationInfo); }
	UClass*& PlayerReplicationInfoClass() { return Value<UClass*>(PropOffsets_Pawn.PlayerReplicationInfoClass); }
	float& ReducedDamagePct() { return Value<float>(PropOffsets_Pawn.ReducedDamagePct); }
	NameString& ReducedDamageType() { return Value<NameString>(PropOffsets_Pawn.ReducedDamageType); }
	FixedArrayView<UNavigationPoint*, 16> RouteCache() { return FixedArray<UNavigationPoint*, 16>(PropOffsets_Pawn.RouteCache); }
	int& SecretCount() { return Value<int>(PropOffsets_Pawn.SecretCount); }
	UInventory*& SelectedItem() { return Value<UInventory*>(PropOffsets_Pawn.SelectedItem); }
	std::string& SelectionMesh() { return Value<std::string>(PropOffsets_Pawn.SelectionMesh); }
	UDecal*& Shadow() { return Value<UDecal*>(PropOffsets_Pawn.Shadow); }
	NameString& SharedAlarmTag() { return Value<NameString>(PropOffsets_Pawn.SharedAlarmTag); }
	float& SightCounter() { return Value<float>(PropOffsets_Pawn.SightCounter); }
	float& SightRadius() { return Value<float>(PropOffsets_Pawn.SightRadius); }
	float& Skill() { return Value<float>(PropOffsets_Pawn.Skill); }
	float& SoundDampening() { return Value<float>(PropOffsets_Pawn.SoundDampening); }
	UActor*& SpecialGoal() { return Value<UActor*>(PropOffsets_Pawn.SpecialGoal); }
	std::string& SpecialMesh() { return Value<std::string>(PropOffsets_Pawn.SpecialMesh); }
	float& SpecialPause() { return Value<float>(PropOffsets_Pawn.SpecialPause); }
	float& SpeechTime() { return Value<float>(PropOffsets_Pawn.SpeechTime); }
	float& SplashTime() { return Value<float>(PropOffsets_Pawn.SplashTime); }
	int& Spree() { return Value<int>(PropOffsets_Pawn.Spree); }
	float& Stimulus() { return Value<float>(PropOffsets_Pawn.Stimulus); }
	float& UnderWaterTime() { return Value<float>(PropOffsets_Pawn.UnderWaterTime); }
	Rotator& ViewRotation() { return Value<Rotator>(PropOffsets_Pawn.ViewRotation); }
	uint8_t& Visibility() { return Value<uint8_t>(PropOffsets_Pawn.Visibility); }
	uint8_t& VoicePitch() { return Value<uint8_t>(PropOffsets_Pawn.VoicePitch); }
	std::string& VoiceType() { return Value<std::string>(PropOffsets_Pawn.VoiceType); }
	vec3& WalkBob() { return Value<vec3>(PropOffsets_Pawn.WalkBob); }
	float& WaterSpeed() { return Value<float>(PropOffsets_Pawn.WaterSpeed); }
	USound*& WaterStep() { return Value<USound*>(PropOffsets_Pawn.WaterStep); }
	UWeapon*& Weapon() { return Value<UWeapon*>(PropOffsets_Pawn.Weapon); }
	BitfieldBool bAdvancedTactics() { return BoolValue(PropOffsets_Pawn.bAdvancedTactics); }
	uint8_t& bAltFire() { return Value<uint8_t>(PropOffsets_Pawn.bAltFire); }
	BitfieldBool bAutoActivate() { return BoolValue(PropOffsets_Pawn.bAutoActivate); }
	BitfieldBool bAvoidLedges() { return BoolValue(PropOffsets_Pawn.bAvoidLedges); }
	BitfieldBool bBehindView() { return BoolValue(PropOffsets_Pawn.bBehindView); }
	BitfieldBool bCanDoSpecial() { return BoolValue(PropOffsets_Pawn.bCanDoSpecial); }
	BitfieldBool bCanFly() { return BoolValue(PropOffsets_Pawn.bCanFly); }
	BitfieldBool bCanJump() { return BoolValue(PropOffsets_Pawn.bCanJump); }
	BitfieldBool bCanOpenDoors() { return BoolValue(PropOffsets_Pawn.bCanOpenDoors); }
	BitfieldBool bCanStrafe() { return BoolValue(PropOffsets_Pawn.bCanStrafe); }
	BitfieldBool bCanSwim() { return BoolValue(PropOffsets_Pawn.bCanSwim); }
	BitfieldBool bCanWalk() { return BoolValue(PropOffsets_Pawn.bCanWalk); }
	BitfieldBool bCountJumps() { return BoolValue(PropOffsets_Pawn.bCountJumps); }
	BitfieldBool bDrowning() { return BoolValue(PropOffsets_Pawn.bDrowning); }
	uint8_t& bDuck() { return Value<uint8_t>(PropOffsets_Pawn.bDuck); }
	uint8_t& bExtra0() { return Value<uint8_t>(PropOffsets_Pawn.bExtra0); }
	uint8_t& bExtra1() { return Value<uint8_t>(PropOffsets_Pawn.bExtra1); }
	uint8_t& bExtra2() { return Value<uint8_t>(PropOffsets_Pawn.bExtra2); }
	uint8_t& bExtra3() { return Value<uint8_t>(PropOffsets_Pawn.bExtra3); }
	uint8_t& bFire() { return Value<uint8_t>(PropOffsets_Pawn.bFire); }
	BitfieldBool bFixedStart() { return BoolValue(PropOffsets_Pawn.bFixedStart); }
	uint8_t& bFreeLook() { return Value<uint8_t>(PropOffsets_Pawn.bFreeLook); }
	BitfieldBool bFromWall() { return BoolValue(PropOffsets_Pawn.bFromWall); }
	BitfieldBool bHitSlopedWall() { return BoolValue(PropOffsets_Pawn.bHitSlopedWall); }
	BitfieldBool bHunting() { return BoolValue(PropOffsets_Pawn.bHunting); }
	BitfieldBool bIsFemale() { return BoolValue(PropOffsets_Pawn.bIsFemale); }
	BitfieldBool bIsHuman() { return BoolValue(PropOffsets_Pawn.bIsHuman); }
	BitfieldBool bIsMultiSkinned() { return BoolValue(PropOffsets_Pawn.bIsMultiSkinned); }
	BitfieldBool bIsPlayer() { return BoolValue(PropOffsets_Pawn.bIsPlayer); }
	BitfieldBool bIsWalking() { return BoolValue(PropOffsets_Pawn.bIsWalking); }
	BitfieldBool bJumpOffPawn() { return BoolValue(PropOffsets_Pawn.bJumpOffPawn); }
	BitfieldBool bJustLanded() { return BoolValue(PropOffsets_Pawn.bJustLanded); }
	BitfieldBool bLOSflag() { return BoolValue(PropOffsets_Pawn.bLOSflag); }
	uint8_t& bLook() { return Value<uint8_t>(PropOffsets_Pawn.bLook); }
	BitfieldBool bNeverSwitchOnPickup() { return BoolValue(PropOffsets_Pawn.bNeverSwitchOnPickup); }
	BitfieldBool bReducedSpeed() { return BoolValue(PropOffsets_Pawn.bReducedSpeed); }
	uint8_t& bRun() { return Value<uint8_t>(PropOffsets_Pawn.bRun); }
	BitfieldBool bShootSpecial() { return BoolValue(PropOffsets_Pawn.bShootSpecial); }
	uint8_t& bSnapLevel() { return Value<uint8_t>(PropOffsets_Pawn.bSnapLevel); }
	BitfieldBool bStopAtLedges() { return BoolValue(PropOffsets_Pawn.bStopAtLedges); }
	uint8_t& bStrafe() { return Value<uint8_t>(PropOffsets_Pawn.bStrafe); }
	BitfieldBool bUpAndOut() { return BoolValue(PropOffsets_Pawn.bUpAndOut); }
	BitfieldBool bUpdatingDisplay() { return BoolValue(PropOffsets_Pawn.bUpdatingDisplay); }
	BitfieldBool bViewTarget() { return BoolValue(PropOffsets_Pawn.bViewTarget); }
	BitfieldBool bWarping() { return BoolValue(PropOffsets_Pawn.bWarping); }
	uint8_t& bZoom() { return Value<uint8_t>(PropOffsets_Pawn.bZoom); }
	UDecoration*& carriedDecoration() { return Value<UDecoration*>(PropOffsets_Pawn.carriedDecoration); }
	UNavigationPoint*& home() { return Value<UNavigationPoint*>(PropOffsets_Pawn.home); }
	UPawn*& nextPawn() { return Value<UPawn*>(PropOffsets_Pawn.nextPawn); }
	float& noise1loudness() { return Value<float>(PropOffsets_Pawn.noise1loudness); }
	UPawn*& noise1other() { return Value<UPawn*>(PropOffsets_Pawn.noise1other); }
	vec3& noise1spot() { return Value<vec3>(PropOffsets_Pawn.noise1spot); }
	float& noise1time() { return Value<float>(PropOffsets_Pawn.noise1time); }
	float& noise2loudness() { return Value<float>(PropOffsets_Pawn.noise2loudness); }
	UPawn*& noise2other() { return Value<UPawn*>(PropOffsets_Pawn.noise2other); }
	vec3& noise2spot() { return Value<vec3>(PropOffsets_Pawn.noise2spot); }
	float& noise2time() { return Value<float>(PropOffsets_Pawn.noise2time); }
	// Deus Ex exclusive properties
	BitfieldBool bCanGlide() { return BoolValue(PropOffsets_Pawn.bCanGlide); }
	int& HealthHead() { return Value<int>(PropOffsets_Pawn.HealthHead); }
	int& HealthTorso() { return Value<int>(PropOffsets_Pawn.HealthTorso); }
	int& HealthLegLeft() { return Value<int>(PropOffsets_Pawn.HealthLegLeft); }
	int& HealthLegRight() { return Value<int>(PropOffsets_Pawn.HealthLegRight); }
	int& HealthArmLeft() { return Value<int>(PropOffsets_Pawn.HealthArmLeft); }
	int& HealthArmRight() { return Value<int>(PropOffsets_Pawn.HealthArmRight); }
	BitfieldBool bIsSpeaking() { return BoolValue(PropOffsets_Pawn.bIsSpeaking); }
	BitfieldBool bWasSpeaking() { return BoolValue(PropOffsets_Pawn.bWasSpeaking); }
	std::string& lastPhoneme() { return Value<std::string>(PropOffsets_Pawn.lastPhoneme); }
	std::string& nextPhoneme() { return Value<std::string>(PropOffsets_Pawn.nextPhoneme); }
	FixedArrayView<float, 4> animTimer() { return FixedArray<float, 4>(PropOffsets_Pawn.animTimer); }
	BitfieldBool bOnFire() { return BoolValue(PropOffsets_Pawn.bOnFire); }
	float& burnTimer() { return Value<float>(PropOffsets_Pawn.burnTimer); }
	float& AIHorizontalFov() { return Value<float>(PropOffsets_Pawn.AIHorizontalFov); }
	float& AspectRatio() { return Value<float>(PropOffsets_Pawn.AspectRatio); }
	float& AngularResolution() { return Value<float>(PropOffsets_Pawn.AngularResolution); }
	float& MinAngularSize() { return Value<float>(PropOffsets_Pawn.MinAngularSize); }
	float& VisibilityThreshold() { return Value<float>(PropOffsets_Pawn.VisibilityThreshold); }
	float& SmellThreshold() { return Value<float>(PropOffsets_Pawn.SmellThreshold); }
	NameString Alliance() { return Value<NameString>(PropOffsets_Pawn.Alliance);}
	Rotator& AIAddViewRotation() { return Value<Rotator>(PropOffsets_Pawn.AIAddViewRotation); }

private:
	void ObserveMoveStallWatchdog(float elapsed);
	void RecordMoveStallCommand();
	void AdvanceMoveStallRecoveryEpisode(float elapsed,
		PawnMovement::MoveStallRecoveryEpisodeEvent event);
	void RecordMoveStallRecoveryEpisodeOutcome(float secondsSinceDetection,
		PawnMovement::MoveStallRecoveryEpisodeOutcome outcome);
	void RecordMoveStallRecoveryDecision(PawnMovement::MoveStallLatentMode latentMode,
		PawnMovement::MoveStallRecoveryDecision decision, UActor* moveTarget);
	void AdvancePainLedgeRecovery(float elapsed);
	bool ApplyPainLedgeRecovery(const vec2& requestedDirection);
	void AdvanceWallAdjustRecovery(float elapsed);
	bool ApplyWallAdjustRecovery(const vec2& requestedDirection);
	bool ApplyHarmfulZoneEscape();
	void EndHarmfulZoneEscapeLife();
	void ResetFallingHazardRecovery();
	void ResetHazardSwimEgressObservation(
		BotAI::HazardSwimEgressPlannerHandoffOutcome outcome =
			BotAI::HazardSwimEgressPlannerHandoffOutcome::EpisodeAbandoned);
	void ResolveHazardSwimEgressPlannerHandoff(
		BotAI::HazardSwimEgressPlannerHandoffOutcome outcome);
	void ObserveHazardSwimEgressPlannerHandoffMovementCommand();
	void ObserveHazardSwimEgressDirectNavigationCandidates();
	void ObserveHazardSwimEgressStaticWalkCertificate();
	void AdvanceHazardResidence(float elapsed);
	void AdvanceHazardResidenceSample(bool positiveDpsHazard, float elapsed);
	void ObserveHazardResidenceCandidate(const std::string& candidateName);
	void ObserveHazardResidenceMovementCommand();
	void ResolveHazardResidence(PawnMovement::HazardResidenceTerminal terminal);
	void ObserveExternalImpulseFallWitness(
		PawnMovement::FallingHazardForecastSource source,
		const PawnMovement::FallingHazardForecastInput& input,
		const PawnMovement::FallingHazardForecastUpdate& forecast);
	void CaptureExternalImpulseNavigationCommit();

	bool IsInPathSpecialHandling = false;
	PawnMovement::FailedNavigationMemoryState FailedNavigationMemory;
	PawnMovement::MoveStallWatchdogState MoveStallWatchdog;
	PawnMovement::MoveStallRecoveryEpisodeState MoveStallRecoveryEpisode;
	PawnMovement::MoveStallCommandKey MoveStallRecoveryEpisodeCommand;
	uint64_t MoveStallRecoveryLifeId = 1;
	PawnMovement::PainLedgeRecoveryState PainLedgeRecovery;
	PawnMovement::WallAdjustRecoveryState WallAdjustRecovery;
	struct HarmfulZoneEscapeState
	{
		bool CenterHarmful = false;
		bool FootHarmful = false;
		bool Active = false;
		bool RecoveryAttempted = false;
		vec2 IncomingDirection = vec2(0.0f);
	};
	HarmfulZoneEscapeState HarmfulZoneEscape;
	BotAI::HarmfulZoneEscapeGate HarmfulZoneEscapeGate;
	uint64_t HarmfulZoneEscapeLifeId = 1;
	uint64_t HarmfulZoneEscapeEpisodeId = 0;
	struct FallingHazardRecoveryState
	{
		bool AnchorKnown = false;
		vec3 Anchor = vec3(0.0f);
		uint64_t LifeId = 0;
		uint64_t FallEpisodeId = 0;
		bool PromotionObserved = false;
		bool AnchorRejectedObserved = false;
		bool ActionActive = false;
		float ActiveSeconds = 0.0f;
	};
	FallingHazardRecoveryState FallingHazardRecovery;
	BotAI::FallingHazardRecoveryGate FallingHazardRecoveryGate;
	struct HazardSwimEgressState
	{
		enum class AnchorSource : uint8_t
		{
			None,
			SafeSwimming,
			FallingPreMove
		};

		bool AnchorKnown = false;
		vec3 Anchor = vec3(0.0f);
		AnchorSource Source = AnchorSource::None;
		bool SwimmingSessionObserved = false;
		bool HarmfulWaterEpisodeActive = false;
		bool LiveActionAuthorized = false;
		bool LiveProbeRejected = false;
		bool LiveReplanIssued = false;
		bool ActionActive = false;
		bool PlannerHandoffWitnessPending = false;
		uint64_t PlannerHandoffMovementCommandToken = 0;
		UActor* PlannerHandoffMoveTarget = nullptr;
		vec3 PlannerHandoffDestination = vec3(0.0f);
		PawnMovement::HazardWaterEgressTransitionSource TransitionSource =
			PawnMovement::HazardWaterEgressTransitionSource::Unknown;
		std::string DirectNavBestCandidateName;
		bool DirectNavBestCandidateLocationKnown = false;
		vec3 DirectNavBestCandidateLocation = vec3(0.0f);
		float DirectNavBestCandidateDistance = std::numeric_limits<float>::infinity();
	};
	HazardSwimEgressState HazardSwimEgress;
	PawnMovement::HazardResidenceState HazardResidence;
	std::string HazardResidenceCandidateName;
	struct ExternalImpulseNavigationCommitState
	{
		bool Active = false;
		bool FallingPhaseActive = false;
		std::string MoveTargetName;
		bool MoveTargetNavigation = false;
		bool RouteHeadKnown = false;
		std::string RouteHeadName;
		vec3 Location = vec3(0.0f);
		vec3 Velocity = vec3(0.0f);
		bool LaunchForecastKnown = false;
		bool LaunchForecastHarmful = false;
	};
	ExternalImpulseNavigationCommitState ExternalImpulseNavigationCommit;
	BotAI::HazardSwimEgressGate HazardSwimEgressGate;
	std::unique_ptr<PawnMovement::HazardWaterEgressObserver>
		HazardWaterEgressObserver;
	uint64_t HazardSwimEgressLifeId = 1;
	uint64_t HazardSwimEgressEpisodeId = 0;
	PawnMovement::FallingSeamEpisodeState FallingSeamEpisode;
	PawnMovement::WalkingStepPreflightEpisodeState WalkingStepPreflightEpisode;
	bool WalkingStepExplicitJumpRequested = false;
	uint64_t PainLedgeVetoCountValue = 0;
	uint64_t PainLedgeRepeatVetoCountValue = 0;
	uint64_t PainLedgeRecoveryAttemptCountValue = 0;
	uint64_t PainLedgeRecoveryEscapeCountValue = 0;
	uint64_t WallAdjustCallCountValue = 0;
	uint64_t WallAdjustRepeatCountValue = 0;
	uint64_t WallAdjustRecoveryAttemptCountValue = 0;
	uint64_t WallAdjustRecoverySuccessCountValue = 0;
	uint64_t WallAdjustForcedReplanCountValue = 0;
	uint64_t MoveStallDetectionCountValue = 0;
	uint64_t MoveStallEpisodeResetCountValue = 0;
	uint64_t MoveStallForcedReplanCountValue = 0;
	uint64_t MoveStallNavigationForcedReplanCountValue = 0;
	uint64_t MoveStallTargetlessMoveToTimeoutCountValue = 0;
	uint64_t MoveStallDirectActorMoveTowardTimeoutCountValue = 0;
	double MoveStallEligibleSecondsValue = 0.0;
	uint64_t MoveStallRecoveryEpisodeStartCountValue = 0;
	uint64_t MoveStallRecoveryClearedWithin2SecondsCountValue = 0;
	uint64_t MoveStallRecoveryClearedAfter2SecondsWithin5SecondsCountValue = 0;
	uint64_t MoveStallRecoveryReplannedWithin5SecondsCountValue = 0;
	uint64_t MoveStallRecoveryMissed5SecondDeadlineCountValue = 0;
	uint64_t MoveStallRecoveryExcludedIntentionalStopCountValue = 0;
	uint64_t MoveStallRecoveryCensoredLifeBoundaryCountValue = 0;
	uint64_t MoveStallRecoveryCensoredRunEndCountValue = 0;
	uint64_t MoveStallRecoveryUnknownCountValue = 0;
	uint64_t MoveStallRecoveryEpisodeRecordOverflowCountValue = 0;
	uint64_t MoveStallRecoveryEpisodeRecordSequence = 0;
	uint64_t MoveStallRecoveryDecisionRecordOverflowCountValue = 0;
	uint64_t MoveStallRecoveryDecisionRecordSequence = 0;
	uint64_t MoveStallRecoveryEpisodeId = 0;
	std::vector<PawnMoveStallRecoveryEpisodeRecord> MoveStallRecoveryEpisodeRecords;
	std::vector<PawnMoveStallRecoveryDecisionRecord> MoveStallRecoveryDecisionRecords;
	uint64_t FailedNavigationAvoidanceActivationCountValue = 0;
	uint64_t FailedNavigationSafeguardSuppressionCountValue = 0;
	uint64_t FailedNavigationRoutePenaltyApplicationCountValue = 0;
	uint64_t HarmfulZoneEscapeEpisodeCountValue = 0;
	uint64_t HarmfulZoneEscapeCenterEntryCountValue = 0;
	uint64_t HarmfulZoneEscapeFootEntryCountValue = 0;
	uint64_t HarmfulZoneEscapeRecoveryAttemptCountValue = 0;
	uint64_t HarmfulZoneEscapeSuccessfulEscapeCountValue = 0;
	uint64_t HarmfulZoneEscapeForcedReplanCountValue = 0;
	uint64_t HarmfulZoneEscapeNoSafeCandidateCountValue = 0;
	uint64_t HazardSwimEgressEpisodeCountValue = 0;
	uint64_t HazardSwimEgressEligibleCountValue = 0;
	uint64_t HazardSwimEgressAuthorizedCountValue = 0;
	uint64_t HazardSwimEgressDebouncedCountValue = 0;
	uint64_t HazardSwimEgressNoAnchorRejectedCountValue = 0;
	uint64_t HazardSwimEgressExitCountValue = 0;
	uint64_t HazardSwimEgressDeathsBeforeExitCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanSameCommandReissuedCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanDifferentCommandIssuedCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanHazardClearedBeforeCommandCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanFellBeforeCommandCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanDiedBeforeCommandCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanLifeBoundaryCensoredCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanRunEndCensoredCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanEpisodeAbandonedCountValue = 0;
	uint64_t HazardSwimEgressFallingPreMoveAnchorCaptureCountValue = 0;
	uint64_t HazardSwimEgressFallingPreMoveAnchorUseCountValue = 0;
	uint64_t HazardSwimEgressLiveApplyCountValue = 0;
	uint64_t HazardSwimEgressLiveActiveTickCountValue = 0;
	uint64_t HazardSwimEgressLiveProbeRejectedCountValue = 0;
	uint64_t HazardSwimEgressLiveSuccessfulExitCountValue = 0;
	uint64_t HazardSwimEgressLiveShadowCandidateCountValue = 0;
	uint64_t HazardSwimEgressLiveShadowFallingTerminalCountValue = 0;
	uint64_t HazardSwimEgressLiveShadowHazardClearedTerminalCountValue = 0;
	uint64_t HazardSwimEgressLiveShadowProbeBlockedTerminalCountValue = 0;
	uint64_t HazardSwimEgressDirectNavProbeCountValue = 0;
	uint64_t HazardSwimEgressDirectNavSafeCandidateCountValue = 0;
	uint64_t HazardResidenceEpisodeCountValue = 0;
	uint64_t HazardResidenceClearedCountValue = 0;
	uint64_t HazardResidenceDeathCountValue = 0;
	uint64_t HazardResidenceLifeBoundaryCensoredCountValue = 0;
	uint64_t HazardResidenceRunEndCensoredCountValue = 0;
	uint64_t HazardResidenceUnknownCountValue = 0;
	uint64_t HazardResidenceReentryCountValue = 0;
	uint64_t HazardResidenceCommandChangeCountValue = 0;
	uint64_t HazardResidenceCandidateObservedCountValue = 0;
	uint64_t HazardResidenceCandidateOtherCommandCountValue = 0;
	uint64_t FallingHazardRecoveryPromotionCountValue = 0;
	uint64_t FallingHazardRecoveryAdvanceCountValue = 0;
	uint64_t FallingHazardRecoveryContextRejectedCountValue = 0;
	uint64_t FallingHazardRecoveryNoActiveFallEpisodeCountValue = 0;
	uint64_t FallingHazardRecoveryNoPrefixCountValue = 0;
	uint64_t FallingHazardRecoveryEligibleCountValue = 0;
	uint64_t FallingHazardRecoveryAnchorRejectedCountValue = 0;
	uint64_t FallingHazardRecoveryProbeRejectedCountValue = 0;
	uint64_t FallingHazardRecoveryLiveApplyCountValue = 0;
	uint64_t FallingHazardRecoveryLiveActiveTickCountValue = 0;
	uint64_t FallingHazardRecoverySafeLandingCountValue = 0;
	uint64_t FallingHazardRecoveryHarmfulEntryCountValue = 0;
	uint64_t FallingHazardRecoveryDeathCountValue = 0;
	uint64_t FallingHazardRecoveryTimeoutCountValue = 0;
	uint64_t ExternalImpulseFallHarmfulWitnessCountValue = 0;
	uint64_t ExternalImpulseFallNoAirControlCountValue = 0;
	uint64_t ExternalImpulseFallAlternativesTestedCountValue = 0;
	uint64_t ExternalImpulseFallCertifiedCountValue = 0;
	uint64_t ExternalImpulseFallUncertifiedCountValue = 0;
	uint64_t FallingSeamDetectionCountValue = 0;
	uint64_t HorizontalCornerCandidateProbeCountValue = 0;
	uint64_t HorizontalCornerAuthorizedEscapeCountValue = 0;
	uint64_t HorizontalCornerTargetProgressRejectCountValue = 0;
	uint64_t HorizontalCornerUnknownOrUnsafeSupportCountValue = 0;
	uint64_t FallingSeamEpisodeCountValue = 0;
	uint64_t FallingSeamInvalidGeometryRejectCountValue = 0;
	uint64_t FallingSeamAuthorizableEpisodeCountValue = 0;
	uint64_t HorizontalCornerAuthorizedCandidateCountValue = 0;
	uint64_t HorizontalCornerBlockedSweepCandidateCountValue = 0;
	uint64_t HorizontalCornerNoStaticWalkableSupportCandidateCountValue = 0;
	uint64_t HorizontalCornerPainSupportCandidateCountValue = 0;
	uint64_t HorizontalCornerNoActiveMovementIntentOrTargetCandidateCountValue = 0;
	uint64_t HorizontalCornerTrueTargetRegressionCandidateCountValue = 0;
	uint64_t HorizontalCornerUnknownEvidenceCandidateCountValue = 0;
	uint64_t WalkingStepPreflightObservationCountValue = 0;
	uint64_t WalkingStepPreflightUnsupportedEndpointCountValue = 0;
	uint64_t WalkingStepPreflightNoDecisionCountValue = 0;
	uint64_t WalkingStepPreflightProvisionalAuthorizationCountValue = 0;
	uint64_t WalkingStepPreflightAuthorizationCountValue = 0;
	uint64_t WalkingStepPreflightAuthorizableEpisodeCountValue = 0;
	uint64_t WalkingStepPreflightDiagnosticOverflowCountValue = 0;
	uint64_t WalkingHitWallDispatchObservationCountValue = 0;
	uint64_t WalkingHitWallDispatchLegacyZBandCountValue = 0;
	uint64_t WalkingHitWallDispatchMinHitWallCountValue = 0;
	uint64_t WalkingHitWallDispatchDisagreementCountValue = 0;
	uint64_t WalkingHitWallDispatchCallbackCountValue = 0;
	uint64_t WalkingHitWallDispatchDiagnosticOverflowCountValue = 0;
	uint64_t WalkingHitWallDispatchDiagnosticSequence = 0;
	uint32_t WalkingHitWallFixtureContactLimit = 0;
	uint32_t WalkingHitWallFixtureContactCount = 0;
	std::vector<PawnMovement::WalkingHitWallDispatchDiagnosticRecord>
		WalkingHitWallDispatchDiagnostics;
	uint64_t WalkingStepPreflightPositiveDpsVetoEligibleCountValue = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoAppliedCountValue = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoDebouncedCountValue = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoForcedReplanCountValue = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoRollbackRejectedCountValue = 0;
	uint64_t WalkingStepPreflightDiagnosticSequence = 0;
	std::vector<PawnMovement::WalkingStepPreflightDiagnosticRecord>
		WalkingStepPreflightDiagnostics;
	uint64_t WalkingStepPreflightPositiveDpsVetoActionOverflowCountValue = 0;
	uint64_t InventoryDirectReachSupportObservationCountValue = 0;
	uint64_t InventoryDirectReachSupportSafeSupportedCountValue = 0;
	uint64_t InventoryDirectReachSupportSafeUnsupportedNoObservedHazardCountValue = 0;
	uint64_t InventoryDirectReachSupportUnsafeHarmfulFootZoneCountValue = 0;
	uint64_t InventoryDirectReachSupportUnsafeUnsupportedOverHarmfulCountValue = 0;
	uint64_t InventoryDirectReachSupportUnavailableCountValue = 0;
	uint64_t InventoryDirectReachSupportDiagnosticOverflowCountValue = 0;
	uint64_t InventoryDirectReachSupportDiagnosticSequence = 0;
	std::vector<PawnMovement::InventoryDirectReachSupportDiagnosticRecord>
		InventoryDirectReachSupportDiagnostics;
	uint64_t RoutePathCommitSequence = 0;
	uint64_t RoutePathCommitOverflowCountValue = 0;
	std::vector<PawnMovement::RoutePathCommitRecord> RoutePathCommitRecords;
	uint64_t WalkingStepPreflightPositiveDpsVetoActionSequence = 0;
	std::vector<PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord>
		WalkingStepPreflightPositiveDpsVetoActions;
	bool WalkingStepPreflightPendingConfirmation = false;
	int WalkingStepPreflightPendingIteration = 0;
	uint64_t WalkingStepPreflightPendingInvocation = 0;
	uint64_t WalkingStepPreflightPendingLifeGeneration = 0;
	PawnMovement::WalkingStepPreflightDiagnosticRecord
		WalkingStepPreflightPendingDiagnostic;
	const void* WalkingStepPreflightPendingSemanticTarget = nullptr;
	PawnMovement::WalkingStepPreflightInput WalkingStepPreflightPendingInput;
	uint64_t WalkingStepPreflightInvocationSequence = 0;
	uint64_t WalkingStepPreflightLifeGeneration = 1;
	std::array<uint64_t, PawnMovement::WalkingStepPreflightReasonCount>
		WalkingStepPreflightReasonCountValues = {};
	bool WalkingStepPreflightObservedTransactionValid = false;
	int WalkingStepPreflightObservedIteration = 0;
	uint64_t WalkingStepPreflightObservedInvocation = 0;
	PawnMovement::FallingParityRealizedCorrelation
		WalkingStepPreflightObservedCorrelation;
	PawnMovement::FallingParityRealizedTraceState FallingParityRealizedTrace;
	std::vector<PawnMovement::FallingParityRealizedRecord>
		FallingParityRealizedRecords;
	uint64_t FallingParityRealizedEpisodeCountValue = 0;
	uint64_t FallingParityRealizedStepCountValue = 0;
	uint64_t FallingParityRealizedMatchedStepCountValue = 0;
	uint64_t FallingParityRealizedMatchedLandingStepCountValue = 0;
	uint64_t FallingParityRealizedMismatchCountValue = 0;
	uint64_t FallingParityRealizedUnknownCountValue = 0;
	uint64_t FallingParityRealizedCallbackBarrierCountValue = 0;
	uint64_t FallingParityRealizedPainEntryCountValue = 0;
	uint64_t FallingParityRealizedDeathCountValue = 0;
	uint64_t FallingParityRealizedLandingCountValue = 0;
	uint64_t FallingParityRealizedContinuityLossCountValue = 0;
	uint64_t FallingParityRealizedRecordOverflowCountValue = 0;
	struct FallingHazardPendingSweep
	{
		bool Active = false;
		bool GeometryLatched = false;
		bool ReentrantMoveObserved = false;
		bool HitWallCallbackExpected = false;
		uint32_t MoveDepth = 0;
		PawnMovement::FallingHazardSweepLeg Leg =
			PawnMovement::FallingHazardSweepLeg::Direct;
		vec3 Origin = vec3(0.0f);
		vec3 RequestedDelta = vec3(0.0f);
		float ElapsedContribution = 0.0f;
		PawnMovement::FallingHazardCollisionKind Collision =
			PawnMovement::FallingHazardCollisionKind::Unknown;
		float HitFraction = 1.0f;
		vec3 HitNormal = vec3(0.0f);
		uint32_t MoveCallbackMask = 0;
	};
	void CommitPendingFallingHazardSweep(const PointRegion& center,
		const PointRegion& foot, const PointRegion& head,
		uint32_t zoneCallbackMask, bool callbackAlreadyDispatched);
	void RecordFallingHazardMovementCommand();
	struct FallingHazardAlignedCommandWitness
	{
		bool Active = false;
		bool StaticWorldCollision = false;
		uint64_t CommandToken = 0;
		uint8_t LatentState = 0;
		UActor* MoveTarget = nullptr;
		vec3 Destination = vec3(0.0f);
		vec3 Acceleration = vec3(0.0f);
	};
	uint64_t FallingHazardMovementCommandToken = 0;
	FallingHazardAlignedCommandWitness PendingFallingHazardAlignedCommandWitness;
	std::unique_ptr<PawnMovement::FallingHazardRuntimeObserver>
		FallingHazardObserver;
	FallingHazardPendingSweep FallingHazardPending;
	uint32_t FallingHazardTryMoveDepth = 0;
	PawnMovement::FallingHazardForecastSource FallingHazardQueuedSource =
		PawnMovement::FallingHazardForecastSource::Unknown;
	std::optional<PawnMovement::FallingHazardForecastContinuationSeed>
		FallingHazardCallbackContinuation;
};

class UScout : public UPawn
{
public:
	using UPawn::UPawn;
};

class UPlayerPawn : public UPawn
{
public:
	using UPawn::UPawn;

	void PausedInput(float elapsed);

	void Tick(float elapsed) override;
	void TickRotating(float elapsed) override;

	// Unreal 227 addition
	bool IsPressing(uint8_t KeyNum);

	float& AppliedBob() { return Value<float>(PropOffsets_PlayerPawn.AppliedBob); }
	float& Bob() { return Value<float>(PropOffsets_PlayerPawn.Bob); }
	float& BorrowedMouseX() { return Value<float>(PropOffsets_PlayerPawn.BorrowedMouseX); }
	float& BorrowedMouseY() { return Value<float>(PropOffsets_PlayerPawn.BorrowedMouseY); }
	UClass*& CarcassType() { return Value<UClass*>(PropOffsets_PlayerPawn.CarcassType); }
	uint8_t& CdTrack() { return Value<uint8_t>(PropOffsets_PlayerPawn.CdTrack); }
	float& ClientUpdateTime() { return Value<float>(PropOffsets_PlayerPawn.ClientUpdateTime); }
	vec3& ConstantGlowFog() { return Value<vec3>(PropOffsets_PlayerPawn.ConstantGlowFog); }
	float& ConstantGlowScale() { return Value<float>(PropOffsets_PlayerPawn.ConstantGlowScale); }
	float& CurrentTimeStamp() { return Value<float>(PropOffsets_PlayerPawn.CurrentTimeStamp); }
	float& DefaultFOV() { return Value<float>(PropOffsets_PlayerPawn.DefaultFOV); }
	std::string& DelayedCommand() { return Value<std::string>(PropOffsets_PlayerPawn.DelayedCommand); }
	int& DemoViewPitch() { return Value<int>(PropOffsets_PlayerPawn.DemoViewPitch); }
	int& DemoViewYaw() { return Value<int>(PropOffsets_PlayerPawn.DemoViewYaw); }
	float& DesiredFOV() { return Value<float>(PropOffsets_PlayerPawn.DesiredFOV); }
	vec3& DesiredFlashFog() { return Value<vec3>(PropOffsets_PlayerPawn.DesiredFlashFog); }
	float& DesiredFlashScale() { return Value<float>(PropOffsets_PlayerPawn.DesiredFlashScale); }
	float& DodgeClickTime() { return Value<float>(PropOffsets_PlayerPawn.DodgeClickTime); }
	float& DodgeClickTimer() { return Value<float>(PropOffsets_PlayerPawn.DodgeClickTimer); }
	std::string& FailedView() { return Value<std::string>(PropOffsets_PlayerPawn.FailedView); }
	vec3& FlashFog() { return Value<vec3>(PropOffsets_PlayerPawn.FlashFog); }
	vec3& FlashScale() { return Value<vec3>(PropOffsets_PlayerPawn.FlashScale); }
	USavedMove*& FreeMoves() { return Value<USavedMove*>(PropOffsets_PlayerPawn.FreeMoves); }
	UGameReplicationInfo*& GameReplicationInfo() { return Value<UGameReplicationInfo*>(PropOffsets_PlayerPawn.GameReplicationInfo); }
	UClass*& HUDType() { return Value<UClass*>(PropOffsets_PlayerPawn.HUDType); }
	float& Handedness() { return Value<float>(PropOffsets_PlayerPawn.Handedness); }
	float& InstantFlash() { return Value<float>(PropOffsets_PlayerPawn.InstantFlash); }
	vec3& InstantFog() { return Value<vec3>(PropOffsets_PlayerPawn.InstantFog); }
	USound*& JumpSound() { return Value<USound*>(PropOffsets_PlayerPawn.JumpSound); }
	float& LandBob() { return Value<float>(PropOffsets_PlayerPawn.LandBob); }
	float& LastMessageWindow() { return Value<float>(PropOffsets_PlayerPawn.LastMessageWindow); }
	float& LastPlaySound() { return Value<float>(PropOffsets_PlayerPawn.LastPlaySound); }
	float& LastUpdateTime() { return Value<float>(PropOffsets_PlayerPawn.LastUpdateTime); }
	float& MaxTimeMargin() { return Value<float>(PropOffsets_PlayerPawn.MaxTimeMargin); }
	int& Misc1() { return Value<int>(PropOffsets_PlayerPawn.Misc1); }
	int& Misc2() { return Value<int>(PropOffsets_PlayerPawn.Misc2); }
	float& MouseSensitivity() { return Value<float>(PropOffsets_PlayerPawn.MouseSensitivity); }
	float& MouseSmoothThreshold() { return Value<float>(PropOffsets_PlayerPawn.MouseSmoothThreshold); }
	float& MouseZeroTime() { return Value<float>(PropOffsets_PlayerPawn.MouseZeroTime); }
	float& MyAutoAim() { return Value<float>(PropOffsets_PlayerPawn.MyAutoAim); }
	std::string& NoPauseMessage() { return Value<std::string>(PropOffsets_PlayerPawn.NoPauseMessage); }
	std::string& OwnCamera() { return Value<std::string>(PropOffsets_PlayerPawn.OwnCamera); }
	std::string& Password() { return Value<std::string>(PropOffsets_PlayerPawn.Password); }
	USavedMove*& PendingMove() { return Value<USavedMove*>(PropOffsets_PlayerPawn.PendingMove); }
	UPlayer*& Player() { return Value<UPlayer*>(PropOffsets_PlayerPawn.Player); }
	FixedArrayView<Color*, 5> ProgressColor() { return FixedArray<Color*, 5>(PropOffsets_PlayerPawn.ProgressColor); }
	FixedArrayView<std::optional<std::string>, 5> ProgressMessage() { return FixedArray<std::optional<std::string>, 5>(PropOffsets_PlayerPawn.ProgressMessage); }
	float& ProgressTimeOut() { return Value<float>(PropOffsets_PlayerPawn.ProgressTimeOut); }
	std::string& QuickSaveString() { return Value<std::string>(PropOffsets_PlayerPawn.QuickSaveString); }
	BitfieldBool ReceivedSecretChecksum() { return BoolValue(PropOffsets_PlayerPawn.ReceivedSecretChecksum); }
	int& RendMap() { return Value<int>(PropOffsets_PlayerPawn.RendMap); }
	USavedMove*& SavedMoves() { return Value<USavedMove*>(PropOffsets_PlayerPawn.SavedMoves); }
	// UScoreBoard*& Scoring() { return Value<UScoreBoard*>(PropOffsets_PlayerPawn.Scoring); }
	UClass*& ScoringType() { return Value<UClass*>(PropOffsets_PlayerPawn.ScoringType); }
	float& ServerTimeStamp() { return Value<float>(PropOffsets_PlayerPawn.ServerTimeStamp); }
	int& ShowFlags() { return Value<int>(PropOffsets_PlayerPawn.ShowFlags); }
	float& SmoothMouseX() { return Value<float>(PropOffsets_PlayerPawn.SmoothMouseX); }
	float& SmoothMouseY() { return Value<float>(PropOffsets_PlayerPawn.SmoothMouseY); }
	UMusic*& Song() { return Value<UMusic*>(PropOffsets_PlayerPawn.Song); }
	uint8_t& SongSection() { return Value<uint8_t>(PropOffsets_PlayerPawn.SongSection); }
	UClass*& SpecialMenu() { return Value<UClass*>(PropOffsets_PlayerPawn.SpecialMenu); }
	float& TargetEyeHeight() { return Value<float>(PropOffsets_PlayerPawn.TargetEyeHeight); }
	Rotator& TargetViewRotation() { return Value<Rotator>(PropOffsets_PlayerPawn.TargetViewRotation); }
	vec3& TargetWeaponViewOffset() { return Value<vec3>(PropOffsets_PlayerPawn.TargetWeaponViewOffset); }
	float& TimeMargin() { return Value<float>(PropOffsets_PlayerPawn.TimeMargin); }
	uint8_t& Transition() { return Value<uint8_t>(PropOffsets_PlayerPawn.Transition); }
	UActor*& ViewTarget() { return Value<UActor*>(PropOffsets_PlayerPawn.ViewTarget); }
	std::string& ViewingFrom() { return Value<std::string>(PropOffsets_PlayerPawn.ViewingFrom); }
	FixedArrayView<NameString*, 20> WeaponPriority() { return FixedArray<NameString*, 20>(PropOffsets_PlayerPawn.WeaponPriority); }
	float& ZoomLevel() { return Value<float>(PropOffsets_PlayerPawn.ZoomLevel); }
	float& aBaseX() { return Value<float>(PropOffsets_PlayerPawn.aBaseX); }
	float& aBaseY() { return Value<float>(PropOffsets_PlayerPawn.aBaseY); }
	float& aBaseZ() { return Value<float>(PropOffsets_PlayerPawn.aBaseZ); }
	float& aExtra0() { return Value<float>(PropOffsets_PlayerPawn.aExtra0); }
	float& aExtra1() { return Value<float>(PropOffsets_PlayerPawn.aExtra1); }
	float& aExtra2() { return Value<float>(PropOffsets_PlayerPawn.aExtra2); }
	float& aExtra3() { return Value<float>(PropOffsets_PlayerPawn.aExtra3); }
	float& aExtra4() { return Value<float>(PropOffsets_PlayerPawn.aExtra4); }
	float& aForward() { return Value<float>(PropOffsets_PlayerPawn.aForward); }
	float& aLookUp() { return Value<float>(PropOffsets_PlayerPawn.aLookUp); }
	float& aMouseX() { return Value<float>(PropOffsets_PlayerPawn.aMouseX); }
	float& aMouseY() { return Value<float>(PropOffsets_PlayerPawn.aMouseY); }
	float& aStrafe() { return Value<float>(PropOffsets_PlayerPawn.aStrafe); }
	float& aTurn() { return Value<float>(PropOffsets_PlayerPawn.aTurn); }
	float& aUp() { return Value<float>(PropOffsets_PlayerPawn.aUp); }
	BitfieldBool bAdmin() { return BoolValue(PropOffsets_PlayerPawn.bAdmin); }
	BitfieldBool bAlwaysMouseLook() { return BoolValue(PropOffsets_PlayerPawn.bAlwaysMouseLook); }
	BitfieldBool bAnimTransition() { return BoolValue(PropOffsets_PlayerPawn.bAnimTransition); }
	BitfieldBool bBadConnectionAlert() { return BoolValue(PropOffsets_PlayerPawn.bBadConnectionAlert); }
	BitfieldBool bCenterView() { return BoolValue(PropOffsets_PlayerPawn.bCenterView); }
	BitfieldBool bCheatsEnabled() { return BoolValue(PropOffsets_PlayerPawn.bCheatsEnabled); }
	BitfieldBool bDelayedCommand() { return BoolValue(PropOffsets_PlayerPawn.bDelayedCommand); }
	BitfieldBool bEdgeBack() { return BoolValue(PropOffsets_PlayerPawn.bEdgeBack); }
	BitfieldBool bEdgeForward() { return BoolValue(PropOffsets_PlayerPawn.bEdgeForward); }
	BitfieldBool bEdgeLeft() { return BoolValue(PropOffsets_PlayerPawn.bEdgeLeft); }
	BitfieldBool bEdgeRight() { return BoolValue(PropOffsets_PlayerPawn.bEdgeRight); }
	BitfieldBool bFixedCamera() { return BoolValue(PropOffsets_PlayerPawn.bFixedCamera); }
	BitfieldBool bFrozen() { return BoolValue(PropOffsets_PlayerPawn.bFrozen); }
	BitfieldBool bInvertMouse() { return BoolValue(PropOffsets_PlayerPawn.bInvertMouse); }
	BitfieldBool bIsCrouching() { return BoolValue(PropOffsets_PlayerPawn.bIsCrouching); }
	BitfieldBool bIsTurning() { return BoolValue(PropOffsets_PlayerPawn.bIsTurning); }
	BitfieldBool bIsTyping() { return BoolValue(PropOffsets_PlayerPawn.bIsTyping); }
	BitfieldBool bJumpStatus() { return BoolValue(PropOffsets_PlayerPawn.bJumpStatus); }
	BitfieldBool bJustAltFired() { return BoolValue(PropOffsets_PlayerPawn.bJustAltFired); }
	BitfieldBool bJustFired() { return BoolValue(PropOffsets_PlayerPawn.bJustFired); }
	BitfieldBool bKeyboardLook() { return BoolValue(PropOffsets_PlayerPawn.bKeyboardLook); }
	BitfieldBool bLookUpStairs() { return BoolValue(PropOffsets_PlayerPawn.bLookUpStairs); }
	BitfieldBool bMaxMouseSmoothing() { return BoolValue(PropOffsets_PlayerPawn.bMaxMouseSmoothing); }
	BitfieldBool bMessageBeep() { return BoolValue(PropOffsets_PlayerPawn.bMessageBeep); }
	BitfieldBool bMouseZeroed() { return BoolValue(PropOffsets_PlayerPawn.bMouseZeroed); }
	BitfieldBool bNeverAutoSwitch() { return BoolValue(PropOffsets_PlayerPawn.bNeverAutoSwitch); }
	BitfieldBool bNoFlash() { return BoolValue(PropOffsets_PlayerPawn.bNoFlash); }
	BitfieldBool bNoVoices() { return BoolValue(PropOffsets_PlayerPawn.bNoVoices); }
	BitfieldBool bPressedJump() { return BoolValue(PropOffsets_PlayerPawn.bPressedJump); }
	BitfieldBool bReadyToPlay() { return BoolValue(PropOffsets_PlayerPawn.bReadyToPlay); }
	BitfieldBool bReducedVis() { return BoolValue(PropOffsets_PlayerPawn.bReducedVis); }
	BitfieldBool bRising() { return BoolValue(PropOffsets_PlayerPawn.bRising); }
	BitfieldBool bShakeDir() { return BoolValue(PropOffsets_PlayerPawn.bShakeDir); }
	BitfieldBool bShowMenu() { return BoolValue(PropOffsets_PlayerPawn.bShowMenu); }
	BitfieldBool bShowScores() { return BoolValue(PropOffsets_PlayerPawn.bShowScores); }
	BitfieldBool bSinglePlayer() { return BoolValue(PropOffsets_PlayerPawn.bSinglePlayer); }
	BitfieldBool bSnapToLevel() { return BoolValue(PropOffsets_PlayerPawn.bSnapToLevel); }
	BitfieldBool bSpecialMenu() { return BoolValue(PropOffsets_PlayerPawn.bSpecialMenu); }
	BitfieldBool bUpdatePosition() { return BoolValue(PropOffsets_PlayerPawn.bUpdatePosition); }
	BitfieldBool bUpdating() { return BoolValue(PropOffsets_PlayerPawn.bUpdating); }
	BitfieldBool bWasBack() { return BoolValue(PropOffsets_PlayerPawn.bWasBack); }
	BitfieldBool bWasForward() { return BoolValue(PropOffsets_PlayerPawn.bWasForward); }
	BitfieldBool bWasLeft() { return BoolValue(PropOffsets_PlayerPawn.bWasLeft); }
	BitfieldBool bWasRight() { return BoolValue(PropOffsets_PlayerPawn.bWasRight); }
	BitfieldBool bWokeUp() { return BoolValue(PropOffsets_PlayerPawn.bWokeUp); }
	BitfieldBool bZooming() { return BoolValue(PropOffsets_PlayerPawn.bZooming); }
	float& bobtime() { return Value<float>(PropOffsets_PlayerPawn.bobtime); }
	float& maxshake() { return Value<float>(PropOffsets_PlayerPawn.maxshake); }
	UHUD*& myHUD() { return Value<UHUD*>(PropOffsets_PlayerPawn.myHUD); }
	BitfieldBool ngSecretSet() { return BoolValue(PropOffsets_PlayerPawn.ngSecretSet); }
	std::string& ngWorldSecret() { return Value<std::string>(PropOffsets_PlayerPawn.ngWorldSecret); }
	int& shakemag() { return Value<int>(PropOffsets_PlayerPawn.shakemag); }
	float& shaketimer() { return Value<float>(PropOffsets_PlayerPawn.shaketimer); }
	float& shakevert() { return Value<float>(PropOffsets_PlayerPawn.shakevert); }
	float& verttimer() { return Value<float>(PropOffsets_PlayerPawn.verttimer); }

	void LoadProperties(); // Always loaded from User.ini
	void SaveConfig() override;
};

struct ActorRef
{
	UActor* Actor = nullptr;
	int RefCount = 0;
};

class UPlayerPawnExt : public UPlayerPawn
{
public:
	using UPlayerPawn::UPlayerPawn;

	void InitRootWindow();
	void PreRenderWindows(UCanvas* canvas);
	void PostRenderWindows(UCanvas* canvas);

	UFlagBase*& FlagBase() { return Value<UFlagBase*>(PropOffsets_PlayerPawnExt.FlagBase); }
	URootWindow*& RootWindow() { return Value<URootWindow*>(PropOffsets_PlayerPawnExt.RootWindow); }
	int& actorCount() { return Value<int>(PropOffsets_PlayerPawnExt.actorCount); }
	FixedArrayView<ActorRef, 32> actorList() { return FixedArray<ActorRef, 32>(PropOffsets_PlayerPawnExt.actorList); }
};

class UCamera : public UPlayerPawn
{
public:
	using UPlayerPawn::UPlayerPawn;
};

class UPakPathNodeIterator : public UActor
{
public:
	using UActor::UActor;

	void BuildPath(vec3& start, vec3& end);
	void CheckUPak();
	UNavigationPoint* GetFirst();
	UNavigationPoint* GetPrevious();
	UNavigationPoint* GetCurrent();
	UNavigationPoint* GetNext();
	UNavigationPoint* GetLast();
	UNavigationPoint* GetLastVisible();

	TypedScriptArray<UNavigationPoint*> NodePath() { return DynamicArray<UNavigationPoint*>(PropOffsets_UPakPathNodeIterator.NodePath); }
	int& NodeCount() { return Value<int>(PropOffsets_UPakPathNodeIterator.NodeCount); }
	int& NodeIndex() { return Value<int>(PropOffsets_UPakPathNodeIterator.NodeIndex); }
	int& NodeCost() { return Value<int>(PropOffsets_UPakPathNodeIterator.NodeCost); }
	vec3& NodeStart() { return Value<vec3>(PropOffsets_UPakPathNodeIterator.NodeStart); }
};

class UPakPawnPathNodeIterator : public UPakPathNodeIterator
{
public:
	using UPakPathNodeIterator::UPakPathNodeIterator;

	void SetPawn(UPawn* P);

	UPawn*& Pawn() { return Value<UPawn*>(PropOffsets_UPakPawnPathNodeIterator.Pawn); }
};


struct BlendAnimChannel  
{  
	MeshAnimSeq* Sequence = nullptr;  
	float AnimRate = 0.0f;  
	float AnimProgressLimit = 0.0f;  
	float BlendAlpha = 0.0f;  
	float BlendRate = 0.0f;  
	float TweenSpeed = 0.0f;  
	float FrameStep = 0.0f;  
	float PreviousRate = 0.0f;  
	  
	int InternalRate = 0;  
	int InternalAnimRate = 0;  
	int InternalTween = 0;  
	int InternalProgressLimit = 0;  
};  

class UDeusExPlayer : public UPlayerPawnExt
{
public:
	using UPlayerPawnExt::UPlayerPawnExt;

	void ConBindEvents();
	UObject* CreateDataVaultImageNoteObject();
	UObject* CreateDumpLocationObject();
	UObject* CreateGameDirectoryObject();
	UObject* CreateHistoryEvent();
	UObject* CreateHistoryObject();
	UObject* CreateLogObject();
	void DeleteSaveGameFiles(std::optional<std::string> saveDirectory);
	std::string GetDeusExVersion();
	void SaveGame(int saveIndex, std::optional<std::string> saveDesc);
	NameString SetBoolFlagFromString(const std::string& flagNameString, bool bValue);
	void UnloadTexture(UObject* Texture);

	//UComputers*& ActiveComputer() { return Value<UComputers*>(PropOffsets_DeusExPlayer.ActiveComputer); }
	std::string& AddedNanoKey() { return Value<std::string>(PropOffsets_DeusExPlayer.AddedNanoKey); }
	NameString& AugPrefs() { return Value<NameString>(PropOffsets_DeusExPlayer.AugPrefs); }
	//UAugmentationManager*& AugmentationSystem() { return Value<UAugmentationManager*>(PropOffsets_DeusExPlayer.AugmentationSystem); }
	//UBarkManager*& BarkManager() { return Value<UBarkManager*>(PropOffsets_DeusExPlayer.BarkManager); }
	float& BleedRate() { return Value<float>(PropOffsets_DeusExPlayer.BleedRate); }
	std::string& BurnString() { return Value<std::string>(PropOffsets_DeusExPlayer.BurnString); }
	std::string& CanCarryOnlyOne() { return Value<std::string>(PropOffsets_DeusExPlayer.CanCarryOnlyOne); }
	std::string& CannotDropHere() { return Value<std::string>(PropOffsets_DeusExPlayer.CannotDropHere); }
	std::string& CannotLift() { return Value<std::string>(PropOffsets_DeusExPlayer.CannotLift); }
	UInventory*& ClientinHandPending() { return Value<UInventory*>(PropOffsets_DeusExPlayer.ClientinHandPending); }
	float& ClotPeriod() { return Value<float>(PropOffsets_DeusExPlayer.ClotPeriod); }
	float& CombatDifficulty() { return Value<float>(PropOffsets_DeusExPlayer.CombatDifficulty); }
	//UConHistory*& ConHistory() { return Value<UConHistory*>(PropOffsets_DeusExPlayer.ConHistory); }
	//UConPlay*& ConPlay() { return Value<UConPlay*>(PropOffsets_DeusExPlayer.ConPlay); }
	UActor*& ConversationActor() { return Value<UActor*>(PropOffsets_DeusExPlayer.ConversationActor); }
	int& Credits() { return Value<int>(PropOffsets_DeusExPlayer.Credits); }
	UGameInfo*& DXGame() { return Value<UGameInfo*>(PropOffsets_DeusExPlayer.DXGame); }
	//Ushieldeffect*& DamageShield() { return Value<Ushieldeffect*>(PropOffsets_DeusExPlayer.DamageShield); }
	//UDataLinkPlay*& DataLinkPlay() { return Value<UDataLinkPlay*>(PropOffsets_DeusExPlayer.DataLinkPlay); }
	float& DropCounter() { return Value<float>(PropOffsets_DeusExPlayer.DropCounter); }
	float& Energy() { return Value<float>(PropOffsets_DeusExPlayer.Energy); }
	std::string& EnergyDepleted() { return Value<std::string>(PropOffsets_DeusExPlayer.EnergyDepleted); }
	float& EnergyDrain() { return Value<float>(PropOffsets_DeusExPlayer.EnergyDrain); }
	float& EnergyDrainTotal() { return Value<float>(PropOffsets_DeusExPlayer.EnergyDrainTotal); }
	float& EnergyMax() { return Value<float>(PropOffsets_DeusExPlayer.EnergyMax); }
	//UDeusExGoal*& FirstGoal() { return Value<UDeusExGoal*>(PropOffsets_DeusExPlayer.FirstGoal); }
	//UDataVaultImage*& FirstImage() { return Value<UDataVaultImage*>(PropOffsets_DeusExPlayer.FirstImage); }
	//UDeusExLog*& FirstLog() { return Value<UDeusExLog*>(PropOffsets_DeusExPlayer.FirstLog); }
	//UDeusExNote*& FirstNote() { return Value<UDeusExNote*>(PropOffsets_DeusExPlayer.FirstNote); }
	float& FlashTimer() { return Value<float>(PropOffsets_DeusExPlayer.FlashTimer); }
	NameString& FloorMaterial() { return Value<NameString>(PropOffsets_DeusExPlayer.FloorMaterial); }
	UActor*& FrobTarget() { return Value<UActor*>(PropOffsets_DeusExPlayer.FrobTarget); }
	float& FrobTime() { return Value<float>(PropOffsets_DeusExPlayer.FrobTime); }
	//UDebugInfo*& GlobalDebugObj() { return Value<UDebugInfo*>(PropOffsets_DeusExPlayer.GlobalDebugObj); }
	std::string& GoalAdded() { return Value<std::string>(PropOffsets_DeusExPlayer.GoalAdded); }
	std::string& HUDThemeName() { return Value<std::string>(PropOffsets_DeusExPlayer.HUDThemeName); }
	std::string& HandsFull() { return Value<std::string>(PropOffsets_DeusExPlayer.HandsFull); }
	std::string& HeadString() { return Value<std::string>(PropOffsets_DeusExPlayer.HeadString); }
	std::string& HealedPointLabel() { return Value<std::string>(PropOffsets_DeusExPlayer.HealedPointLabel); }
	std::string& HealedPointsLabel() { return Value<std::string>(PropOffsets_DeusExPlayer.HealedPointsLabel); }
	std::string& InventoryFull() { return Value<std::string>(PropOffsets_DeusExPlayer.InventoryFull); }
	float& JoltMagnitude() { return Value<float>(PropOffsets_DeusExPlayer.JoltMagnitude); }
	//UNanoKeyInfo*& KeyList() { return Value<UNanoKeyInfo*>(PropOffsets_DeusExPlayer.KeyList); }
	//UNanoKeyRing*& KeyRing() { return Value<UNanoKeyRing*>(PropOffsets_DeusExPlayer.KeyRing); }
	//UDeusExGoal*& LastGoal() { return Value<UDeusExGoal*>(PropOffsets_DeusExPlayer.LastGoal); }
	//UDeusExLog*& LastLog() { return Value<UDeusExLog*>(PropOffsets_DeusExPlayer.LastLog); }
	//UDeusExNote*& LastNote() { return Value<UDeusExNote*>(PropOffsets_DeusExPlayer.LastNote); }
	float& LastRefreshTime() { return Value<float>(PropOffsets_DeusExPlayer.LastRefreshTime); }
	UInventory*& LastinHand() { return Value<UInventory*>(PropOffsets_DeusExPlayer.LastinHand); }
	std::string& LegsString() { return Value<std::string>(PropOffsets_DeusExPlayer.LegsString); }
	float& MPDamageMult() { return Value<float>(PropOffsets_DeusExPlayer.MPDamageMult); }
	float& MaxFrobDistance() { return Value<float>(PropOffsets_DeusExPlayer.MaxFrobDistance); }
	float& MaxRegenPoint() { return Value<float>(PropOffsets_DeusExPlayer.MaxRegenPoint); }
	std::string& MenuThemeName() { return Value<std::string>(PropOffsets_DeusExPlayer.MenuThemeName); }
	std::string& NextMap() { return Value<std::string>(PropOffsets_DeusExPlayer.NextMap); }
	float& NintendoImmunityTime() { return Value<float>(PropOffsets_DeusExPlayer.NintendoImmunityTime); }
	float& NintendoImmunityTimeLeft() { return Value<float>(PropOffsets_DeusExPlayer.NintendoImmunityTimeLeft); }
	std::string& NoRoomToLift() { return Value<std::string>(PropOffsets_DeusExPlayer.NoRoomToLift); }
	std::string& NoneString() { return Value<std::string>(PropOffsets_DeusExPlayer.NoneString); }
	std::string& NoteAdded() { return Value<std::string>(PropOffsets_DeusExPlayer.NoteAdded); }
	int& PlayerSkin() { return Value<int>(PropOffsets_DeusExPlayer.PlayerSkin); }
	std::string& PoisonString() { return Value<std::string>(PropOffsets_DeusExPlayer.PoisonString); }
	std::string& PrimaryGoalCompleted() { return Value<std::string>(PropOffsets_DeusExPlayer.PrimaryGoalCompleted); }
	std::string& QuickSaveGameTitle() { return Value<std::string>(PropOffsets_DeusExPlayer.QuickSaveGameTitle); }
	float& RegenRate() { return Value<float>(PropOffsets_DeusExPlayer.RegenRate); }
	float& RunSilentValue() { return Value<float>(PropOffsets_DeusExPlayer.RunSilentValue); }
	std::string& SecondaryGoalCompleted() { return Value<std::string>(PropOffsets_DeusExPlayer.SecondaryGoalCompleted); }
	float& ServerTimeDiff() { return Value<float>(PropOffsets_DeusExPlayer.ServerTimeDiff); }
	float& ServerTimeLastRefresh() { return Value<float>(PropOffsets_DeusExPlayer.ServerTimeLastRefresh); }
	uint8_t& ShieldStatus() { return Value<uint8_t>(PropOffsets_DeusExPlayer.ShieldStatus); }
	float& ShieldTimer() { return Value<float>(PropOffsets_DeusExPlayer.ShieldTimer); }
	int& SkillPointsAvail() { return Value<int>(PropOffsets_DeusExPlayer.SkillPointsAvail); }
	std::string& SkillPointsAward() { return Value<std::string>(PropOffsets_DeusExPlayer.SkillPointsAward); }
	int& SkillPointsTotal() { return Value<int>(PropOffsets_DeusExPlayer.SkillPointsTotal); }
	//USkillManager*& SkillSystem() { return Value<USkillManager*>(PropOffsets_DeusExPlayer.SkillSystem); }
	std::string& TakenOverString() { return Value<std::string>(PropOffsets_DeusExPlayer.TakenOverString); }
	//UColorThemeManager*& ThemeManager() { return Value<UColorThemeManager*>(PropOffsets_DeusExPlayer.ThemeManager); }
	std::string& TooHeavyToLift() { return Value<std::string>(PropOffsets_DeusExPlayer.TooHeavyToLift); }
	std::string& TooMuchAmmo() { return Value<std::string>(PropOffsets_DeusExPlayer.TooMuchAmmo); }
	std::string& TorsoString() { return Value<std::string>(PropOffsets_DeusExPlayer.TorsoString); }
	std::string& TruePlayerName() { return Value<std::string>(PropOffsets_DeusExPlayer.TruePlayerName); }
	int& UIBackground() { return Value<int>(PropOffsets_DeusExPlayer.UIBackground); }
	UActor*& ViewModelActor() { return Value<UActor*>(PropOffsets_DeusExPlayer.ViewModelActor); }
	NameString& WallMaterial() { return Value<NameString>(PropOffsets_DeusExPlayer.WallMaterial); }
	vec3& WallNormal() { return Value<vec3>(PropOffsets_DeusExPlayer.WallNormal); }
	int& WarrenSlot() { return Value<int>(PropOffsets_DeusExPlayer.WarrenSlot); }
	float& WarrenTimer() { return Value<float>(PropOffsets_DeusExPlayer.WarrenTimer); }
	std::string& WeaponUnCloak() { return Value<std::string>(PropOffsets_DeusExPlayer.WeaponUnCloak); }
	std::string& WithString() { return Value<std::string>(PropOffsets_DeusExPlayer.WithString); }
	std::string& WithTheString() { return Value<std::string>(PropOffsets_DeusExPlayer.WithTheString); }
	//USpyDrone*& aDrone() { return Value<USpyDrone*>(PropOffsets_DeusExPlayer.aDrone); }
	BitfieldBool bAlwaysRun() { return BoolValue(PropOffsets_DeusExPlayer.bAlwaysRun); }
	BitfieldBool bAmmoDisplayVisible() { return BoolValue(PropOffsets_DeusExPlayer.bAmmoDisplayVisible); }
	BitfieldBool bAskedToTrain() { return BoolValue(PropOffsets_DeusExPlayer.bAskedToTrain); }
	BitfieldBool bAugDisplayVisible() { return BoolValue(PropOffsets_DeusExPlayer.bAugDisplayVisible); }
	BitfieldBool bAutoReload() { return BoolValue(PropOffsets_DeusExPlayer.bAutoReload); }
	BitfieldBool bBeltIsMPInventory() { return BoolValue(PropOffsets_DeusExPlayer.bBeltIsMPInventory); }
	BitfieldBool bBuySkills() { return BoolValue(PropOffsets_DeusExPlayer.bBuySkills); }
	BitfieldBool bCanLean() { return BoolValue(PropOffsets_DeusExPlayer.bCanLean); }
	BitfieldBool bCompassVisible() { return BoolValue(PropOffsets_DeusExPlayer.bCompassVisible); }
	BitfieldBool bConfirmNoteDeletes() { return BoolValue(PropOffsets_DeusExPlayer.bConfirmNoteDeletes); }
	BitfieldBool bConfirmSaveDeletes() { return BoolValue(PropOffsets_DeusExPlayer.bConfirmSaveDeletes); }
	BitfieldBool bCrosshairVisible() { return BoolValue(PropOffsets_DeusExPlayer.bCrosshairVisible); }
	BitfieldBool bCrouchOn() { return BoolValue(PropOffsets_DeusExPlayer.bCrouchOn); }
	BitfieldBool bDisplayAllGoals() { return BoolValue(PropOffsets_DeusExPlayer.bDisplayAllGoals); }
	BitfieldBool bDisplayAmmoByClip() { return BoolValue(PropOffsets_DeusExPlayer.bDisplayAmmoByClip); }
	BitfieldBool bDisplayCompletedGoals() { return BoolValue(PropOffsets_DeusExPlayer.bDisplayCompletedGoals); }
	BitfieldBool bFirstOptionsSynced() { return BoolValue(PropOffsets_DeusExPlayer.bFirstOptionsSynced); }
	BitfieldBool bForceDuck() { return BoolValue(PropOffsets_DeusExPlayer.bForceDuck); }
	BitfieldBool bHUDBackgroundTranslucent() { return BoolValue(PropOffsets_DeusExPlayer.bHUDBackgroundTranslucent); }
	BitfieldBool bHUDBordersTranslucent() { return BoolValue(PropOffsets_DeusExPlayer.bHUDBordersTranslucent); }
	BitfieldBool bHUDBordersVisible() { return BoolValue(PropOffsets_DeusExPlayer.bHUDBordersVisible); }
	BitfieldBool bHUDShowAllAugs() { return BoolValue(PropOffsets_DeusExPlayer.bHUDShowAllAugs); }
	BitfieldBool bHelpMessages() { return BoolValue(PropOffsets_DeusExPlayer.bHelpMessages); }
	BitfieldBool bHitDisplayVisible() { return BoolValue(PropOffsets_DeusExPlayer.bHitDisplayVisible); }
	BitfieldBool bIgnoreNextShowMenu() { return BoolValue(PropOffsets_DeusExPlayer.bIgnoreNextShowMenu); }
	BitfieldBool bInHandTransition() { return BoolValue(PropOffsets_DeusExPlayer.bInHandTransition); }
	BitfieldBool bKillerProfile() { return BoolValue(PropOffsets_DeusExPlayer.bKillerProfile); }
	BitfieldBool bMenusTranslucent() { return BoolValue(PropOffsets_DeusExPlayer.bMenusTranslucent); }
	BitfieldBool bNPCHighlighting() { return BoolValue(PropOffsets_DeusExPlayer.bNPCHighlighting); }
	BitfieldBool bNintendoImmunity() { return BoolValue(PropOffsets_DeusExPlayer.bNintendoImmunity); }
	BitfieldBool bObjectBeltVisible() { return BoolValue(PropOffsets_DeusExPlayer.bObjectBeltVisible); }
	BitfieldBool bObjectNames() { return BoolValue(PropOffsets_DeusExPlayer.bObjectNames); }
	BitfieldBool bQuotesEnabled() { return BoolValue(PropOffsets_DeusExPlayer.bQuotesEnabled); }
	BitfieldBool bSavingSkillsAugs() { return BoolValue(PropOffsets_DeusExPlayer.bSavingSkillsAugs); }
	BitfieldBool bSecondOptionsSynced() { return BoolValue(PropOffsets_DeusExPlayer.bSecondOptionsSynced); }
	BitfieldBool bShowAmmoDescriptions() { return BoolValue(PropOffsets_DeusExPlayer.bShowAmmoDescriptions); }
	BitfieldBool bSpyDroneActive() { return BoolValue(PropOffsets_DeusExPlayer.bSpyDroneActive); }
	BitfieldBool bStartNewGameAfterIntro() { return BoolValue(PropOffsets_DeusExPlayer.bStartNewGameAfterIntro); }
	BitfieldBool bStartingNewGame() { return BoolValue(PropOffsets_DeusExPlayer.bStartingNewGame); }
	BitfieldBool bSubtitles() { return BoolValue(PropOffsets_DeusExPlayer.bSubtitles); }
	BitfieldBool bToggleCrouch() { return BoolValue(PropOffsets_DeusExPlayer.bToggleCrouch); }
	BitfieldBool bToggleWalk() { return BoolValue(PropOffsets_DeusExPlayer.bToggleWalk); }
	BitfieldBool bWarrenEMPField() { return BoolValue(PropOffsets_DeusExPlayer.bWarrenEMPField); }
	BitfieldBool bWasCrouchOn() { return BoolValue(PropOffsets_DeusExPlayer.bWasCrouchOn); }
	float& curLeanDist() { return Value<float>(PropOffsets_DeusExPlayer.curLeanDist); }
	float& drugEffectTimer() { return Value<float>(PropOffsets_DeusExPlayer.drugEffectTimer); }
	UInventory*& inHand() { return Value<UInventory*>(PropOffsets_DeusExPlayer.inHand); }
	UInventory*& inHandPending() { return Value<UInventory*>(PropOffsets_DeusExPlayer.inHandPending); }
	uint8_t& invSlots() { return Value<uint8_t>(PropOffsets_DeusExPlayer.invSlots); }
	//UInvulnSphere*& invulnSph() { return Value<UInvulnSphere*>(PropOffsets_DeusExPlayer.invulnSph); }
	//UKillerProfile*& killProfile() { return Value<UKillerProfile*>(PropOffsets_DeusExPlayer.killProfile); }
	UActor*& lastFirstPersonConvoActor() { return Value<UActor*>(PropOffsets_DeusExPlayer.lastFirstPersonConvoActor); }
	float& lastFirstPersonConvoTime() { return Value<float>(PropOffsets_DeusExPlayer.lastFirstPersonConvoTime); }
	UActor*& lastThirdPersonConvoActor() { return Value<UActor*>(PropOffsets_DeusExPlayer.lastThirdPersonConvoActor); }
	float& lastThirdPersonConvoTime() { return Value<float>(PropOffsets_DeusExPlayer.lastThirdPersonConvoTime); }
	uint8_t& lastbDuck() { return Value<uint8_t>(PropOffsets_DeusExPlayer.lastbDuck); }
	float& logTimeout() { return Value<float>(PropOffsets_DeusExPlayer.logTimeout); }
	int& maxInvCols() { return Value<int>(PropOffsets_DeusExPlayer.maxInvCols); }
	int& maxInvRows() { return Value<int>(PropOffsets_DeusExPlayer.maxInvRows); }
	uint8_t& maxLogLines() { return Value<uint8_t>(PropOffsets_DeusExPlayer.maxLogLines); }
	int& mpMsgCode() { return Value<int>(PropOffsets_DeusExPlayer.mpMsgCode); }
	int& mpMsgFlags() { return Value<int>(PropOffsets_DeusExPlayer.mpMsgFlags); }
	int& mpMsgOptionalParam() { return Value<int>(PropOffsets_DeusExPlayer.mpMsgOptionalParam); }
	std::string& mpMsgOptionalString() { return Value<std::string>(PropOffsets_DeusExPlayer.mpMsgOptionalString); }
	int& mpMsgServerFlags() { return Value<int>(PropOffsets_DeusExPlayer.mpMsgServerFlags); }
	float& mpMsgTime() { return Value<float>(PropOffsets_DeusExPlayer.mpMsgTime); }
	float& musicChangeTimer() { return Value<float>(PropOffsets_DeusExPlayer.musicChangeTimer); }
	float& musicCheckTimer() { return Value<float>(PropOffsets_DeusExPlayer.musicCheckTimer); }
	uint8_t& musicMode() { return Value<uint8_t>(PropOffsets_DeusExPlayer.musicMode); }
	UPawn*& myBurner() { return Value<UPawn*>(PropOffsets_DeusExPlayer.myBurner); }
	UActor*& myKiller() { return Value<UActor*>(PropOffsets_DeusExPlayer.myKiller); }
	UPawn*& myPoisoner() { return Value<UPawn*>(PropOffsets_DeusExPlayer.myPoisoner); }
	UActor*& myProjKiller() { return Value<UActor*>(PropOffsets_DeusExPlayer.myProjKiller); }
	UActor*& myTurretKiller() { return Value<UActor*>(PropOffsets_DeusExPlayer.myTurretKiller); }
	int& poisonCounter() { return Value<int>(PropOffsets_DeusExPlayer.poisonCounter); }
	int& poisonDamage() { return Value<int>(PropOffsets_DeusExPlayer.poisonDamage); }
	float& poisonTimer() { return Value<float>(PropOffsets_DeusExPlayer.poisonTimer); }
	float& prevLeanDist() { return Value<float>(PropOffsets_DeusExPlayer.prevLeanDist); }
	int& saveCount() { return Value<int>(PropOffsets_DeusExPlayer.saveCount); }
	float& saveTime() { return Value<float>(PropOffsets_DeusExPlayer.saveTime); }
	uint8_t& savedSection() { return Value<uint8_t>(PropOffsets_DeusExPlayer.savedSection); }
	int& spyDroneLevel() { return Value<int>(PropOffsets_DeusExPlayer.spyDroneLevel); }
	float& spyDroneLevelValue() { return Value<float>(PropOffsets_DeusExPlayer.spyDroneLevelValue); }
	std::string& strStartMap() { return Value<std::string>(PropOffsets_DeusExPlayer.strStartMap); }
	float& swimBubbleTimer() { return Value<float>(PropOffsets_DeusExPlayer.swimBubbleTimer); }
	float& swimDuration() { return Value<float>(PropOffsets_DeusExPlayer.swimDuration); }
	float& swimTimer() { return Value<float>(PropOffsets_DeusExPlayer.swimTimer); }
	uint8_t& translucencyLevel() { return Value<uint8_t>(PropOffsets_DeusExPlayer.translucencyLevel); }

private:
	UDXGameDirectory* m_GameDirectory = nullptr;
};

struct UDXInitialAllianceInfo
{
	NameString AllianceName;
	float AllianceLevel;
	BitfieldBool bPermanent;
};

struct UDXInitialAllianceInfoEx
{
	NameString AllianceName;
	float AllianceLevel;
	float AllianceAgitation;
	BitfieldBool bPermanent;
};

class UScriptedPawn : public UPawn
{
public:
	using UPawn::UPawn;
	FixedArrayView<NameString, 4> Carcasses() { return FixedArray<NameString, 4>(PropOffsets_ScriptedPawn.Carcasses);}
	FixedArrayView<UDXInitialAllianceInfo, 8> InitialAlliances() { return FixedArray<UDXInitialAllianceInfo, 8>(PropOffsets_ScriptedPawn.InitialAlliances);}
	FixedArrayView<UDXInitialAllianceInfoEx, 16> AlliancesEx() { return FixedArray<UDXInitialAllianceInfoEx, 16>(PropOffsets_ScriptedPawn.AlliancesEx);}
	int& NumCarcasses() { return Value<int>(PropOffsets_ScriptedPawn.NumCarcasses);}
	BitfieldBool bLikesNeutral() { return BoolValue(PropOffsets_ScriptedPawn.bLikesNeutral);}
	BitfieldBool bReverseAlliances() { return BoolValue(PropOffsets_ScriptedPawn.bReverseAlliances);}
	void AddCarcass(const NameString& CarcassName);
	void ConBindEvents();
	uint8_t GetAllianceType(const NameString& AllianceName);
	uint8_t GetPawnAllianceType(UPawn* QueryPawn);
	bool HaveSeenCarcass(const NameString& CarcassName);
	bool IsValidEnemy(UPawn* TestEnemy, std::optional<bool> bCheckAlliance);
};

class UDeusExDecoration : public UDecoration
{
public:
	using UDecoration::UDecoration;

	void ConBindEvents();
};

// Deus Ex
struct XAIParams
{
	UActor* BestActor;
	float Score;
	float Visibility;
	float Volume;
	float Smell;
};
