#pragma once

#include "Packages/Engine/Actors/Pawn/UPawn.h"

// These mirror the UnrealScript AllianceInfo/AllianceInfoEx structs and are laid
// over the property data, so their size has to match what UStruct computes: bool
// members are packed into a 32-bit word, not stored as a BitfieldBool reference.
// Getting the size wrong only corrupts elements past the first, which is why a
// pawn whose hostile entry was not AlliancesEx[0] never saw the player as hostile.
struct UDXInitialAllianceInfo
{
	NameString AllianceName;
	float AllianceLevel;
	uint32_t bPermanent;
};
static_assert(sizeof(UDXInitialAllianceInfo) == 16, "AllianceInfo layout must match the script struct");

struct UDXInitialAllianceInfoEx
{
	NameString AllianceName;
	float AllianceLevel;
	float AllianceAgitation;
	uint32_t bPermanent;
};
static_assert(sizeof(UDXInitialAllianceInfoEx) == 20, "AllianceInfoEx layout must match the script struct");

class UScriptedPawn : public UPawn
{
public:
	using UPawn::UPawn;
	FixedArrayView<NameString, 4> Carcasses() { return FixedArray<NameString, 4>(PropOffsets_ScriptedPawn.Carcasses); }
	FixedArrayView<UDXInitialAllianceInfo, 8> InitialAlliances() { return FixedArray<UDXInitialAllianceInfo, 8>(PropOffsets_ScriptedPawn.InitialAlliances); }
	FixedArrayView<UDXInitialAllianceInfoEx, 16> AlliancesEx() { return FixedArray<UDXInitialAllianceInfoEx, 16>(PropOffsets_ScriptedPawn.AlliancesEx); }
	int& NumCarcasses() { return Value<int>(PropOffsets_ScriptedPawn.NumCarcasses); }
	BitfieldBool bLikesNeutral() { return BoolValue(PropOffsets_ScriptedPawn.bLikesNeutral); }
	BitfieldBool bReverseAlliances() { return BoolValue(PropOffsets_ScriptedPawn.bReverseAlliances); }
	void AddCarcass(const NameString& CarcassName);
	void ConBindEvents();
	uint8_t GetAllianceType(const NameString& AllianceName);
	uint8_t GetPawnAllianceType(UPawn* QueryPawn);
	bool HaveSeenCarcass(const NameString& CarcassName);
	bool IsValidEnemy(UPawn* TestEnemy, std::optional<bool> bCheckAlliance);

	// ScriptedPawn.Tick's per-frame timer block is commented out in the script
	// (ScriptedPawn.uc 7198-7358); retail runs it natively instead.
	void TickScriptedPawnTimers(float elapsed);
};

struct XAIParams
{
	UActor* BestActor;
	float Score;
	float Visibility;
	float Volume;
	float Smell;
};
