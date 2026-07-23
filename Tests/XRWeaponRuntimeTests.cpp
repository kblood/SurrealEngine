#include "Precomp.h"
#include "XR/XRWeaponRuntime.h"

#include <iostream>
#include <stdexcept>

namespace
{
	using XRWeaponRuntime::AimScopeKind;
	using XRWeaponRuntime::CallMetadata;

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	bool Exact(const Rotator& value, const Rotator& expected)
	{
		return value.Pitch == expected.Pitch && value.Yaw == expected.Yaw && value.Roll == expected.Roll;
	}

	CallMetadata Global(std::string_view packageName, std::string_view className,
		std::string_view functionName)
	{
		return { packageName, className, {}, "Firing", functionName };
	}

	CallMetadata State(std::string_view className, std::string_view declaringState,
		std::string_view activeState, std::string_view functionName)
	{
		return { "Botpack", className, declaringState, activeState, functionName };
	}

	void TestUT99Classification()
	{
		auto expect = [](const CallMetadata& call, AimScopeKind expected)
		{
			Require(XRWeaponRuntime::ClassifyUT99WeaponCall(call) == expected,
				"UT99 weapon call was classified incorrectly");
		};

		expect(Global("Engine", "Weapon", "TraceFire"), AimScopeKind::Ballistic);
		expect(Global("engine", "weapon", "PROJECTILEFIRE"), AimScopeKind::Ballistic);
		expect(Global("Engine", "Pawn", "AdjustAim"), AimScopeKind::Ballistic);
		expect(Global("Engine", "Pawn", "ADJUSTTOSS"), AimScopeKind::Ballistic);
		expect(Global("Botpack", "UT_FlakCannon", "Fire"), AimScopeKind::Ballistic);
		expect(Global("Botpack", "UT_FlakCannon", "AltFire"), AimScopeKind::Ballistic);
		expect(State("UT_Eightball", "FireRockets", "FireRockets", "BeginState"), AimScopeKind::Ballistic);
		expect(Global("Botpack", "UT_Eightball", "CheckTarget"), AimScopeKind::TargetAcquisition);
		expect(Global("Botpack", "Translocator", "ThrowTarget"), AimScopeKind::Ballistic);
		expect(Global("Botpack", "ChainSaw", "Slash"), AimScopeKind::Ballistic);
		expect(Global("Botpack", "ImpactHammer", "TraceAltFire"), AimScopeKind::Ballistic);
		expect(State("ImpactHammer", "Firing", "Firing", "Tick"), AimScopeKind::Ballistic);
		expect(Global("Botpack", "StarterBolt", "Tick"), AimScopeKind::Ballistic);
		expect(Global("Engine", "Weapon", "RenderOverlays"), AimScopeKind::Presentation);
		expect(Global("CustomWeapons", "ModWeapon", "RenderOverlays"), AimScopeKind::Presentation);

		const CallMetadata rejected[] = {
			Global("Botpack", "TournamentWeapon", "Fire"),
			Global("Engine", "Actor", "Tick"),
			State("UT_Eightball", "NormalFire", "NormalFire", "Tick"),
			State("ImpactHammer", "Firing", "ClientFiring", "Tick"),
			Global("Botpack", "PBolt", "Tick"),
			Global("Botpack", "StarterBolt", "Timer"),
			Global("WrongPackage", "StarterBolt", "Tick"),
			Global("WrongPackage", "UT_FlakCannon", "Fire"),
			Global("Botpack", "FlakCannon", "Fire"),
			State("WrongClass", "FireRockets", "FireRockets", "BeginState"),
			Global("Botpack", "GuidedWarShell", "TraceFire"),
			Global("Botpack", "GuidedWarShell", "AdjustAim"),
			Global("Engine", "Pawn", "AdjustAiming"),
			State("TournamentWeapon", "Firing", "Firing", "RenderOverlays"),
			Global("Engine", "Weapon", "CalcDrawOffset")
		};
		for (const CallMetadata& call : rejected)
			expect(call, AimScopeKind::None);
	}

	void TestScopedRotatorRestoration()
	{
		XRWeaponRuntime::TransformInputs transforms;
		transforms.BallisticRotationValid = true;
		transforms.BallisticRotation = Rotator(1001, 2002, 0);
		transforms.PresentationRotationValid = true;
		transforms.PresentationRotation = Rotator(3003, 4004, 5005);

		Rotator pawn(0x12345, -0x23456, 0x34567);
		Rotator weapon(-0x45678, 0x56789, -0x6789a);
		const Rotator pawnBaseline = pawn;
		const Rotator weaponBaseline = weapon;

		auto outer = XRWeaponRuntime::BeginRotationScope(AimScopeKind::Ballistic,
			{ &pawn, &weapon }, transforms);
		Require(outer && Exact(pawn, transforms.BallisticRotation) && Exact(weapon, weaponBaseline),
			"ballistic scope did not replace only the pawn rotation");
		auto inner = XRWeaponRuntime::BeginRotationScope(AimScopeKind::Presentation,
			{ &pawn, &weapon }, transforms);
		Require(inner && Exact(pawn, transforms.PresentationRotation) &&
			Exact(weapon, transforms.PresentationRotation),
			"presentation scope did not replace both rotations");
		inner();
		Require(Exact(pawn, transforms.BallisticRotation) && Exact(weapon, weaponBaseline),
			"inner presentation scope did not restore its outer state");
		outer();
		Require(Exact(pawn, pawnBaseline) && Exact(weapon, weaponBaseline),
			"outer ballistic scope did not restore exact baseline values");

		XRWeaponRuntime::TransformInputs invalid;
		Require(!XRWeaponRuntime::BeginRotationScope(AimScopeKind::Ballistic,
			{ &pawn, &weapon }, invalid) && Exact(pawn, pawnBaseline),
			"invalid ballistic transform changed runtime state");
		Require(!XRWeaponRuntime::BeginRotationScope(AimScopeKind::Presentation,
			{ &pawn, nullptr }, transforms) && Exact(pawn, pawnBaseline),
			"incomplete presentation targets changed runtime state");
	}

	void TestVMHookAdapter()
	{
		Rotator pawn(11, 22, 33);
		Rotator weapon(44, 55, 66);
		int resolverCalls = 0;
		CallMetadata currentCall = Global("Botpack", "UT_FlakCannon", "AltFire");
		XRWeaponRuntime::ScopeResolver resolver = [&](UFunction*, UObject*)
			-> std::optional<XRWeaponRuntime::ScopeRequest>
		{
			resolverCalls++;
			XRWeaponRuntime::ScopeRequest request;
			request.Call = currentCall;
			request.Targets = { &pawn, &weapon };
			request.Transforms.BallisticRotationValid = true;
			request.Transforms.BallisticRotation = Rotator(101, 202, 0);
			return request;
		};

		VMCallHook hook = XRWeaponRuntime::MakeUT99WeaponCallHook(std::move(resolver), 47);
		Require(hook.Order == 47, "VM hook order was not preserved");
		VMCallHookRegistry registry;
		registry.Register(std::move(hook));
		Array<ExpressionValue> arguments;
		const CallMetadata ballisticCalls[] = {
			Global("Engine", "Weapon", "TraceFire"),
			Global("Engine", "Weapon", "ProjectileFire"),
			Global("Engine", "Pawn", "AdjustAim"),
			Global("Engine", "Pawn", "AdjustToss"),
			Global("Botpack", "UT_FlakCannon", "Fire"),
			Global("Botpack", "UT_FlakCannon", "AltFire"),
			State("UT_Eightball", "FireRockets", "FireRockets", "BeginState"),
			Global("Botpack", "UT_Eightball", "CheckTarget"),
			Global("Botpack", "ImpactHammer", "TraceAltFire"),
			State("ImpactHammer", "Firing", "Firing", "Tick"),
			Global("Botpack", "StarterBolt", "Tick")
		};
		for (const CallMetadata& call : ballisticCalls)
		{
			currentCall = call;
			auto scope = registry.BeginCall(nullptr, nullptr, arguments);
			Require(Exact(pawn, Rotator(101, 202, 0)),
				"VM hook adapter did not enter a classified ballistic scope");
		}
		Require(Exact(pawn, Rotator(11, 22, 33)) && Exact(weapon, Rotator(44, 55, 66)),
			"VM hook cleanup did not restore rotations after classified calls");

		currentCall = Global("Botpack", "UT_FlakCannon", "AltFire");
		{
			auto outer = registry.BeginCall(nullptr, nullptr, arguments);
			Require(Exact(pawn, Rotator(101, 202, 0)),
				"outer VM weapon scope did not apply hand aim");
			{
				auto inner = registry.BeginCall(nullptr, nullptr, arguments);
				Require(Exact(pawn, Rotator(101, 202, 0)),
					"nested VM weapon scope did not preserve hand aim");
			}
			Require(Exact(pawn, Rotator(101, 202, 0)),
				"nested VM weapon scope did not restore its outer state");
		}
		Require(Exact(pawn, Rotator(11, 22, 33)) && Exact(weapon, Rotator(44, 55, 66)),
			"nested VM hook cleanup did not restore exact baseline rotations");

		currentCall = Global("Botpack", "TournamentWeapon", "Fire");
		{
			auto scope = registry.BeginCall(nullptr, nullptr, arguments);
			Require(Exact(pawn, Rotator(11, 22, 33)) && Exact(weapon, Rotator(44, 55, 66)),
				"unclassified weapon call changed rotations");
		}
		Require(resolverCalls == 14, "VM hook resolver call count was unexpected");
	}

	void TestIndependentDualEnforcerFirePolicy()
	{
		using XRWeaponRuntime::PairedOffHandTriggerAction;
		Require(XRWeaponRuntime::ResolvePairedOffHandTrigger(false, true) ==
			PairedOffHandTriggerAction::DefaultAlternateFire,
			"single weapon did not retain ordinary alternate fire");
		Require(XRWeaponRuntime::ResolvePairedOffHandTrigger(true, true) ==
			PairedOffHandTriggerAction::FireSlave,
			"paired off-hand press did not select manual slave fire");
		Require(XRWeaponRuntime::ResolvePairedOffHandTrigger(true, false) ==
			PairedOffHandTriggerAction::ConsumeRelease,
			"paired off-hand release leaked into alternate fire");
		Require(XRWeaponRuntime::ResolvePairedOffHandTrigger(true, false, true) ==
			PairedOffHandTriggerAction::DefaultAlternateFire,
			"pairing while alternate fire was held did not balance its key release");

		Rotator pawn(11, 22, 33);
		XRWeaponRuntime::ScopeResolver resolver = [&](UFunction*, UObject*)
			-> std::optional<XRWeaponRuntime::ScopeRequest>
		{
			XRWeaponRuntime::ScopeRequest request;
			request.Call = Global("Engine", "Weapon", "TraceFire");
			request.Targets.PawnViewRotation = &pawn;
			request.SuppressDispatch = true;
			return request;
		};
		VMCallHookRegistry registry;
		registry.Register(XRWeaponRuntime::MakeUT99WeaponCallHook(
			std::move(resolver)));
		Array<ExpressionValue> arguments;
		auto scope = registry.BeginCall(nullptr, nullptr, arguments);
		Require(scope.DispatchSuppressed() &&
			scope.OverriddenResult().GetType() == ExpressionValueType::Nothing,
			"unrequested slave ballistic call was not suppressed");
		Require(Exact(pawn, Rotator(11, 22, 33)),
			"suppressed slave call changed pawn rotation");
	}
}

int main()
{
	try
	{
		TestUT99Classification();
		TestScopedRotatorRestoration();
		TestVMHookAdapter();
		TestIndependentDualEnforcerFirePolicy();
		std::cout << "XR weapon runtime tests passed\n";
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "XR weapon runtime test failed: " << e.what() << '\n';
		return 1;
	}
}
