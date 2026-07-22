#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace WebXR
{
	constexpr uint32_t InputABIVersion = 1;
	constexpr uint32_t MaxInputSources = 2;
	constexpr uint32_t InputAxisCount = 4;
	constexpr uint32_t InputButtonCount = 6;

	enum class InputError : int
	{
		None = 0,
		InvalidHeader = 1,
		InvalidSource = 2,
		DuplicateHand = 3
	};

	enum class InputHand : uint32_t
	{
		Left = 1,
		Right = 2
	};

	enum InputHeaderFlags : uint32_t
	{
		InputActionFocused = 1u << 0
	};

	enum InputSourceFlags : uint32_t
	{
		InputSourceConnected = 1u << 0,
		InputAimPoseValid = 1u << 1,
		InputGripPoseValid = 1u << 2
	};

	enum InputButton : uint32_t
	{
		InputTrigger = 0,
		InputSqueeze = 1,
		InputPrimary = 2,
		InputSecondary = 3,
		InputMenu = 4,
		InputStickClick = 5
	};

#pragma pack(push, 1)
	struct PackedInputHeader
	{
		uint32_t Version;
		uint32_t ByteSize;
		uint32_t SourceCount;
		uint32_t Flags;
		double Timestamp;
	};

	struct PackedInputSource
	{
		uint32_t Hand;
		uint32_t Flags;
		uint32_t PressedButtons;
		uint32_t TouchedButtons;
		float ButtonValues[InputButtonCount];
		float Axes[InputAxisCount];
		float AimPosition[3];
		float AimOrientation[4];
		float GripPosition[3];
		float GripOrientation[4];
	};
#pragma pack(pop)

	static_assert(sizeof(PackedInputHeader) == 24);
	static_assert(offsetof(PackedInputHeader, Timestamp) == 16);
	static_assert(sizeof(PackedInputSource) == 112);
	static_assert(offsetof(PackedInputSource, ButtonValues) == 16);
	static_assert(offsetof(PackedInputSource, AimPosition) == 56);
	static_assert(offsetof(PackedInputSource, GripPosition) == 84);

	struct DecodedInputSource
	{
		bool Connected = false;
		bool AimPoseValid = false;
		bool GripPoseValid = false;
		uint32_t PressedButtons = 0;
		uint32_t TouchedButtons = 0;
		std::array<float, InputButtonCount> ButtonValues = {};
		std::array<float, InputAxisCount> Axes = {};
		std::array<float, 3> AimPosition = {};
		std::array<float, 4> AimOrientation = {};
		std::array<float, 3> GripPosition = {};
		std::array<float, 4> GripOrientation = {};
	};

	struct DecodedInputSnapshot
	{
		double Timestamp = 0.0;
		bool ActionFocused = false;
		std::array<DecodedInputSource, MaxInputSources> Sources = {};
	};

	bool DecodeInputSnapshot(const void* inputData, uint32_t bufferBytes,
		DecodedInputSnapshot& result, InputError& error);
	void StoreInputSnapshot(const DecodedInputSnapshot& snapshot);
	DecodedInputSnapshot GetInputSnapshot();
	void ClearInputSnapshot(double timestamp = 0.0);
	void SetLastInputError(InputError error);
	InputError GetLastInputError();
}

extern "C"
{
	uint32_t Surreal_GetWebXRInputABIVersion();
	uint32_t Surreal_GetWebXRInputHeaderSize();
	uint32_t Surreal_GetWebXRInputSourceSize();
	uint32_t Surreal_GetWebXRInputMaxSources();
	int Surreal_ValidateWebXRInputSnapshot(const void* inputData, uint32_t bufferBytes);
	int Surreal_SubmitWebXRInputSnapshot(const void* inputData, uint32_t bufferBytes);
	int Surreal_GetWebXRInputLastError();
	void Surreal_ClearWebXRInputSnapshot();
}
