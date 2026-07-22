#include "WebXRTwoHandWeapon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace WebXRTwoHandWeapon
{
	namespace
	{
		constexpr float Epsilon = 0.00001f;
		constexpr float MetadataScaleTolerance = 0.0001f;

		bool Equals(const char* left, const char* right)
		{
			if (!left || !right)
				return false;
			while (*left && *right)
			{
				char a = *left++;
				char b = *right++;
				if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
				if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
				if (a != b)
					return false;
			}
			return *left == *right;
		}

		bool Finite(float value) { return std::isfinite(value); }
		bool Finite(const Vec3& value) { return Finite(value.X) && Finite(value.Y) && Finite(value.Z); }
		Vec3 Add(const Vec3& a, const Vec3& b) { return { a.X + b.X, a.Y + b.Y, a.Z + b.Z }; }
		Vec3 Sub(const Vec3& a, const Vec3& b) { return { a.X - b.X, a.Y - b.Y, a.Z - b.Z }; }
		Vec3 Mul(const Vec3& a, float scale) { return { a.X * scale, a.Y * scale, a.Z * scale }; }
		float Dot(const Vec3& a, const Vec3& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
		Vec3 Cross(const Vec3& a, const Vec3& b)
		{
			return { a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z,
				a.X * b.Y - a.Y * b.X };
		}
		float LengthSquared(const Vec3& value) { return Dot(value, value); }
		float Length(const Vec3& value) { return std::sqrt(LengthSquared(value)); }
		bool Normalize(const Vec3& value, Vec3& result)
		{
			const float lengthSquared = LengthSquared(value);
			if (!Finite(lengthSquared) || lengthSquared <= Epsilon * Epsilon)
				return false;
			result = Mul(value, 1.0f / std::sqrt(lengthSquared));
			return Finite(result);
		}

		float Clamp01(float value) { return std::max(0.0f, std::min(1.0f, value)); }
		float SmoothStep01(float value)
		{
			const float x = Clamp01(value);
			return x * x * (3.0f - 2.0f * x);
		}

		bool ValidBasis(const Basis& basis)
		{
			return Finite(basis.Forward) && Finite(basis.Right) && Finite(basis.Up);
		}

		bool Orthonormalize(const Basis& input, Basis& output)
		{
			if (!ValidBasis(input) || !Normalize(input.Forward, output.Forward))
				return false;
			Vec3 right = Sub(input.Right, Mul(output.Forward, Dot(output.Forward, input.Right)));
			if (!Normalize(right, output.Right))
				return false;
			if (!Normalize(Cross(output.Forward, output.Right), output.Up))
				return false;
			if (Dot(output.Up, input.Up) < 0.0f)
			{
				output.Right = Mul(output.Right, -1.0f);
				output.Up = Mul(output.Up, -1.0f);
			}
			return true;
		}

		float BasisError(const Basis& basis)
		{
			if (!ValidBasis(basis))
				return std::numeric_limits<float>::infinity();
			float result = std::fabs(Length(basis.Forward) - 1.0f) +
				std::fabs(Length(basis.Right) - 1.0f) + std::fabs(Length(basis.Up) - 1.0f);
			result += std::fabs(Dot(basis.Forward, basis.Right));
			result += std::fabs(Dot(basis.Forward, basis.Up));
			result += std::fabs(Dot(basis.Right, basis.Up));
			result += Length(Sub(Cross(basis.Forward, basis.Right), basis.Up));
			return result;
		}

		bool BuildTwoHandBasis(const Vec3& mainPosition, const Vec3& offPosition,
			const Basis& mainBasis, Basis& result)
		{
			if (!Finite(mainPosition) || !Finite(offPosition) || !ValidBasis(mainBasis) ||
				!Normalize(Sub(offPosition, mainPosition), result.Forward))
				return false;

			Vec3 projectedUp = Sub(mainBasis.Up,
				Mul(result.Forward, Dot(result.Forward, mainBasis.Up)));
			if (Normalize(projectedUp, result.Up))
			{
				if (!Normalize(Cross(result.Up, result.Forward), result.Right))
					return false;
				result.Up = Cross(result.Forward, result.Right);
				return Finite(result.Up);
			}

			Vec3 projectedRight = Sub(mainBasis.Right,
				Mul(result.Forward, Dot(result.Forward, mainBasis.Right)));
			if (!Normalize(projectedRight, result.Right))
				return false;
			if (!Normalize(Cross(result.Forward, result.Right), result.Up))
				return false;
			return true;
		}

		struct Quaternion
		{
			float X = 0.0f;
			float Y = 0.0f;
			float Z = 0.0f;
			float W = 1.0f;
		};

		Quaternion NormalizeQuaternion(Quaternion value)
		{
			const float length = std::sqrt(value.X * value.X + value.Y * value.Y +
				value.Z * value.Z + value.W * value.W);
			if (!Finite(length) || length <= Epsilon)
				return {};
			value.X /= length;
			value.Y /= length;
			value.Z /= length;
			value.W /= length;
			return value;
		}

		Quaternion QuaternionFromBasis(const Basis& basis)
		{
			// Matrix columns are the forward/right/up axes.
			const float m00 = basis.Forward.X, m01 = basis.Right.X, m02 = basis.Up.X;
			const float m10 = basis.Forward.Y, m11 = basis.Right.Y, m12 = basis.Up.Y;
			const float m20 = basis.Forward.Z, m21 = basis.Right.Z, m22 = basis.Up.Z;
			Quaternion q;
			const float trace = m00 + m11 + m22;
			if (trace > 0.0f)
			{
				const float s = std::sqrt(trace + 1.0f) * 2.0f;
				q.W = 0.25f * s;
				q.X = (m21 - m12) / s;
				q.Y = (m02 - m20) / s;
				q.Z = (m10 - m01) / s;
			}
			else if (m00 > m11 && m00 > m22)
			{
				const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
				q.W = (m21 - m12) / s;
				q.X = 0.25f * s;
				q.Y = (m01 + m10) / s;
				q.Z = (m02 + m20) / s;
			}
			else if (m11 > m22)
			{
				const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
				q.W = (m02 - m20) / s;
				q.X = (m01 + m10) / s;
				q.Y = 0.25f * s;
				q.Z = (m12 + m21) / s;
			}
			else
			{
				const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
				q.W = (m10 - m01) / s;
				q.X = (m02 + m20) / s;
				q.Y = (m12 + m21) / s;
				q.Z = 0.25f * s;
			}
			return NormalizeQuaternion(q);
		}

		Vec3 Rotate(const Quaternion& q, const Vec3& value)
		{
			const Vec3 u{ q.X, q.Y, q.Z };
			return Add(Add(Mul(u, 2.0f * Dot(u, value)),
				Mul(value, q.W * q.W - Dot(u, u))), Mul(Cross(u, value), 2.0f * q.W));
		}

		Basis BasisFromQuaternion(const Quaternion& q)
		{
			return { Rotate(q, { 1.0f, 0.0f, 0.0f }),
				Rotate(q, { 0.0f, 1.0f, 0.0f }), Rotate(q, { 0.0f, 0.0f, 1.0f }) };
		}

		Basis BlendShortestArc(const Basis& from, const Basis& to, float weight)
		{
			Quaternion a = QuaternionFromBasis(from);
			Quaternion b = QuaternionFromBasis(to);
			float cosine = a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
			if (cosine < 0.0f)
			{
				b.X = -b.X; b.Y = -b.Y; b.Z = -b.Z; b.W = -b.W;
				cosine = -cosine;
			}
			const float t = Clamp01(weight);
			Quaternion result;
			if (cosine > 0.9995f)
			{
				result = { a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t,
					a.Z + (b.Z - a.Z) * t, a.W + (b.W - a.W) * t };
			}
			else
			{
				const float theta = std::acos(std::max(-1.0f, std::min(1.0f, cosine)));
				const float sine = std::sin(theta);
				const float wa = std::sin((1.0f - t) * theta) / sine;
				const float wb = std::sin(t * theta) / sine;
				result = { a.X * wa + b.X * wb, a.Y * wa + b.Y * wb,
					a.Z * wa + b.Z * wb, a.W * wa + b.W * wb };
			}
			Basis basis = BasisFromQuaternion(NormalizeQuaternion(result));
			Basis normalized;
			return Orthonormalize(basis, normalized) ? normalized : from;
		}

		bool ValidMetadata(const MetadataRow& row)
		{
			return row.SchemaVersion == MetadataSchemaVersion && row.PackageName && *row.PackageName &&
				row.ClassName && *row.ClassName && row.PackageHash && *row.PackageHash &&
				row.ControllerProfile && *row.ControllerProfile && row.Headset && *row.Headset &&
				row.Runtime && *row.Runtime && row.TwoHandEligible && Finite(row.ForegripLocalUU) &&
				Finite(row.GrabSqueeze) && Finite(row.ReleaseSqueeze) &&
				row.ReleaseSqueeze >= 0.0f && row.GrabSqueeze <= 1.0f &&
				row.ReleaseSqueeze < row.GrabSqueeze && Finite(row.GrabRadiusMeters) &&
				Finite(row.ReleaseRadiusMeters) && row.GrabRadiusMeters >= 0.03f &&
				row.GrabRadiusMeters <= 0.30f && row.ReleaseRadiusMeters > row.GrabRadiusMeters &&
				row.ReleaseRadiusMeters <= 0.50f &&
				std::fabs(row.ForegripLocalUU.X) <= 1000.0f &&
				std::fabs(row.ForegripLocalUU.Y) <= 1000.0f &&
				std::fabs(row.ForegripLocalUU.Z) <= 1000.0f && row.DominantHand >= 1 &&
				row.DominantHand <= 2 && Finite(row.WorldUnitsPerMeter) &&
				row.WorldUnitsPerMeter >= 1.0f && row.WorldUnitsPerMeter <= 1000.0f;
		}

		void Reject(State& state, Rejection reason)
		{
			state.Stats.LastRejection = reason;
			state.Stats.RejectionCount++;
		}

		bool SameIdentity(const State& state, const FrameInput& input)
		{
			return state.SessionGeneration == input.SessionGeneration &&
				state.ReferenceSpaceGeneration == input.ReferenceSpaceGeneration &&
				state.DominantSourceId == input.DominantSourceId &&
				state.OffSourceId == input.OffSourceId && state.DominantHand == input.DominantHand &&
				state.PawnIdentity == input.PawnIdentity && state.WeaponIdentity == input.WeaponIdentity &&
				state.MetadataIdentity == reinterpret_cast<uintptr_t>(input.Metadata);
		}

		ResetReason IdentityResetReason(const State& state, const FrameInput& input)
		{
			if (state.SessionGeneration != input.SessionGeneration) return ResetReason::SessionChanged;
			if (state.ReferenceSpaceGeneration != input.ReferenceSpaceGeneration) return ResetReason::ReferenceSpaceReset;
			if (state.DominantSourceId != input.DominantSourceId || state.OffSourceId != input.OffSourceId) return ResetReason::SourceChanged;
			if (state.DominantHand != input.DominantHand) return ResetReason::DominantHandChanged;
			if (state.PawnIdentity != input.PawnIdentity) return ResetReason::PawnChanged;
			if (state.WeaponIdentity != input.WeaponIdentity || state.MetadataIdentity != reinterpret_cast<uintptr_t>(input.Metadata)) return ResetReason::WeaponChanged;
			return ResetReason::None;
		}

		void BindIdentity(State& state, const FrameInput& input)
		{
			state.HasIdentity = true;
			state.LastFrameGeneration = input.FrameGeneration;
			state.SessionGeneration = input.SessionGeneration;
			state.ReferenceSpaceGeneration = input.ReferenceSpaceGeneration;
			state.DominantSourceId = input.DominantSourceId;
			state.OffSourceId = input.OffSourceId;
			state.DominantHand = input.DominantHand;
			state.PawnIdentity = input.PawnIdentity;
			state.WeaponIdentity = input.WeaponIdentity;
			state.MetadataIdentity = reinterpret_cast<uintptr_t>(input.Metadata);
		}

		bool Nearly(float actual, float expected, float tolerance = 0.0005f)
		{
			return std::fabs(actual - expected) <= tolerance;
		}
	}

	MetadataLookup FindQualifiedMetadata(const MetadataRow* rows, size_t rowCount,
		const MetadataQuery& query)
	{
		if (!rows)
			rowCount = 0;
		const MetadataRow* match = nullptr;
		Rejection firstMismatch = Rejection::MissingMetadata;
		for (size_t index = 0; index < rowCount; index++)
		{
			const MetadataRow& row = rows[index];
			if (!Equals(row.PackageName, query.PackageName) || !Equals(row.ClassName, query.ClassName))
				continue;
			if (!ValidMetadata(row))
				return { nullptr, Rejection::InvalidMetadata };
			Rejection mismatch = Rejection::None;
			if (!Equals(row.PackageHash, query.PackageHash))
				mismatch = Rejection::WrongPackageHash;
			else if (!Equals(row.ControllerProfile, query.ControllerProfile))
				mismatch = Rejection::WrongControllerProfile;
			else if (!Equals(row.Headset, query.Headset))
				mismatch = Rejection::WrongHeadset;
			else if (!Equals(row.Runtime, query.Runtime))
				mismatch = Rejection::WrongRuntime;
			else if (row.DominantHand != query.DominantHand)
				mismatch = Rejection::WrongDominantHand;
			else if (!Finite(query.WorldUnitsPerMeter) || query.WorldUnitsPerMeter <= 0.0f ||
				std::fabs(row.WorldUnitsPerMeter - query.WorldUnitsPerMeter) > MetadataScaleTolerance)
				mismatch = Rejection::WrongWorldScale;
			if (mismatch != Rejection::None)
			{
				if (firstMismatch == Rejection::MissingMetadata)
					firstMismatch = mismatch;
				continue;
			}
			if (match)
				return { nullptr, Rejection::DuplicateMetadata };
			match = &row;
		}
		return match ? MetadataLookup{ match, Rejection::None } :
			MetadataLookup{ nullptr, firstMismatch };
	}

	void Reset(State& state, ResetReason reason, bool requireFreshCondition)
	{
		if (state.Active)
			state.Stats.ReleaseCount++;
		state.Active = false;
		state.GrabBlend = 0.0f;
		state.FreshConditionRequired = requireFreshCondition;
		state.HasIdentity = false;
		state.LastFrameGeneration = 0;
		state.SessionGeneration = 0;
		state.ReferenceSpaceGeneration = 0;
		state.DominantSourceId = 0;
		state.OffSourceId = 0;
		state.DominantHand = 0;
		state.PawnIdentity = 0;
		state.WeaponIdentity = 0;
		state.MetadataIdentity = 0;
		state.Stats.Active = false;
		state.Stats.Eligible = false;
		state.Stats.GrabBlend = 0.0f;
		state.Stats.BaselineWeight = 0.0f;
		state.Stats.EffectiveWeight = 0.0f;
		state.Stats.FreshConditionRequired = requireFreshCondition;
		state.Stats.LastReset = reason;
		state.Stats.ResetCount++;
	}

	FrameOutput Advance(State& state, const FrameInput& input)
	{
		FrameOutput output;
		output.PresentationBasis = input.OneHandBasis;
		output.BallisticForward = input.OneHandBasis.Forward;
		output.MainVisualOriginUU = input.MainGripPositionUU;
		state.Stats.Enabled = input.Enabled;
		state.Stats.Eligible = false;
		state.Stats.OneHandBasis = input.OneHandBasis;
		state.Stats.OffGripSqueeze = input.OffGripSqueeze;
		state.Stats.LastFrameGeneration = input.FrameGeneration;
		state.Stats.SessionGeneration = input.SessionGeneration;
		state.Stats.ReferenceSpaceGeneration = input.ReferenceSpaceGeneration;
		state.Stats.DominantSourceId = input.DominantSourceId;
		state.Stats.OffSourceId = input.OffSourceId;
		state.Stats.DominantHand = input.DominantHand;
		state.Stats.LastRejection = Rejection::None;

		auto fail = [&](ResetReason reset, Rejection rejection)
		{
			Reset(state, reset);
			Reject(state, rejection);
			output.Why = rejection;
			state.Stats.BlendedBasis = output.PresentationBasis;
			state.Stats.OrthonormalError = BasisError(output.PresentationBasis);
			return output;
		};

		if (!input.Enabled)
			return fail(ResetReason::Disabled, Rejection::Disabled);
		if (!input.LocalCurrentWeapon)
		{
			const Rejection reason = input.OwnershipRejection == Rejection::RemotePawn ||
				input.OwnershipRejection == Rejection::BotPawn ||
				input.OwnershipRejection == Rejection::StaleWeapon ||
				input.OwnershipRejection == Rejection::WrongOwner ?
				input.OwnershipRejection : Rejection::NotLocalCurrentWeapon;
			return fail(ResetReason::Ineligible, reason);
		}
		if (!input.PawnAlive)
			return fail(ResetReason::PawnDead, Rejection::PawnDead);
		if (input.MenuOwned)
			return fail(ResetReason::MenuOwned, Rejection::MenuOwned);
		if (!input.MainGripTracked || !input.OffGripTracked)
			return fail(ResetReason::TrackingLost, Rejection::TrackingLost);
		if (!input.Metadata)
			return fail(ResetReason::Ineligible,
				input.MetadataRejection == Rejection::None ? Rejection::MissingMetadata :
				input.MetadataRejection);
		if (!ValidMetadata(*input.Metadata))
			return fail(ResetReason::Ineligible, Rejection::InvalidMetadata);
		if (input.Metadata->DominantHand != input.DominantHand)
			return fail(ResetReason::Ineligible, Rejection::WrongDominantHand);
		if (!Finite(input.WorldUnitsPerMeter) || input.WorldUnitsPerMeter <= 0.0f ||
			std::fabs(input.Metadata->WorldUnitsPerMeter - input.WorldUnitsPerMeter) >
			MetadataScaleTolerance)
			return fail(ResetReason::Ineligible, Rejection::WrongWorldScale);
		if (!Finite(input.DeltaSeconds) || !Finite(input.WorldUnitsPerMeter) ||
			input.WorldUnitsPerMeter <= 0.0f || !Finite(input.OffGripSqueeze) ||
			!Finite(input.MainGripPositionUU) || !Finite(input.OffGripPositionUU) ||
			!ValidBasis(input.MainGripBasis) || !ValidBasis(input.OneHandBasis))
			return fail(ResetReason::NonFiniteInput, Rejection::NonFiniteInput);

		Basis oneHand;
		if (!Orthonormalize(input.OneHandBasis, oneHand))
			return fail(ResetReason::NonFiniteInput, Rejection::DegenerateBasis);
		output.PresentationBasis = oneHand;
		output.BallisticForward = oneHand.Forward;
		state.Stats.OneHandBasis = oneHand;

		if (state.HasIdentity && input.FrameGeneration <= state.LastFrameGeneration)
			return fail(ResetReason::StaleFrame, Rejection::StaleFrame);
		if (!state.HasIdentity)
			BindIdentity(state, input);
		else if (!SameIdentity(state, input))
		{
			const ResetReason reason = IdentityResetReason(state, input);
			Reset(state, reason, true);
			BindIdentity(state, input);
		}
		else
			state.LastFrameGeneration = input.FrameGeneration;

		const MetadataRow& metadata = *input.Metadata;
		output.Eligible = true;
		state.Stats.Eligible = true;
		const Vec3& origin = input.MainGripPositionUU;
		output.ForegripWorldUU = Add(origin, Add(Mul(oneHand.Forward, metadata.ForegripLocalUU.X),
			Add(Mul(oneHand.Right, metadata.ForegripLocalUU.Y), Mul(oneHand.Up, metadata.ForegripLocalUU.Z))));
		const float baselineUU = Length(Sub(input.OffGripPositionUU, origin));
		const float foregripDistanceUU = Length(Sub(input.OffGripPositionUU, output.ForegripWorldUU));
		output.GripDistanceMeters = baselineUU / input.WorldUnitsPerMeter;
		output.ForegripDistanceMeters = foregripDistanceUU / input.WorldUnitsPerMeter;
		if (!Finite(output.GripDistanceMeters) || !Finite(output.ForegripDistanceMeters))
			return fail(ResetReason::NonFiniteInput, Rejection::NonFiniteInput);

		const bool neutral = input.OffGripSqueeze <= metadata.ReleaseSqueeze ||
			output.ForegripDistanceMeters >= metadata.GrabRadiusMeters;
		if (state.FreshConditionRequired && neutral)
			state.FreshConditionRequired = false;

		if (state.Active)
		{
			if (input.OffGripSqueeze < metadata.ReleaseSqueeze ||
				output.ForegripDistanceMeters > metadata.ReleaseRadiusMeters)
			{
				state.Active = false;
				state.Stats.ReleaseCount++;
			}
		}
		else if (!state.FreshConditionRequired &&
			input.OffGripSqueeze > metadata.GrabSqueeze &&
			output.ForegripDistanceMeters < metadata.GrabRadiusMeters)
		{
			state.Active = true;
			state.Stats.GrabCount++;
		}

		if (input.DeltaSeconds > 0.0f)
		{
			const float step = input.DeltaSeconds / DefaultBlendSeconds;
			state.GrabBlend = state.Active ? std::min(1.0f, state.GrabBlend + step) :
				std::max(0.0f, state.GrabBlend - step);
		}

		Basis twoHand;
		if (!BuildTwoHandBasis(origin, input.OffGripPositionUU, input.MainGripBasis, twoHand))
		{
			output.Why = Rejection::DegenerateBasis;
			Reject(state, output.Why);
			state.Stats.BasisFallbackCount++;
			state.GrabBlend = 0.0f;
			state.Active = false;
			state.FreshConditionRequired = true;
		}
		else
		{
			output.BaselineWeight = SmoothStep01(
				output.GripDistanceMeters / DefaultMinimumBaselineMeters);
			output.EffectiveWeight = state.GrabBlend * output.BaselineWeight;
			output.PresentationBasis = BlendShortestArc(oneHand, twoHand, output.EffectiveWeight);
			output.BallisticForward = output.PresentationBasis.Forward;
			state.Stats.TwoHandBasis = twoHand;
		}
		if (output.EffectiveWeight == 0.0f)
		{
			// This is intentionally an exact structural passthrough, not merely an
			// equivalent normalized orientation.
			output.PresentationBasis = input.OneHandBasis;
			output.BallisticForward = input.OneHandBasis.Forward;
		}

		output.Active = state.Active;
		output.GrabBlend = state.GrabBlend;
		state.Stats.LastFrameGeneration = state.LastFrameGeneration;
		state.Stats.SessionGeneration = state.SessionGeneration;
		state.Stats.ReferenceSpaceGeneration = state.ReferenceSpaceGeneration;
		state.Stats.DominantSourceId = state.DominantSourceId;
		state.Stats.OffSourceId = state.OffSourceId;
		state.Stats.DominantHand = state.DominantHand;
		state.Stats.Active = state.Active;
		state.Stats.FreshConditionRequired = state.FreshConditionRequired;
		state.Stats.GripDistanceMeters = output.GripDistanceMeters;
		state.Stats.ForegripDistanceMeters = output.ForegripDistanceMeters;
		state.Stats.GrabBlend = output.GrabBlend;
		state.Stats.BaselineWeight = output.BaselineWeight;
		state.Stats.EffectiveWeight = output.EffectiveWeight;
		state.Stats.BlendedBasis = output.PresentationBasis;
		state.Stats.OrthonormalError = BasisError(output.PresentationBasis);
		if (output.Why == Rejection::None && !state.Active && state.GrabBlend == 0.0f)
			output.Why = state.FreshConditionRequired ? Rejection::FreshConditionRequired :
				Rejection::OutsideGrabCondition;
		state.Stats.LastRejection = output.Why;
		return output;
	}

	bool RunSelfTest()
	{
		const MetadataRow valid{ MetadataSchemaVersion, "Botpack", "FixtureRifle", true,
			{ 20.0f, 0.0f, 0.0f }, DefaultGrabSqueeze, DefaultReleaseSqueeze,
			DefaultGrabRadiusMeters, DefaultReleaseRadiusMeters, "0123456789abcdef",
			"oculus-touch-v3", 2, 100.0f, "Quest 3", "VDXR 1.0.10" };
		const MetadataQuery query{ "Botpack", "FixtureRifle", "0123456789abcdef",
			"oculus-touch-v3", "Quest 3", "VDXR 1.0.10", 2, 100.0f };
		if (FindQualifiedMetadata(nullptr, 0, query).Why != Rejection::MissingMetadata)
			return false;
		const MetadataRow collisions[] = { valid, valid };
		if (FindQualifiedMetadata(collisions, 2, query).Why != Rejection::DuplicateMetadata)
			return false;
		MetadataRow leftHanded = valid;
		leftHanded.DominantHand = 1;
		const MetadataRow handedRows[] = { valid, leftHanded };
		MetadataQuery leftQuery = query;
		leftQuery.DominantHand = 1;
		if (FindQualifiedMetadata(handedRows, 2, query).Row != &handedRows[0] ||
			FindQualifiedMetadata(handedRows, 2, leftQuery).Row != &handedRows[1])
			return false;
		MetadataRow wrongPackage = valid;
		wrongPackage.PackageName = "ExampleMod";
		if (FindQualifiedMetadata(&wrongPackage, 1, query).Why != Rejection::MissingMetadata)
			return false;
		MetadataRow invalid = valid;
		invalid.ForegripLocalUU.X = std::numeric_limits<float>::quiet_NaN();
		if (FindQualifiedMetadata(&invalid, 1, query).Why != Rejection::InvalidMetadata)
			return false;
		MetadataQuery mismatch = query;
		mismatch.PackageHash = "wrong";
		if (FindQualifiedMetadata(&valid, 1, mismatch).Why != Rejection::WrongPackageHash)
			return false;
		mismatch = query; mismatch.ControllerProfile = "generic-trigger-squeeze-thumbstick";
		if (FindQualifiedMetadata(&valid, 1, mismatch).Why != Rejection::WrongControllerProfile)
			return false;
		mismatch = query; mismatch.Headset = "Other headset";
		if (FindQualifiedMetadata(&valid, 1, mismatch).Why != Rejection::WrongHeadset)
			return false;
		mismatch = query; mismatch.Runtime = "Other runtime";
		if (FindQualifiedMetadata(&valid, 1, mismatch).Why != Rejection::WrongRuntime)
			return false;
		mismatch = query; mismatch.DominantHand = 1;
		if (FindQualifiedMetadata(&valid, 1, mismatch).Why != Rejection::WrongDominantHand)
			return false;
		mismatch = query; mismatch.WorldUnitsPerMeter = 99.0f;
		if (FindQualifiedMetadata(&valid, 1, mismatch).Why != Rejection::WrongWorldScale ||
			FindQualifiedMetadata(&valid, 1, query).Row != &valid)
			return false;
		for (float invalidScale : { 0.0f, 1001.0f,
			std::numeric_limits<float>::infinity() })
		{
			mismatch = query;
			mismatch.WorldUnitsPerMeter = invalidScale;
			if (FindQualifiedMetadata(&valid, 1, mismatch).Why != Rejection::WrongWorldScale)
				return false;
		}
		for (float invalidScale : { 0.0f, 1001.0f })
		{
			MetadataRow invalidScaleRow = valid;
			invalidScaleRow.WorldUnitsPerMeter = invalidScale;
			if (FindQualifiedMetadata(&invalidScaleRow, 1, query).Why != Rejection::InvalidMetadata)
				return false;
		}

		auto makeInput = [&](uint64_t generation)
		{
			FrameInput input;
			input.FrameGeneration = generation;
			input.SessionGeneration = 7;
			input.ReferenceSpaceGeneration = 11;
			input.DominantSourceId = 17;
			input.OffSourceId = 9;
			input.DominantHand = 2;
			input.PawnIdentity = 0x100;
			input.WeaponIdentity = 0x200;
			input.Metadata = &valid;
			input.DeltaSeconds = 1.0f / 90.0f;
			input.WorldUnitsPerMeter = 100.0f;
			input.MainGripPositionUU = { 10.0f, 0.0f, 30.0f };
			input.OffGripPositionUU = { 30.0f, 0.0f, 30.0f };
			input.OffGripSqueeze = 0.0f;
			return input;
		};

		State state;
		FrameInput input = makeInput(1);
		FrameOutput output = Advance(state, input);
		if (!output.Eligible || output.Active || output.MainVisualOriginUU.X != 10.0f ||
			output.ForegripWorldUU.X != 30.0f || output.ForegripDistanceMeters != 0.0f ||
			output.PresentationBasis.Forward.X != 1.0f)
			return false;

		// Strict analog and spatial acquisition boundaries.
		input.FrameGeneration++;
		input.OffGripSqueeze = DefaultGrabSqueeze;
		if (Advance(state, input).Active)
			return false;
		input.FrameGeneration++;
		input.OffGripSqueeze = std::nextafter(DefaultGrabSqueeze, 1.0f);
		input.OffGripPositionUU.Y = DefaultGrabRadiusMeters * input.WorldUnitsPerMeter;
		if (Advance(state, input).Active)
			return false;
		input.FrameGeneration++;
		input.OffGripPositionUU.Y = std::nextafter(
			DefaultGrabRadiusMeters * input.WorldUnitsPerMeter, 0.0f);
		if (!Advance(state, input).Active)
			return false;

		// Release thresholds are also strict; equality retains the grab.
		input.FrameGeneration++;
		input.OffGripSqueeze = DefaultReleaseSqueeze;
		input.OffGripPositionUU = { 30.0f,
			DefaultReleaseRadiusMeters * input.WorldUnitsPerMeter, 30.0f };
		if (!Advance(state, input).Active)
			return false;
		input.FrameGeneration++;
		input.OffGripSqueeze = std::nextafter(DefaultReleaseSqueeze, 0.0f);
		if (Advance(state, input).Active)
			return false;

		// Every common headset rate converges in approximately 100 ms independent of frame rate.
		for (float hz : { 60.0f, 72.0f, 80.0f, 90.0f })
		{
			State rateState;
			FrameInput rate = makeInput(1);
			rate.DeltaSeconds = 1.0f / hz;
			rate.OffGripSqueeze = 1.0f;
			FrameOutput rateOutput;
			for (uint32_t frame = 0; frame < static_cast<uint32_t>(std::ceil(hz * 0.10f)); frame++)
			{
				rate.FrameGeneration++;
				rateOutput = Advance(rateState, rate);
			}
			if (!Nearly(rateOutput.GrabBlend, 1.0f, 0.0001f))
				return false;
		}

		// Invalid delta cannot advance a valid grab, while a non-finite delta resets.
		State deltaState;
		FrameInput delta = makeInput(1);
		delta.OffGripSqueeze = 1.0f;
		delta.DeltaSeconds = -1.0f;
		if (!Advance(deltaState, delta).Active || deltaState.GrabBlend != 0.0f)
			return false;
		delta.FrameGeneration++;
		delta.DeltaSeconds = std::numeric_limits<float>::quiet_NaN();
		if (Advance(deltaState, delta).Why != Rejection::NonFiniteInput || deltaState.Active)
			return false;

		// A source/reset/identity change clears state and requires a neutral frame.
		State resetState;
		FrameInput resetInput = makeInput(1);
		resetInput.OffGripSqueeze = 1.0f;
		Advance(resetState, resetInput);
		resetInput.FrameGeneration++;
		resetInput.OffSourceId++;
		if (Advance(resetState, resetInput).Active || !resetState.FreshConditionRequired ||
			resetState.Stats.LastReset != ResetReason::SourceChanged)
			return false;
		resetInput.FrameGeneration++;
		resetInput.OffGripSqueeze = 0.0f;
		Advance(resetState, resetInput);
		resetInput.FrameGeneration++;
		resetInput.OffGripSqueeze = 1.0f;
		if (!Advance(resetState, resetInput).Active)
			return false;
		State staleState;
		FrameInput staleInput = makeInput(10);
		staleInput.OffGripSqueeze = 1.0f;
		Advance(staleState, staleInput);
		if (Advance(staleState, staleInput).Why != Rejection::StaleFrame || staleState.Active ||
			staleState.Stats.LastReset != ResetReason::StaleFrame)
			return false;
		const std::array<ResetReason, 7> expectedResets = { ResetReason::SessionChanged,
			ResetReason::ReferenceSpaceReset, ResetReason::DominantHandChanged,
			ResetReason::PawnChanged, ResetReason::WeaponChanged, ResetReason::MenuOwned,
			ResetReason::TrackingLost };
		for (ResetReason expected : expectedResets)
		{
			MetadataRow alternate = valid;
			State test;
			FrameInput changed = makeInput(1);
			changed.OffGripSqueeze = 1.0f;
			Advance(test, changed);
			changed.FrameGeneration++;
			switch (expected)
			{
			case ResetReason::SessionChanged: changed.SessionGeneration++; break;
			case ResetReason::ReferenceSpaceReset: changed.ReferenceSpaceGeneration++; break;
			case ResetReason::DominantHandChanged:
				changed.DominantHand = 1;
				alternate.DominantHand = 1;
				changed.Metadata = &alternate;
				break;
			case ResetReason::PawnChanged: changed.PawnIdentity++; break;
			case ResetReason::WeaponChanged: changed.WeaponIdentity++; break;
			case ResetReason::MenuOwned: changed.MenuOwned = true; break;
			case ResetReason::TrackingLost: changed.OffGripTracked = false; break;
			default: break;
			}
			Advance(test, changed);
			if (test.Active || test.GrabBlend != 0.0f || test.Stats.LastReset != expected)
				return false;
		}

		// Short baselines fade smoothly; zero weight is exact one-hand passthrough.
		State baselineState;
		FrameInput baseline = makeInput(1);
		baseline.OffGripSqueeze = 1.0f;
		baseline.DeltaSeconds = DefaultBlendSeconds;
		baseline.OffGripPositionUU = { 10.0f, 0.0f, 30.0f };
		MetadataRow nearRow = valid;
		nearRow.ForegripLocalUU = { 0.0f, 0.0f, 0.0f };
		baseline.Metadata = &nearRow;
		FrameOutput nearOutput = Advance(baselineState, baseline);
		if (nearOutput.EffectiveWeight != 0.0f ||
			nearOutput.PresentationBasis.Forward.X != baseline.OneHandBasis.Forward.X)
			return false;
		baseline.FrameGeneration++;
		baseline.OffGripPositionUU = { 25.0f, 0.0f, 30.0f };
		nearRow.ForegripLocalUU = { 15.0f, 0.0f, 0.0f };
		nearOutput = Advance(baselineState, baseline);
		if (!Nearly(nearOutput.BaselineWeight, 1.0f) ||
			baselineState.Stats.OrthonormalError > 0.001f)
			return false;

		// Full roll is preserved, the blended basis stays handed/orthonormal, and
		// the shortest arc crosses the +/-180 degree boundary instead of spinning.
		State rollState;
		FrameInput roll = makeInput(1);
		roll.OffGripSqueeze = 1.0f;
		roll.DeltaSeconds = 0.05f;
		roll.OneHandBasis = { { -0.9998477f, 0.0174524f, 0.0f },
			{ -0.0174524f, -0.9998477f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
		roll.MainGripBasis = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f },
			{ 0.0f, 1.0f, 0.0f } };
		roll.OffGripPositionUU = { -90.0f, -18.2542f, 30.0f };
		MetadataRow rollRow = valid;
		// This local point lies next to the off hand while its raw hand baseline
		// crosses the +179/-170 degree yaw boundary.
		rollRow.ForegripLocalUU = { 100.0f, 20.0f, 0.0f };
		roll.Metadata = &rollRow;
		FrameOutput rollOutput = Advance(rollState, roll);
		if (rollOutput.EffectiveWeight <= 0.0f || BasisError(rollOutput.PresentationBasis) > 0.001f ||
			Dot(Cross(rollOutput.PresentationBasis.Forward, rollOutput.PresentationBasis.Right),
				rollOutput.PresentationBasis.Up) < 0.999f || rollOutput.PresentationBasis.Forward.X > -0.9f)
			return false;

		// When forward is parallel to the grip up axis, main-right supplies roll.
		State fallbackState;
		FrameInput fallback = makeInput(1);
		fallback.DeltaSeconds = DefaultBlendSeconds;
		fallback.OffGripSqueeze = 1.0f;
		fallback.OffGripPositionUU = { 10.0f, 0.0f, 50.0f };
		MetadataRow fallbackRow = valid;
		fallbackRow.ForegripLocalUU = { 0.0f, 0.0f, 20.0f };
		fallback.Metadata = &fallbackRow;
		FrameOutput fallbackOutput = Advance(fallbackState, fallback);
		if (!fallbackOutput.Active || fallbackOutput.EffectiveWeight <= 0.0f ||
			fallbackOutput.PresentationBasis.Forward.Z < 0.9f ||
			BasisError(fallbackOutput.PresentationBasis) > 0.001f)
			return false;
		State degenerateHintState;
		fallback.FrameGeneration++;
		fallback.MainGripBasis.Right = { 0.0f, 0.0f, 1.0f };
		fallback.MainGripBasis.Up = { 0.0f, 0.0f, 1.0f };
		if (Advance(degenerateHintState, fallback).Why != Rejection::DegenerateBasis ||
			degenerateHintState.Active)
			return false;

		// Disabled and ownership failures are synchronous one-hand fallbacks.
		for (Rejection expected : { Rejection::Disabled, Rejection::NotLocalCurrentWeapon,
			Rejection::RemotePawn, Rejection::BotPawn, Rejection::StaleWeapon,
			Rejection::WrongOwner, Rejection::PawnDead, Rejection::MissingMetadata })
		{
			State test;
			FrameInput rejected = makeInput(1);
			if (expected == Rejection::Disabled) rejected.Enabled = false;
			if (expected == Rejection::NotLocalCurrentWeapon || expected == Rejection::RemotePawn ||
				expected == Rejection::BotPawn || expected == Rejection::StaleWeapon ||
				expected == Rejection::WrongOwner)
			{
				rejected.LocalCurrentWeapon = false;
				rejected.OwnershipRejection = expected;
			}
			if (expected == Rejection::PawnDead) rejected.PawnAlive = false;
			if (expected == Rejection::MissingMetadata) rejected.Metadata = nullptr;
			const FrameOutput rejectedOutput = Advance(test, rejected);
			if (rejectedOutput.Why != expected || rejectedOutput.EffectiveWeight != 0.0f ||
				rejectedOutput.PresentationBasis.Forward.X != 1.0f)
				return false;
		}

		// Coincident hands and every non-finite vector fail closed.
		State invalidState;
		FrameInput coincident = makeInput(1);
		coincident.OffGripPositionUU = coincident.MainGripPositionUU;
		coincident.OffGripSqueeze = 1.0f;
		MetadataRow zeroRow = valid;
		zeroRow.ForegripLocalUU = {};
		coincident.Metadata = &zeroRow;
		if (Advance(invalidState, coincident).Why != Rejection::DegenerateBasis)
			return false;
		coincident.FrameGeneration++;
		coincident.MainGripPositionUU.X = std::numeric_limits<float>::infinity();
		if (Advance(invalidState, coincident).Why != Rejection::NonFiniteInput)
			return false;

		// Explicit exception-unwind API clears every transient field.
		Reset(invalidState, ResetReason::ExceptionUnwind);
		return !invalidState.Active && invalidState.GrabBlend == 0.0f &&
			invalidState.FreshConditionRequired &&
			invalidState.Stats.LastReset == ResetReason::ExceptionUnwind;
	}
}
