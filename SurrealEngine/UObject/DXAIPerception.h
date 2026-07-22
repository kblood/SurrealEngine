#pragma once

float ComputeDXAIHearing(float volume, float radius, float hearingThreshold, float deltaX, float deltaY, float deltaZ);
float ComputeDXAISight(float visibility, float lightVisibility, float collisionRadius, float collisionHeight, float distanceSquared, float minAngularSize, float visibilityThreshold);
float ComputeDXAIMotionVisibility(float lightVisibility, float speed, bool includeVelocity);
