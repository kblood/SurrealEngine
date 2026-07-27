
#include "Precomp.h"
#include "NRuneActor.h"
#include "VM/NativeFunc.h"
#include "VM/ScriptCall.h"
#include "VM/Iterator.h"
#include "VM/Frame.h"
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
	RegisterVMNativeFunc_2("Actor", "GetJointRot", &NRuneActor::GetJointRot, 603);
	RegisterVMNativeFunc_2("Actor", "GetJointName", &NRuneActor::GetJointName, 605);
	RegisterVMNativeFunc_2("Actor", "ApplyJointForce", &NRuneActor::ApplyJointForce, 608);
	RegisterVMNativeFunc_1("Actor", "NumJoints", &NRuneActor::NumJoints, 609);
	RegisterVMNativeFunc_1("Actor", "ResetAnimationCache", &NRuneActor::ResetAnimationCache, 620);
	RegisterVMNativeFunc_0("Actor", "SetDefaultPolygroups", &NRuneActor::SetDefaultPolygroups, 610);
	RegisterVMNativeFunc_0("Actor", "SetDefaultJointFlags", &NRuneActor::SetDefaultJointFlags, 611);
	RegisterVMNativeFunc_1("Pawn", "SkeletonLook", &NRuneActor::SkeletonLook, 670);
	RegisterVMNativeFunc_2("Actor", "SetJointRot", &NRuneActor::SetJointRot, 621);
	RegisterVMNativeFunc_2("Actor", "TurnJointTo", &NRuneActor::TurnJointTo, 616);
	RegisterVMNativeFunc_2("Actor", "ClosestJointTo", &NRuneActor::ClosestJointTo, 618);
	RegisterVMNativeFunc_4("Actor", "FrameSweep", &NRuneActor::FrameSweep, 622);
	RegisterVMNativeFunc_11("Actor", "SweepActors", &NRuneActor::SweepActors, 313);
	RegisterVMNativeFunc_2("Actor", "DetachActorFromJoint", &NRuneActor::DetachActorFromJoint, 614);
	RegisterVMNativeFunc_2("Actor", "ActorAttachedTo", &NRuneActor::ActorAttachedTo, 617);
}

void NRuneActor::AttachActorToJoint(UObject* Self, UObject* A, int j)
{
	// No joint offset table exists, so the attached actor is based on Self
	// directly (follows Self's overall position) rather than a named joint. Keep
	// the joint identity so Rune can later query or detach the same actor.
	UActor* selfActor = UObject::Cast<UActor>(Self);
	UActor* attachee = UObject::TryCast<UActor>(A);
	if (attachee)
	{
		UActor* previous = selfActor->JointAttachment(j);
		if (previous && previous != attachee)
			previous->SetBase(nullptr, true);

		attachee->SetBase(selfActor, true);
		selfActor->SetJointAttachment(j, attachee);
	}
}

void NRuneActor::JointNamed(UObject* Self, const NameString& jointname, int& ReturnValue)
{
	// Rune treats zero as "no such joint". NameString compare indices are stable
	// and case-insensitive, so use a negative namespace to keep named joints
	// distinct from the non-negative numeric joint IDs used by Rune scripts.
	ReturnValue = jointname.IsNone() ? 0 : -1 - jointname.GetCompareIndex();
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

void NRuneActor::GetJointRot(UObject* Self, int joint, Rotator& ReturnValue)
{
	// Falls back to the actor's overall rotation without a per-joint pose.
	ReturnValue = UObject::Cast<UActor>(Self)->Rotation();
}

void NRuneActor::GetJointName(UObject* Self, int joint, std::string& ReturnValue)
{
	// Numeric and synthetic joint IDs do not have a skeletal name table.
	ReturnValue.clear();
}

void NRuneActor::ApplyJointForce(UObject* Self, int joint, const vec3& force)
{
	// No-op: applying a local bone impulse to the whole actor would create
	// incorrect gameplay motion without Rune's skeletal physics model.
}

void NRuneActor::NumJoints(UObject* Self, int& ReturnValue)
{
	// Report no enumerable skeleton. Named and explicit attachment IDs still
	// work through the runtime attachment identity maintained by UActor.
	ReturnValue = 0;
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

void NRuneActor::SetDefaultJointFlags(UObject* Self)
{
	// No-op: joint flags have no backing skeleton in this compatibility layer.
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

void NRuneActor::TurnJointTo(UObject* Self, int joint, const Rotator& Rot)
{
	// No-op for the same reason as SetJointRot.
}

void NRuneActor::ClosestJointTo(UObject* Self, const vec3& point, int& ReturnValue)
{
	// Joint zero is Rune's root/body fallback when no per-joint geometry exists.
	ReturnValue = 0;
}

void NRuneActor::FrameSweep(UObject* Self, int curframe, const vec3& weaponVector, vec3& lastB1, vec3& lastE1)
{
	UActor* selfActor = UObject::Cast<UActor>(Self);
	const vec3 currentBase = selfActor->Location();
	const vec3 currentEnd = currentBase + Coords::Rotation(selfActor->Rotation()).Inverse() * weaponVector;

	vec3 previousBase = lastB1;
	vec3 previousEnd = lastE1;
	if (dot(previousBase, previousBase) == 0.0f && dot(previousEnd, previousEnd) == 0.0f)
	{
		previousBase = currentBase;
		previousEnd = currentEnd;
	}

	CallEvent(selfActor, "FrameSwept", {
		ExpressionValue::VectorValue(previousBase),
		ExpressionValue::VectorValue(previousEnd),
		ExpressionValue::VectorValue(currentBase),
		ExpressionValue::VectorValue(currentEnd)
	});

	lastB1 = currentBase;
	lastE1 = currentEnd;
}

void NRuneActor::SweepActors(UObject* Self, UObject* BaseClass, UObject*& Actor,
	const vec3& Start1, const vec3& Stop1, const vec3& Start2, const vec3& Stop2,
	float ExtentRadius, vec3& HitLoc, vec3& HitNorm, int& LowJointMask, int& HighJointMask)
{
	Frame::CreatedIterator = std::make_unique<SweepActorsIterator>(
		UObject::Cast<UActor>(Self), BaseClass, &Actor,
		Start1, Stop1, Start2, Stop2, ExtentRadius,
		&HitLoc, &HitNorm, &LowJointMask, &HighJointMask);
}

void NRuneActor::DetachActorFromJoint(UObject* Self, int j, UObject*& ReturnValue)
{
	UActor* selfActor = UObject::Cast<UActor>(Self);
	ReturnValue = nullptr;

	UActor* actor = selfActor->TakeJointAttachment(j);
	if (actor)
	{
		ReturnValue = actor;
		actor->SetBase(nullptr, true);
	}
}

void NRuneActor::ActorAttachedTo(UObject* Self, int j, UObject*& ReturnValue)
{
	ReturnValue = UObject::Cast<UActor>(Self)->JointAttachment(j);
}
