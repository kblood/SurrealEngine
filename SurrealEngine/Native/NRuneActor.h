#pragma once

#include "UObject/UObject.h"

class NRuneActor
{
public:
	static void RegisterFunctions();

	static void AttachActorToJoint(UObject* Self, UObject* A, int j);
	static void JointNamed(UObject* Self, const NameString& jointname, int& ReturnValue);
	static void TraceTexture(UObject* Self, const vec3& TraceEnd, const vec3& TraceStart, int& Flags, vec3& ScrollDir, UObject*& ReturnValue);
	static void GetJointPos(UObject* Self, int joint, vec3& ReturnValue);
	static void ResetAnimationCache(UObject* Self, const NameString& seq);
	static void SetDefaultPolygroups(UObject* Self);
	static void SkeletonLook(UObject* Self, float DeltaTime);
};
