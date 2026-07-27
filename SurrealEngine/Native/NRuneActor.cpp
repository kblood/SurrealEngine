
#include "Precomp.h"
#include "NRuneActor.h"
#include "VM/NativeFunc.h"
#include "UObject/UActor.h"

// Rune's licensed engine fork added a named-joint/skeletal-attachment system
// (Skeletal/SkelMesh/SkelGroupSkins/SkelGroupFlags/JointChild) that this engine
// does not implement (see Docs/RUNE_SUPPORT_PLAN.md M3). These natives are
// stubbed to the coarsest behavior that keeps Rune's script code from crashing
// or leaking actors, without pretending to model per-joint transforms.
void NRuneActor::RegisterFunctions()
{
	RegisterVMNativeFunc_2("Actor", "AttachActorToJoint", &NRuneActor::AttachActorToJoint, 613);
	RegisterVMNativeFunc_2("Actor", "JointNamed", &NRuneActor::JointNamed, 619);
	RegisterVMNativeFunc_5("Actor", "TraceTexture", &NRuneActor::TraceTexture, 666);
	RegisterVMNativeFunc_2("Actor", "GetJointPos", &NRuneActor::GetJointPos, 602);
	RegisterVMNativeFunc_1("Actor", "ResetAnimationCache", &NRuneActor::ResetAnimationCache, 620);
	RegisterVMNativeFunc_0("Actor", "SetDefaultPolygroups", &NRuneActor::SetDefaultPolygroups, 610);
	RegisterVMNativeFunc_1("Pawn", "SkeletonLook", &NRuneActor::SkeletonLook, 670);
	RegisterVMNativeFunc_2("Actor", "SetJointRot", &NRuneActor::SetJointRot, 621);
	RegisterVMNativeFunc_2("Actor", "DetachActorFromJoint", &NRuneActor::DetachActorFromJoint, 614);
}

void NRuneActor::AttachActorToJoint(UObject* Self, UObject* A, int j)
{
	// No joint offset table exists, so the attached actor is based on Self
	// directly (follows Self's overall position) rather than a named joint.
	UActor* attachee = UObject::TryCast<UActor>(A);
	if (attachee)
		attachee->SetBase(UObject::Cast<UActor>(Self), true);
}

void NRuneActor::JointNamed(UObject* Self, const NameString& jointname, int& ReturnValue)
{
	// -1 means "no such joint" to any caller checking the result, and is safe
	// to pass straight into AttachActorToJoint above, which ignores it anyway.
	ReturnValue = -1;
}

void NRuneActor::TraceTexture(UObject* Self, const vec3& TraceEnd, const vec3& TraceStart, int& Flags, vec3& ScrollDir, UObject*& ReturnValue)
{
	UActor* SelfActor = UObject::Cast<UActor>(Self);
	vec3 hitLocation, hitNormal;
	ReturnValue = SelfActor->Trace(hitLocation, hitNormal, TraceEnd, TraceStart, false, vec3(0.0f, 0.0f, 0.0f));
	// Surface scroll/flag data (moving water, conveyors, etc.) isn't tracked by
	// this engine's textures, so these report "no scroll" rather than fabricate a value.
	Flags = 0;
	ScrollDir = vec3(0.0f, 0.0f, 0.0f);
}

void NRuneActor::GetJointPos(UObject* Self, int joint, vec3& ReturnValue)
{
	// Falls back to the actor's own location since per-joint transforms aren't modeled.
	ReturnValue = UObject::Cast<UActor>(Self)->Location();
}

void NRuneActor::ResetAnimationCache(UObject* Self, const NameString& seq)
{
	// No-op: there is no animation cache to invalidate.
}

void NRuneActor::SetDefaultPolygroups(UObject* Self)
{
	// No-op: cosmetic menu polygon-group setup (RuneMenu.Paint) with no
	// gameplay effect once accepted as a harmless call.
}

void NRuneActor::SkeletonLook(UObject* Self, float DeltaTime)
{
	// No-op: per-tick head/bone look-at update (RunePlayer.Tick calls this
	// every frame). Without a joint offset table there is nothing to rotate,
	// consistent with GetJointPos/AttachActorToJoint above.
}

void NRuneActor::SetJointRot(UObject* Self, int joint, const Rotator& Rot)
{
	// No-op: per-tick jaw/joint rotation (Pawn.Jaw calls this every tick while
	// talking). Without a joint offset table there is nothing to rotate,
	// consistent with GetJointPos/SkeletonLook above.
}

void NRuneActor::DetachActorFromJoint(UObject* Self, int j, UObject*& ReturnValue)
{
	UActor* selfActor = UObject::Cast<UActor>(Self);
	ReturnValue = nullptr;

	// AttachActorToJoint approximates Rune's missing joint system with ordinary
	// actor basing. Prefer an owned based actor so a pawn standing on Self is
	// never mistaken for a joint attachment merely because the joint index is
	// unavailable to this engine.
	for (auto it = selfActor->BasedActors.rbegin(); it != selfActor->BasedActors.rend(); ++it)
	{
		UActor* actor = *it;
		if (actor && actor->Owner() == selfActor)
		{
			ReturnValue = actor;
			actor->SetBase(nullptr, true);
			return;
		}
	}
}
