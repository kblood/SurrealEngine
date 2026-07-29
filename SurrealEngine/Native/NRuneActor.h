#pragma once

#include "Packages/Core/UObject.h"

class NRuneActor
{
public:
	static void RegisterFunctions();

	static void AttachActorToJoint(UObject* Self, UObject* A, int j);
	static void JointNamed(UObject* Self, const NameString& jointname, int& ReturnValue);
	static void TraceTexture(UObject* Self, const vec3& TraceEnd, const vec3& TraceStart, int& Flags, vec3& ScrollDir, UObject*& ReturnValue);
	static void GetJointPos(UObject* Self, int joint, vec3& ReturnValue);
	static void GetJointRot(UObject* Self, int joint, Rotator& ReturnValue);
	static void GetJointName(UObject* Self, int joint, std::string& ReturnValue);
	static void ApplyJointForce(UObject* Self, int joint, const vec3& force);
	static void NumJoints(UObject* Self, int& ReturnValue);
	static void ResetAnimationCache(UObject* Self, const NameString& seq);
	static void SetDefaultPolygroups(UObject* Self);
	static void SetDefaultJointFlags(UObject* Self);
	static void SkeletonLook(UObject* Self, float DeltaTime);
	static void SetJointRot(UObject* Self, int joint, const Rotator& Rot);
	static void TurnJointTo(UObject* Self, int joint, const Rotator& Rot);
	static void ClosestJointTo(UObject* Self, const vec3& point, int& ReturnValue);
	static void FrameSweep(UObject* Self, int curframe, const vec3& weaponVector, vec3& lastB1, vec3& lastE1);
	static void SweepActors(UObject* Self, UObject* BaseClass, UObject*& Actor,
		const vec3& Start1, const vec3& Stop1, const vec3& Start2, const vec3& Stop2,
		float ExtentRadius, vec3& HitLoc, vec3& HitNorm, int& LowJointMask, int& HighJointMask);
	static void DetachActorFromJoint(UObject* Self, int j, UObject*& ReturnValue);
	static void ActorAttachedTo(UObject* Self, int j, UObject*& ReturnValue);
};
