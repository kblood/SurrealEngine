#pragma once

#include <cstddef>
#include <cstdint>

namespace WebXRTwoHandWeapon
{
	constexpr uint32_t MetadataSchemaVersion = 1;
	constexpr uint32_t DiagnosticsSchemaVersion = 1;
	constexpr float DefaultGrabSqueeze = 0.60f;
	constexpr float DefaultReleaseSqueeze = 0.40f;
	constexpr float DefaultGrabRadiusMeters = 0.12f;
	constexpr float DefaultReleaseRadiusMeters = 0.25f;
	constexpr float DefaultBlendSeconds = 0.10f;
	constexpr float DefaultMinimumBaselineMeters = 0.15f;

	struct Vec3
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
	};

	struct Basis
	{
		Vec3 Forward{ 1.0f, 0.0f, 0.0f };
		Vec3 Right{ 0.0f, 1.0f, 0.0f };
		Vec3 Up{ 0.0f, 0.0f, 1.0f };
	};

	struct MetadataRow
	{
		uint32_t SchemaVersion = MetadataSchemaVersion;
		const char* PackageName = nullptr;
		const char* ClassName = nullptr;
		bool TwoHandEligible = false;
		Vec3 ForegripLocalUU;
		float GrabSqueeze = DefaultGrabSqueeze;
		float ReleaseSqueeze = DefaultReleaseSqueeze;
		float GrabRadiusMeters = DefaultGrabRadiusMeters;
		float ReleaseRadiusMeters = DefaultReleaseRadiusMeters;
		const char* PackageHash = nullptr;
		const char* ControllerProfile = nullptr;
		uint32_t DominantHand = 0;
		float WorldUnitsPerMeter = 0.0f;
		const char* Headset = nullptr;
		const char* Runtime = nullptr;
	};

	struct MetadataQuery
	{
		const char* PackageName = nullptr;
		const char* ClassName = nullptr;
		const char* PackageHash = nullptr;
		const char* ControllerProfile = nullptr;
		const char* Headset = nullptr;
		const char* Runtime = nullptr;
		uint32_t DominantHand = 0;
		float WorldUnitsPerMeter = 0.0f;
	};

	enum class Rejection : uint32_t
	{
		None = 0,
		Disabled,
		MissingMetadata,
		DuplicateMetadata,
		InvalidMetadata,
		WrongPackageHash,
		WrongControllerProfile,
		WrongHeadset,
		WrongRuntime,
		WrongDominantHand,
		WrongWorldScale,
		NotLocalCurrentWeapon,
		RemotePawn,
		BotPawn,
		StaleWeapon,
		WrongOwner,
		PawnDead,
		MenuOwned,
		TrackingLost,
		NonFiniteInput,
		StaleFrame,
		FreshConditionRequired,
		OutsideGrabCondition,
		DegenerateBasis
	};

	enum class ResetReason : uint32_t
	{
		None = 0,
		Disabled,
		SessionChanged,
		ReferenceSpaceReset,
		SourceChanged,
		DominantHandChanged,
		PawnChanged,
		WeaponChanged,
		PawnDead,
		MenuOwned,
		TrackingLost,
		NonFiniteInput,
		StaleFrame,
		Ineligible,
		ExceptionUnwind
	};

	struct MetadataLookup
	{
		const MetadataRow* Row = nullptr;
		Rejection Why = Rejection::MissingMetadata;
	};

	MetadataLookup FindQualifiedMetadata(const MetadataRow* rows, size_t rowCount,
		const MetadataQuery& query);

	struct FrameInput
	{
		bool Enabled = true;
		bool LocalCurrentWeapon = true;
		Rejection OwnershipRejection = Rejection::None;
		bool PawnAlive = true;
		bool MenuOwned = false;
		bool MainGripTracked = true;
		bool OffGripTracked = true;
		uint64_t FrameGeneration = 0;
		uint32_t SessionGeneration = 0;
		uint32_t ReferenceSpaceGeneration = 0;
		uint32_t DominantSourceId = 0;
		uint32_t OffSourceId = 0;
		uint32_t DominantHand = 0;
		uintptr_t PawnIdentity = 0;
		uintptr_t WeaponIdentity = 0;
		const MetadataRow* Metadata = nullptr;
		Rejection MetadataRejection = Rejection::MissingMetadata;
		float DeltaSeconds = 0.0f;
		float WorldUnitsPerMeter = 39.3701f;
		float OffGripSqueeze = 0.0f;
		Vec3 MainGripPositionUU;
		Vec3 OffGripPositionUU;
		Basis MainGripBasis;
		Basis OneHandBasis;
	};

	struct FrameOutput
	{
		Basis PresentationBasis;
		Vec3 BallisticForward{ 1.0f, 0.0f, 0.0f };
		Vec3 MainVisualOriginUU;
		Vec3 ForegripWorldUU;
		bool Eligible = false;
		bool Active = false;
		float GrabBlend = 0.0f;
		float BaselineWeight = 0.0f;
		float EffectiveWeight = 0.0f;
		float GripDistanceMeters = 0.0f;
		float ForegripDistanceMeters = 0.0f;
		Rejection Why = Rejection::None;
	};

	struct Diagnostics
	{
		uint32_t SchemaVersion = DiagnosticsSchemaVersion;
		uint32_t ProductionMetadataRows = 0;
		uint64_t LastFrameGeneration = 0;
		uint32_t SessionGeneration = 0;
		uint32_t ReferenceSpaceGeneration = 0;
		uint32_t DominantSourceId = 0;
		uint32_t OffSourceId = 0;
		uint32_t DominantHand = 0;
		uint32_t ResetCount = 0;
		uint32_t RejectionCount = 0;
		uint32_t GrabCount = 0;
		uint32_t ReleaseCount = 0;
		uint32_t BasisFallbackCount = 0;
		ResetReason LastReset = ResetReason::None;
		Rejection LastRejection = Rejection::None;
		bool Enabled = true;
		bool Eligible = false;
		bool Active = false;
		bool FreshConditionRequired = false;
		float OffGripSqueeze = 0.0f;
		float GripDistanceMeters = 0.0f;
		float ForegripDistanceMeters = 0.0f;
		float GrabBlend = 0.0f;
		float BaselineWeight = 0.0f;
		float EffectiveWeight = 0.0f;
		float FilterLatencySeconds = 0.0f; // Filtering is deliberately off by default.
		float OrthonormalError = 0.0f;
		Basis OneHandBasis;
		Basis TwoHandBasis;
		Basis BlendedBasis;
	};

	struct State
	{
		bool HasIdentity = false;
		bool Active = false;
		bool FreshConditionRequired = false;
		float GrabBlend = 0.0f;
		uint64_t LastFrameGeneration = 0;
		uint32_t SessionGeneration = 0;
		uint32_t ReferenceSpaceGeneration = 0;
		uint32_t DominantSourceId = 0;
		uint32_t OffSourceId = 0;
		uint32_t DominantHand = 0;
		uintptr_t PawnIdentity = 0;
		uintptr_t WeaponIdentity = 0;
		uintptr_t MetadataIdentity = 0;
		Diagnostics Stats;
	};

	void Reset(State& state, ResetReason reason, bool requireFreshCondition = true);
	FrameOutput Advance(State& state, const FrameInput& input);
	bool RunSelfTest();
}
