#pragma once

// Pure scalar approximations of the Deus Ex AI perception formulas. These
// functions deliberately have no actor, trace, property-offset, or world-state
// dependency so native wrappers can supply already-resolved inputs later.
float ComputeDXAIHearing(float volume, float radius, float hearingThreshold, float deltaX, float deltaY, float deltaZ) noexcept;
float ComputeDXAISight(float visibility, float lightVisibility, float collisionRadius, float collisionHeight, float distanceSquared, float minAngularSize, float visibilityThreshold) noexcept;
float ComputeDXAIMotionVisibility(float lightVisibility, float speed, bool includeVelocity) noexcept;
