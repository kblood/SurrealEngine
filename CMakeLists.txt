cmake_minimum_required(VERSION 3.15)

project(SurrealEngine)

include(CTest)

option(SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS "Use the experimental on-demand WasmFS OPFS game-data mount" OFF)
option(SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS_ASYNCIFY "Use the experimental Window-owned Asyncify/WasmFS OPFS game-data mount" OFF)
set(SURREAL_WEB_RELEASE_PROFILE "diagnostic" CACHE STRING "Browser build profile: diagnostic or production")
set_property(CACHE SURREAL_WEB_RELEASE_PROFILE PROPERTY STRINGS diagnostic production)
if(NOT SURREAL_WEB_RELEASE_PROFILE MATCHES "^(diagnostic|production)$")
	message(FATAL_ERROR "SURREAL_WEB_RELEASE_PROFILE must be diagnostic or production")
endif()
if(EMSCRIPTEN AND SURREAL_WEB_RELEASE_PROFILE STREQUAL "production" AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
	message(FATAL_ERROR "The production browser profile requires CMAKE_BUILD_TYPE=Release")
endif()
if((SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS OR SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS_ASYNCIFY) AND NOT EMSCRIPTEN)
	message(FATAL_ERROR "The experimental WasmFS OPFS options are available only for Emscripten builds")
endif()
if(SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS AND SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS_ASYNCIFY)
	message(FATAL_ERROR "Select only one experimental WasmFS OPFS execution model")
endif()

set(SURREAL_WEB_USE_PTHREADS ON)
if(EMSCRIPTEN AND SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS_ASYNCIFY)
	set(SURREAL_WEB_USE_PTHREADS OFF)
endif()
option(SURREAL_WEB_EXPERIMENTAL_PROXY_TO_PTHREAD "Run the experimental Emscripten application main on a worker" OFF)
if(SURREAL_WEB_EXPERIMENTAL_PROXY_TO_PTHREAD AND NOT EMSCRIPTEN)
	message(FATAL_ERROR "SURREAL_WEB_EXPERIMENTAL_PROXY_TO_PTHREAD is available only for Emscripten builds")
endif()

if(EMSCRIPTEN)
	# Every TU in every subdirectory must be compiled with atomics/bulk-memory when
	# the final link uses shared memory. The Asyncify experiment deliberately keeps
	# the application on Window and therefore compiles without pthreads. Likewise,
	# exception catching (needed throughout - the engine uses try/catch
	# pervasively, e.g. File::try_open_existing) must be enabled per-TU at
	# compile time, not just as a link setting.
	if(SURREAL_WEB_USE_PTHREADS)
		add_compile_options(-pthread)
	endif()
	add_compile_options(-sNO_DISABLE_EXCEPTION_CATCHING)
endif()

option(SURREAL_ENABLE_OPENXR "Build the optional native OpenXR Vulkan provider" OFF)
if(SURREAL_ENABLE_OPENXR)
	include(FetchContent)
	set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
	set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
	FetchContent_Declare(openxr
		GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
		GIT_TAG release-1.0.34
	)
	FetchContent_MakeAvailable(openxr)
endif()
add_subdirectory(Thirdparty/openmpt)
if(NOT EMSCRIPTEN)
	add_subdirectory(SurrealGPU)
endif()
add_subdirectory(SurrealWidgets)

if (WIN32)
	add_subdirectory(Thirdparty/openal-soft)
endif()

project(SurrealEngine)

set(SURREALCOMMON_SOURCES
	SurrealEngine/Precomp.cpp
	SurrealEngine/Precomp.h
	SurrealEngine/GameApp.cpp
	SurrealEngine/GameApp.h
	SurrealEngine/EditorApp.cpp
	SurrealEngine/EditorApp.h
	SurrealEngine/DebuggerApp.cpp
	SurrealEngine/DebuggerApp.h
	SurrealEngine/Engine.cpp
	SurrealEngine/Engine.h
	SurrealEngine/GameSupport.cpp
	SurrealEngine/GameSupport.h
	SurrealEngine/BotBenchmark/BotBenchmarkDriver.cpp
	SurrealEngine/BotBenchmark/BotBenchmarkDriver.h
	SurrealEngine/BotBenchmark/BotBenchmarkDeathAttribution.cpp
	SurrealEngine/BotBenchmark/BotBenchmarkDeathAttribution.h
	SurrealEngine/BotBenchmark/BotBenchmarkDeathAttributionCoordinator.cpp
	SurrealEngine/BotBenchmark/BotBenchmarkDeathAttributionCoordinator.h
	SurrealEngine/BotBenchmark/BotBenchmarkDeathAttributionHookContract.cpp
	SurrealEngine/BotBenchmark/BotBenchmarkDeathAttributionHookContract.h
	SurrealEngine/BotBenchmark/BotControlledMatch.cpp
	SurrealEngine/BotBenchmark/BotControlledMatch.h
	SurrealEngine/BotBenchmark/BotBenchmarkGameProfile.cpp
	SurrealEngine/BotBenchmark/BotBenchmarkGameProfile.h
	SurrealEngine/BotBenchmark/BotBenchmarkProtocol.cpp
	SurrealEngine/BotBenchmark/BotBenchmarkProtocol.h
	SurrealEngine/BotBenchmark/BotBenchmarkRoster.cpp
	SurrealEngine/BotBenchmark/BotBenchmarkRoster.h
	SurrealEngine/BotBenchmark/BotBenchmarkShadowTelemetry.cpp
	SurrealEngine/BotBenchmark/BotBenchmarkShadowTelemetry.h
	SurrealEngine/BotBenchmark/BotSpectatorMatch.cpp
	SurrealEngine/BotBenchmark/BotSpectatorMatch.h
	SurrealEngine/BotBenchmark/BotSpectatorMatchConfig.cpp
	SurrealEngine/GameSupport/DeusEx/TextTokenizer.cpp
	SurrealEngine/GameSupport/DeusEx/TextTokenizer.h
	SurrealEngine/BotBenchmark/BotBenchmarkTelemetry.cpp
	SurrealEngine/BotBenchmark/BotBenchmarkTelemetry.h
	SurrealEngine/BotAI/BotPolicy.h
	SurrealEngine/BotAI/BotPolicyObservationBuilder.cpp
	SurrealEngine/BotAI/BotPolicyObservationBuilder.h
	SurrealEngine/BotAI/BotPolicyRegistry.cpp
	SurrealEngine/BotAI/BotPolicyRegistry.h
	SurrealEngine/BotAI/BotPolicyShadow.cpp
	SurrealEngine/BotAI/BotPolicyShadow.h
	SurrealEngine/BotAI/BotPolicyShadowTelemetry.cpp
	SurrealEngine/BotAI/BotPolicyShadowTelemetry.h
	SurrealEngine/BotAI/BotMovementSafety.cpp
	SurrealEngine/BotAI/BotMovementSafety.h
	SurrealEngine/BotAI/UtilityBotPolicy.cpp
	SurrealEngine/BotAI/UtilityBotPolicy.h
	SurrealEngine/BotAI/TacticalBotPolicy.cpp
	SurrealEngine/BotAI/TacticalBotPolicy.h
	SurrealEngine/GameFolder.cpp
	SurrealEngine/GameFolder.h
	SurrealEngine/LauncherSettings.cpp
	SurrealEngine/LauncherSettings.h
	SurrealEngine/GameWindow.cpp
	SurrealEngine/GameWindow.h
	SurrealEngine/Input/InputComposition.cpp
	SurrealEngine/Input/InputComposition.h
	SurrealEngine/Input/DesktopInputDefaults.cpp
	SurrealEngine/Input/DesktopInputDefaults.h
	SurrealEngine/Input/XRInputAdapter.cpp
	SurrealEngine/Input/XRInputAdapter.h
	SurrealEngine/XR/XRCommon.cpp
	SurrealEngine/XR/XRCommon.h
	SurrealEngine/XR/XRInputDiagnostics.cpp
	SurrealEngine/XR/XRInputDiagnostics.h
	SurrealEngine/XR/XRHapticFeedbackPolicy.cpp
	SurrealEngine/XR/XRHapticFeedbackPolicy.h
	SurrealEngine/XR/XRWeaponPoseSolver.cpp
	SurrealEngine/XR/XRWeaponPoseSolver.h
	SurrealEngine/XR/XRWeaponRuntime.cpp
	SurrealEngine/XR/XRWeaponRuntime.h
	SurrealEngine/XR/XRLaunchPolicy.cpp
	SurrealEngine/XR/XRLaunchPolicy.h
	SurrealEngine/UE1GameDatabase.h
	SurrealEngine/UE1GameDatabase.cpp
	SurrealEngine/Utils/CommandLine.cpp
	SurrealEngine/Utils/CommandLine.h
	SurrealEngine/Utils/Convert.cpp
	SurrealEngine/Utils/Convert.h
	SurrealEngine/Utils/Exception.cpp
	SurrealEngine/Utils/Exception.h
	SurrealEngine/Utils/File.cpp
	SurrealEngine/Utils/File.h
	SurrealEngine/Utils/Logger.cpp
	SurrealEngine/Utils/Logger.h
	SurrealEngine/Utils/JsonValue.cpp
	SurrealEngine/Utils/JsonValue.h
	SurrealEngine/Utils/SHA1Sum.cpp
	SurrealEngine/Utils/SHA1Sum.h
	SurrealEngine/Utils/StrTools.cpp
	SurrealEngine/Utils/StrTools.h
	SurrealEngine/Utils/Random.cpp
	SurrealEngine/Utils/Random.h
	SurrealEngine/Runtime/DeterministicRuntime.cpp
	SurrealEngine/Runtime/DeterministicRuntime.h
	SurrealEngine/Runtime/HeadlessDriver.cpp
	SurrealEngine/Runtime/HeadlessDriver.h
	SurrealEngine/Utils/UTF16.cpp
	SurrealEngine/Utils/UTF16.h
	SurrealEngine/Utils/UTF8Reader.cpp
	SurrealEngine/Utils/UTF8Reader.h
	SurrealEngine/Utils/MemoryStreamWriter.cpp
	SurrealEngine/Utils/MemoryStreamWriter.h
	SurrealEngine/Utils/Array.h
	SurrealEngine/Utils/CrashReporter.cpp
	SurrealEngine/Utils/CrashReporter.h
	SurrealEngine/Utils/AlignedAlloc.h
	SurrealEngine/Commandlet/Commandlet.cpp
	SurrealEngine/Commandlet/Commandlet.h
	SurrealEngine/Commandlet/Native/NativeCommandlet.cpp
	SurrealEngine/Commandlet/Native/NativeCommandlet.h
	SurrealEngine/Commandlet/Native/NativeObjExtractor.cpp
	SurrealEngine/Commandlet/Native/NativeObjExtractor.h
	SurrealEngine/Commandlet/Native/NativeFuncExtractor.cpp
	SurrealEngine/Commandlet/Native/NativeFuncExtractor.h
	SurrealEngine/Commandlet/Native/NativeCppGenerator.cpp
	SurrealEngine/Commandlet/Native/NativeCppGenerator.h
	SurrealEngine/Commandlet/Native/NativeCppUpdater.cpp
	SurrealEngine/Commandlet/Native/NativeCppUpdater.h
	SurrealEngine/Commandlet/QuitCommandlet.cpp
	SurrealEngine/Commandlet/QuitCommandlet.h
	SurrealEngine/Commandlet/RunCommandlet.cpp
	SurrealEngine/Commandlet/RunCommandlet.h
	SurrealEngine/Commandlet/ExportCommandlet.cpp
	SurrealEngine/Commandlet/ExportCommandlet.h
	SurrealEngine/Commandlet/CompilerCommandlet.cpp
	SurrealEngine/Commandlet/CompilerCommandlet.h
	SurrealEngine/Commandlet/Debug/CollisionCommandlet.cpp
	SurrealEngine/Commandlet/Debug/CollisionCommandlet.h
	SurrealEngine/Commandlet/VM/BreakpointCommandlet.cpp
	SurrealEngine/Commandlet/VM/BreakpointCommandlet.h
	SurrealEngine/Commandlet/VM/CallstackCommandlet.cpp
	SurrealEngine/Commandlet/VM/CallstackCommandlet.h
	SurrealEngine/Commandlet/VM/DisassemblyCommandlet.cpp
	SurrealEngine/Commandlet/VM/DisassemblyCommandlet.h
	SurrealEngine/Commandlet/VM/ListSourceCommandlet.cpp
	SurrealEngine/Commandlet/VM/ListSourceCommandlet.h
	SurrealEngine/Commandlet/VM/LocalsCommandlet.cpp
	SurrealEngine/Commandlet/VM/LocalsCommandlet.h
	SurrealEngine/Commandlet/VM/PrintCommandlet.cpp
	SurrealEngine/Commandlet/VM/PrintCommandlet.h
	SurrealEngine/Commandlet/VM/StepCommandlet.cpp
	SurrealEngine/Commandlet/VM/StepCommandlet.h
	SurrealEngine/Editor/Export.cpp
	SurrealEngine/Editor/Export.h
	SurrealEngine/Render/VisibleActor.cpp
	SurrealEngine/Render/VisibleActor.h
	SurrealEngine/Render/VisibleBrush.cpp
	SurrealEngine/Render/VisibleBrush.h
	SurrealEngine/Render/VisibleCorona.cpp
	SurrealEngine/Render/VisibleCorona.h
	SurrealEngine/Render/VisibleDecal.cpp
	SurrealEngine/Render/VisibleDecal.h
	SurrealEngine/Render/VisibleFrame.cpp
	SurrealEngine/Render/VisibleFrame.h
	SurrealEngine/Render/ViewFamily.h
	SurrealEngine/Render/ViewFamily.cpp
	SurrealEngine/Render/Presentation.h
	SurrealEngine/Platform/OpenXR/OpenXRProvider.cpp
	SurrealEngine/Platform/OpenXR/OpenXRProvider.h
	SurrealEngine/Platform/OpenXR/OpenXRExtensions.cpp
	SurrealEngine/Platform/OpenXR/OpenXRExtensions.h
	SurrealEngine/Platform/OpenXR/OpenXRView.cpp
	SurrealEngine/Platform/OpenXR/OpenXRView.h
	SurrealEngine/Platform/OpenXR/OpenXRUIRuntime.cpp
	SurrealEngine/Platform/OpenXR/OpenXRUIRuntime.h
	SurrealEngine/Platform/Browser/BrowserRelativeMouse.cpp
	SurrealEngine/Platform/Browser/BrowserRelativeMouse.h
	SurrealEngine/Platform/WebXR/WebXRFrameBridge.cpp
	SurrealEngine/Platform/WebXR/WebXRFrameBridge.h
	SurrealEngine/Platform/WebXR/WebXRFramePhase.cpp
	SurrealEngine/Platform/WebXR/WebXRFramePhase.h
	SurrealEngine/Platform/WebXR/WebXRUIProvider.cpp
	SurrealEngine/Platform/WebXR/WebXRUIProvider.h
	SurrealEngine/Platform/WebXR/WebXRUICompositor.cpp
	SurrealEngine/Platform/WebXR/WebXRUICompositor.h
	SurrealEngine/Platform/WebXR/WebXRInputBridge.cpp
	SurrealEngine/Platform/WebXR/WebXRInputBridge.h
	SurrealEngine/Platform/WebXR/WebXRInputAdapter.cpp
	SurrealEngine/Platform/WebXR/WebXRInputAdapter.h
	SurrealEngine/Platform/WebXR/WebXRInputRuntime.cpp
	SurrealEngine/Platform/WebXR/WebXRInputRuntime.h
	SurrealEngine/Platform/WebXR/WebXRHaptics.cpp
	SurrealEngine/Platform/WebXR/WebXRHaptics.h
	SurrealEngine/Platform/WebXR/WebXRProvider.cpp
	SurrealEngine/Render/XRUISurfaces.h
	SurrealEngine/Render/XRUISurfaces.cpp
	SurrealEngine/Render/XRUISurfaceRuntimeAdapter.h
	SurrealEngine/Render/XRUISurfaceRuntimeAdapter.cpp
	SurrealEngine/Render/XRUISurfaceEngineBinding.h
	SurrealEngine/Render/XRUISurfaceEngineBinding.cpp
	SurrealEngine/XR/XRUIRuntime.cpp
	SurrealEngine/XR/XRUIVisuals.cpp
	SurrealEngine/XR/XRUIRuntime.h
	SurrealEngine/XR/XRStartupIntroRoute.cpp
	SurrealEngine/XR/XRStartupIntroRoute.h
	SurrealEngine/XR/XRStartupMenuRoute.cpp
	SurrealEngine/XR/XRStartupMenuRoute.h
	SurrealEngine/Render/VisibleMesh.cpp
	SurrealEngine/Render/VisibleMesh.h
	SurrealEngine/Render/VisibleNode.cpp
	SurrealEngine/Render/VisibleNode.h
	SurrealEngine/Render/VisiblePortal.cpp
	SurrealEngine/Render/VisiblePortal.h
	SurrealEngine/Render/VisibleSprite.cpp
	SurrealEngine/Render/VisibleSprite.h
	SurrealEngine/Render/VisibleTranslucent.h
	SurrealEngine/Render/RenderSubsystem.cpp
	SurrealEngine/Render/RenderXRUISurfaces.cpp
	SurrealEngine/Render/RenderSubsystem.h
	SurrealEngine/Render/RenderCanvas.cpp
	SurrealEngine/Render/RenderScene.cpp
	SurrealEngine/Render/RenderLight.cpp
	SurrealEngine/Render/RenderFog.cpp
	SurrealEngine/Render/RenderWindow.cpp
	SurrealEngine/Render/BspClipper.cpp
	SurrealEngine/Render/BspClipper.h
	SurrealEngine/Render/Lightmap/LightEffect.cpp
	SurrealEngine/Render/Lightmap/LightEffect.h
	SurrealEngine/Render/Lightmap/LightmapBuilder.cpp
	SurrealEngine/Render/Lightmap/LightmapBuilder.h
	SurrealEngine/Render/Lightmap/Shadowmap.cpp
	SurrealEngine/Render/Lightmap/Shadowmap.h
	SurrealEngine/Render/Lightmap/FogmapBuilder.cpp
	SurrealEngine/Render/Lightmap/FogmapBuilder.h
	SurrealEngine/VM/NativeFunc.cpp
	SurrealEngine/VM/Frame.cpp
	SurrealEngine/VM/CallHooks.cpp
	SurrealEngine/VM/CallHooks.h
	SurrealEngine/VM/ExpressionValue.h
	SurrealEngine/VM/StructValue.h
	SurrealEngine/VM/ArrayValue.h
	SurrealEngine/VM/ScriptCall.cpp
	SurrealEngine/VM/Bytecode.cpp
	SurrealEngine/VM/Bytecode.h
	SurrealEngine/VM/ScriptCall.h
	SurrealEngine/VM/ExpressionEvaluator.h
	SurrealEngine/VM/Frame.h
	SurrealEngine/VM/NativeFunc.h
	SurrealEngine/VM/Expression.h
	SurrealEngine/VM/ExpressionEvaluator.cpp
	SurrealEngine/VM/ExpressionVisitor.h
	SurrealEngine/VM/Iterator.cpp
	SurrealEngine/VM/Iterator.h
	SurrealEngine/Audio/AudioSource.h
	SurrealEngine/Audio/AudioSource.cpp
	SurrealEngine/Audio/AudioDevice.cpp
	SurrealEngine/Audio/AudioDevice.h
	SurrealEngine/Video/VideoPlayer.cpp
	SurrealEngine/Video/VideoPlayer.h
	SurrealEngine/Video/VideoFrameScheduler.cpp
	SurrealEngine/Video/VideoFrameScheduler.h
	SurrealEngine/Video/AVIFileReader.h
	SurrealEngine/Video/AVIHeaders.h
	SurrealEngine/Native/NStatLog.h
	SurrealEngine/Native/NZoneInfo.cpp
	SurrealEngine/Native/NNavigationPoint.cpp
	SurrealEngine/Native/NLevelInfo.h
	SurrealEngine/Native/NScriptedTexture.cpp
	SurrealEngine/Native/NPawn.cpp
	SurrealEngine/Native/NGameInfo.h
	SurrealEngine/Native/NGameInfo.cpp
	SurrealEngine/Native/NStatLogFile.h
	SurrealEngine/Native/NWarpZoneInfo.cpp
	SurrealEngine/Native/NScriptedTexture.h
	SurrealEngine/Native/NObject.h
	SurrealEngine/Native/NActor.cpp
	SurrealEngine/Native/N227Emitter.h
	SurrealEngine/Native/N227Emitter.cpp
	SurrealEngine/Native/N227Projector.h
	SurrealEngine/Native/N227Projector.cpp
	SurrealEngine/Native/NPlayerPawn.h
	SurrealEngine/Native/NStatLog.cpp
	SurrealEngine/Native/NStatLogFile.cpp
	SurrealEngine/Native/NPawn.h
	SurrealEngine/Native/NConsole.h
	SurrealEngine/Native/NWarpZoneInfo.h
	SurrealEngine/Native/NLevelInfo.cpp
	SurrealEngine/Native/NCanvas.h
	SurrealEngine/Native/NCommandlet.h
	SurrealEngine/Native/NConsole.cpp
	SurrealEngine/Native/NNavigationPoint.h
	SurrealEngine/Native/NCanvas.cpp
	SurrealEngine/Native/NPlayerPawn.cpp
	SurrealEngine/Native/NCommandlet.cpp
	SurrealEngine/Native/NDecal.cpp
	SurrealEngine/Native/NActor.h
	SurrealEngine/Native/NObject.cpp
	SurrealEngine/Native/NDecal.h
	SurrealEngine/Native/NZoneInfo.h
	SurrealEngine/Native/NInternetLink.cpp
	SurrealEngine/Native/NInternetLink.h
	SurrealEngine/Native/NTcpLink.cpp
	SurrealEngine/Native/NTcpLink.h
	SurrealEngine/Native/NUdpLink.cpp
	SurrealEngine/Native/NUdpLink.h
	SurrealEngine/Native/NDebugInfo.cpp
	SurrealEngine/Native/NDebugInfo.h
	SurrealEngine/Native/NDeusExPlayer.cpp
	SurrealEngine/Native/NDeusExPlayer.h
	SurrealEngine/Native/NScriptedPawn.cpp
	SurrealEngine/Native/NScriptedPawn.h
	SurrealEngine/Native/NDeusExDecoration.cpp
	SurrealEngine/Native/NDeusExDecoration.h
	SurrealEngine/Native/NDumpLocation.cpp
	SurrealEngine/Native/NDumpLocation.h
	SurrealEngine/Native/NDeusExSaveInfo.cpp
	SurrealEngine/Native/NDeusExSaveInfo.h
	SurrealEngine/Native/NGameDirectory.cpp
	SurrealEngine/Native/NGameDirectory.h
	SurrealEngine/Native/NParticleIterator.cpp
	SurrealEngine/Native/NParticleIterator.h
	SurrealEngine/Native/NPlayerPawnExt.cpp
	SurrealEngine/Native/NPlayerPawnExt.h
	SurrealEngine/Native/NUPakPathNodeIterator.cpp
	SurrealEngine/Native/NUPakPathNodeIterator.h
	SurrealEngine/Native/NUPakPawnPathNodeIterator.cpp
	SurrealEngine/Native/NUPakPawnPathNodeIterator.h
	SurrealEngine/Native/NRMusicPlayer.cpp
	SurrealEngine/Native/NRMusicPlayer.h
	SurrealEngine/Native/NBorderWindow.cpp
	SurrealEngine/Native/NBorderWindow.h
	SurrealEngine/Native/NButtonWindow.cpp
	SurrealEngine/Native/NButtonWindow.h
	SurrealEngine/Native/NCheckboxWindow.cpp
	SurrealEngine/Native/NCheckboxWindow.h
	SurrealEngine/Native/NClipWindow.cpp
	SurrealEngine/Native/NClipWindow.h
	SurrealEngine/Native/NComputerWindow.cpp
	SurrealEngine/Native/NComputerWindow.h
	SurrealEngine/Native/NConEvent.cpp
	SurrealEngine/Native/NConEvent.h
	SurrealEngine/Native/NConEventRandomLabel.cpp
	SurrealEngine/Native/NConEventRandomLabel.h
	SurrealEngine/Native/NConversation.cpp
	SurrealEngine/Native/NConversation.h
	SurrealEngine/Native/NDeusExTextParser.cpp
	SurrealEngine/Native/NDeusExTextParser.h
	SurrealEngine/Native/NEditWindow.cpp
	SurrealEngine/Native/NEditWindow.h
	SurrealEngine/Native/NExtensionObject.cpp
	SurrealEngine/Native/NExtensionObject.h
	SurrealEngine/Native/NExtString.cpp
	SurrealEngine/Native/NExtString.h
	SurrealEngine/Native/NFlagBase.cpp
	SurrealEngine/Native/NFlagBase.h
	SurrealEngine/Native/NGC.cpp
	SurrealEngine/Native/NGC.h
	SurrealEngine/Native/NLargeTextWindow.cpp
	SurrealEngine/Native/NLargeTextWindow.h
	SurrealEngine/Native/NListWindow.cpp
	SurrealEngine/Native/NListWindow.h
	SurrealEngine/Native/NModalWindow.cpp
	SurrealEngine/Native/NModalWindow.h
	SurrealEngine/Native/NRadioBoxWindow.cpp
	SurrealEngine/Native/NRadioBoxWindow.h
	SurrealEngine/Native/NRootWindow.cpp
	SurrealEngine/Native/NRootWindow.h
	SurrealEngine/Native/NScaleManagerWindow.cpp
	SurrealEngine/Native/NScaleManagerWindow.h
	SurrealEngine/Native/NScaleWindow.cpp
	SurrealEngine/Native/NScaleWindow.h
	SurrealEngine/Native/NScrollAreaWindow.cpp
	SurrealEngine/Native/NScrollAreaWindow.h
	SurrealEngine/Native/NTextLogWindow.cpp
	SurrealEngine/Native/NTextLogWindow.h
	SurrealEngine/Native/NTextWindow.cpp
	SurrealEngine/Native/NTextWindow.h
	SurrealEngine/Native/NTileWindow.cpp
	SurrealEngine/Native/NTileWindow.h
	SurrealEngine/Native/NTimeDemo.cpp
	SurrealEngine/Native/NTimeDemo.h
	SurrealEngine/Native/NToggleWindow.cpp
	SurrealEngine/Native/NToggleWindow.h
	SurrealEngine/Native/NViewportWindow.cpp
	SurrealEngine/Native/NViewportWindow.h
	SurrealEngine/Native/NWebRequest.cpp
	SurrealEngine/Native/NWebRequest.h
	SurrealEngine/Native/NWebResponse.cpp
	SurrealEngine/Native/NWebResponse.h
	SurrealEngine/Native/NWindow.cpp
	SurrealEngine/Native/NWindow.h
	SurrealEngine/RenderDevice/RenderDevice.cpp
	SurrealEngine/RenderDevice/RenderDevice.h
	SurrealEngine/RenderDevice/ClipSpaceConversion.cpp
	SurrealEngine/RenderDevice/ClipSpaceConversion.h
	SurrealEngine/RenderDevice/RenderDeviceSelection.cpp
	SurrealEngine/RenderDevice/RenderDeviceSelection.h
	SurrealEngine/RenderDevice/Vulkan/BufferManager.cpp
	SurrealEngine/RenderDevice/Vulkan/BufferManager.h
	SurrealEngine/RenderDevice/Vulkan/CachedTexture.h
	SurrealEngine/RenderDevice/Vulkan/CommandBufferManager.cpp
	SurrealEngine/RenderDevice/Vulkan/CommandBufferManager.h
	SurrealEngine/RenderDevice/Vulkan/DescriptorSetManager.cpp
	SurrealEngine/RenderDevice/Vulkan/DescriptorSetManager.h
	SurrealEngine/RenderDevice/Vulkan/FileResource.cpp
	SurrealEngine/RenderDevice/Vulkan/FileResource.h
	SurrealEngine/RenderDevice/Vulkan/FramebufferManager.cpp
	SurrealEngine/RenderDevice/Vulkan/FramebufferManager.h
	SurrealEngine/RenderDevice/Vulkan/RenderPassManager.cpp
	SurrealEngine/RenderDevice/Vulkan/RenderPassManager.h
	SurrealEngine/RenderDevice/Vulkan/SamplerManager.cpp
	SurrealEngine/RenderDevice/Vulkan/SamplerManager.h
	SurrealEngine/RenderDevice/Vulkan/SceneTextures.cpp
	SurrealEngine/RenderDevice/Vulkan/SceneTextures.h
	SurrealEngine/RenderDevice/Vulkan/ShaderManager.cpp
	SurrealEngine/RenderDevice/Vulkan/ShaderManager.h
	SurrealEngine/RenderDevice/Vulkan/TextureManager.cpp
	SurrealEngine/RenderDevice/Vulkan/TextureManager.h
	SurrealEngine/RenderDevice/Vulkan/TextureUploader.cpp
	SurrealEngine/RenderDevice/Vulkan/TextureUploader.h
	SurrealEngine/RenderDevice/Vulkan/UploadManager.cpp
	SurrealEngine/RenderDevice/Vulkan/UploadManager.h
	SurrealEngine/RenderDevice/Vulkan/VulkanRenderDevice.cpp
	SurrealEngine/RenderDevice/Vulkan/VulkanRenderDevice.h
	SurrealEngine/RenderDevice/Vulkan/VulkanGraphicsBinding.h
	SurrealEngine/RenderDevice/Vulkan/VulkanPresentationTargets.cpp
	SurrealEngine/RenderDevice/Vulkan/VulkanPresentationTargets.h
	SurrealEngine/RenderDevice/Null/NullRenderDevice.cpp
	SurrealEngine/RenderDevice/Null/NullRenderDevice.h
	SurrealEngine/Audio/NullAudioDevice.cpp
	SurrealEngine/Audio/NullAudioDevice.h
	SurrealEngine/Package/Package.cpp
	SurrealEngine/Package/Package.h
	SurrealEngine/Package/PackageManager.cpp
	SurrealEngine/Package/PackageManager.h
	SurrealEngine/Package/PackageStream.cpp
	SurrealEngine/Package/PackageStream.h
	SurrealEngine/Package/PackageWriter.cpp
	SurrealEngine/Package/PackageWriter.h
	SurrealEngine/Package/PackageFlags.h
	SurrealEngine/Package/PackageTables.h
	SurrealEngine/Package/ObjectFlags.h
	SurrealEngine/Package/ObjectStream.h
	SurrealEngine/Package/IniFile.h
	SurrealEngine/Package/IniFile.cpp
	SurrealEngine/Package/IniProperty.cpp
	SurrealEngine/Package/IniProperty.h
	SurrealEngine/Package/NameString.cpp
	SurrealEngine/Package/NameString.h
	SurrealEngine/Math/FrustumPlanes.cpp
	SurrealEngine/Math/FrustumPlanes.h
	SurrealEngine/Math/halffloat.cpp
	SurrealEngine/Math/halffloat.h
	SurrealEngine/Math/hsb.cpp
	SurrealEngine/Math/hsb.h
	SurrealEngine/Math/mat.cpp
	SurrealEngine/Math/mat.h
	SurrealEngine/Math/quaternion.h
	SurrealEngine/Math/vec.h
	SurrealEngine/Math/rotator.h
	SurrealEngine/Math/bbox.h
	SurrealEngine/Math/coords.h
	SurrealEngine/Math/scale.h
	SurrealEngine/GC/GC.cpp
	SurrealEngine/GC/GC.h
	SurrealEngine/UObject/ULevel.cpp
	SurrealEngine/UObject/PropertyOffsets.cpp
	SurrealEngine/UObject/UMusic.cpp
	SurrealEngine/UObject/UClient.cpp
	SurrealEngine/UObject/UActor.cpp
	SurrealEngine/UObject/ActorMoveCollisionProbe.cpp
	SurrealEngine/UObject/ActorMoveCollisionProbe.h
	SurrealEngine/UObject/PawnFailedNavigationMemory.cpp
	SurrealEngine/UObject/PawnFailedNavigationMemory.h
	SurrealEngine/UObject/PawnInventoryReachability.cpp
	SurrealEngine/UObject/PawnInventoryReachability.h
	SurrealEngine/UObject/PawnLedgeTransition.cpp
	SurrealEngine/UObject/PawnLedgeTransition.h
	SurrealEngine/UObject/PawnFallingTwoPlaneContact.cpp
	SurrealEngine/UObject/PawnFallingTwoPlaneContact.h
	SurrealEngine/UObject/PawnFallingTwoPlaneSafety.cpp
	SurrealEngine/UObject/PawnFallingTwoPlaneSafety.h
	SurrealEngine/UObject/PawnPainZoneFallPrediction.cpp
	SurrealEngine/UObject/PawnPainZoneFallPrediction.h
	SurrealEngine/UObject/PawnFallingParityForecast.cpp
	SurrealEngine/UObject/PawnFallingParityForecast.h
	SurrealEngine/UObject/PawnWalkingStepPreflight.cpp
	SurrealEngine/UObject/PawnWalkingStepPreflight.h
	SurrealEngine/UObject/PawnPainLedgeRecovery.cpp
	SurrealEngine/UObject/PawnPainLedgeRecovery.h
	SurrealEngine/UObject/PawnMoveStallWatchdog.cpp
	SurrealEngine/UObject/PawnMoveStallWatchdog.h
	SurrealEngine/UObject/PawnMovementArrival.cpp
	SurrealEngine/UObject/PawnMovementArrival.h
	SurrealEngine/UObject/PawnMoveToward.cpp
	SurrealEngine/UObject/PawnMoveToward.h
	SurrealEngine/UObject/PawnPathCost.cpp
	SurrealEngine/UObject/PawnPathCost.h
	SurrealEngine/UObject/PawnWallAdjustment.cpp
	SurrealEngine/UObject/PawnWallAdjustment.h
	SurrealEngine/UObject/PawnWallAdjustRecovery.cpp
	SurrealEngine/UObject/PawnWallAdjustRecovery.h
	SurrealEngine/UObject/ActorMovement.h
	SurrealEngine/UObject/ULevel.h
	SurrealEngine/UObject/UClass.cpp
	SurrealEngine/UObject/UTexture.cpp
	SurrealEngine/UObject/UFont.cpp
	SurrealEngine/UObject/UObject.h
	SurrealEngine/UObject/UProperty.h
	SurrealEngine/UObject/UMusic.h
	SurrealEngine/UObject/UTexture.h
	SurrealEngine/UObject/USound.h
	SurrealEngine/UObject/UDeusExLevelInfo.h
	SurrealEngine/UObject/UDXExtString.cpp
	SurrealEngine/UObject/UDXExtString.h
	SurrealEngine/UObject/UDXGameDirectory.cpp
	SurrealEngine/UObject/UDXGameDirectory.h
	SurrealEngine/UObject/UDXSaveInfo.cpp
	SurrealEngine/UObject/UDXSaveInfo.h
	SurrealEngine/GameSupport/DeusEx/AIPerception.cpp
	SurrealEngine/GameSupport/DeusEx/AIPerception.h
	SurrealEngine/UObject/UDXTextParser.cpp
	SurrealEngine/UObject/UDXTextParser.h
	SurrealEngine/UObject/U227Emitter.cpp
	SurrealEngine/UObject/U227Emitter.h
	SurrealEngine/UObject/U227SkeletalMeshInstance.h
	SurrealEngine/UObject/U227Projector.h
	SurrealEngine/UObject/UClient.h
	SurrealEngine/UObject/UMesh.cpp
	SurrealEngine/UObject/UMesh.h
	SurrealEngine/UObject/UProperty.cpp
	SurrealEngine/UObject/UActor.h
	SurrealEngine/UObject/USound.cpp
	SurrealEngine/UObject/UFont.h
	SurrealEngine/UObject/UTextBuffer.cpp
	SurrealEngine/UObject/UClass.h
	SurrealEngine/UObject/UObject.cpp
	SurrealEngine/UObject/UObjectVersion.cpp
	SurrealEngine/UObject/UObjectVersion.h
	SurrealEngine/UObject/UTextBuffer.h
	SurrealEngine/UObject/UInternetLink.cpp
	SurrealEngine/UObject/UInternetLink.h
	SurrealEngine/UObject/PropertyOffsets.h
	SurrealEngine/UObject/USubsystem.cpp
	SurrealEngine/UObject/USubsystem.h
	SurrealEngine/UObject/ObjectTravelInfo.cpp
	SurrealEngine/UObject/ObjectTravelInfo.h
	SurrealEngine/UObject/UnrealURL.cpp
	SurrealEngine/UObject/UnrealURL.h
	SurrealEngine/UObject/UWindow.cpp
	SurrealEngine/UObject/UWindow.h
	SurrealEngine/UObject/UFlag.cpp
	SurrealEngine/UObject/UFlag.h
	SurrealEngine/UObject/UConSys.cpp
	SurrealEngine/UObject/UConSys.h
	SurrealEngine/UObject/URMusicPlayer.h
	SurrealEngine/Collision/TopLevel/CollisionSystem.cpp
	SurrealEngine/Collision/TopLevel/CollisionSystem.h
	SurrealEngine/Collision/TopLevel/CollisionHit.h
	SurrealEngine/Collision/TopLevel/CollisionTest.h
	SurrealEngine/Collision/TopLevel/TraceTest.cpp
	SurrealEngine/Collision/TopLevel/TraceTest.h
	SurrealEngine/Collision/TopLevel/OverlapTest.cpp
	SurrealEngine/Collision/TopLevel/OverlapTest.h
	SurrealEngine/Collision/BottomLevel/TraceRayModel.cpp
	SurrealEngine/Collision/BottomLevel/TraceRayModel.h
	SurrealEngine/Collision/BottomLevel/TraceAABBModel.cpp
	SurrealEngine/Collision/BottomLevel/TraceAABBModel.h
	SurrealEngine/Collision/BottomLevel/OverlapAABBModel.cpp
	SurrealEngine/Collision/BottomLevel/OverlapAABBModel.h
	SurrealEngine/Light/LightSystem.cpp
	SurrealEngine/Light/LightSystem.h
	SurrealEngine/UI/WidgetResourceData.cpp
	SurrealEngine/UI/WidgetResourceData.h
	SurrealEngine/UI/Editor/AboutDialog.cpp
	SurrealEngine/UI/Editor/AboutDialog.h
	SurrealEngine/UI/ErrorWindow/ErrorWindow.cpp
	SurrealEngine/UI/ErrorWindow/ErrorWindow.h
	SurrealEngine/UI/Editor/EditorMainWindow.cpp
	SurrealEngine/UI/Editor/EditorMainWindow.h
	SurrealEngine/UI/Editor/EditorWorkspace.cpp
	SurrealEngine/UI/Editor/EditorWorkspace.h
	SurrealEngine/UI/Editor/EditorViewportFrame.cpp
	SurrealEngine/UI/Editor/EditorViewportFrame.h
	SurrealEngine/UI/Editor/EditorViewportHeader.cpp
	SurrealEngine/UI/Editor/EditorViewportHeader.h
	SurrealEngine/UI/Editor/EditorViewport.cpp
	SurrealEngine/UI/Editor/EditorViewport.h
	SurrealEngine/UI/Editor/Editor2DViewport.cpp
	SurrealEngine/UI/Editor/Editor2DViewport.h
	SurrealEngine/UI/Editor/Editor3DViewport.cpp
	SurrealEngine/UI/Editor/Editor3DViewport.h
	SurrealEngine/UI/Launcher/LauncherBanner.cpp
	SurrealEngine/UI/Launcher/LauncherBanner.h
	SurrealEngine/UI/Launcher/LauncherButtonbar.cpp
	SurrealEngine/UI/Launcher/LauncherButtonbar.h
	SurrealEngine/UI/Launcher/LauncherWindow.cpp
	SurrealEngine/UI/Launcher/LauncherWindow.h
	SurrealEngine/UI/Launcher/PlayGamePage.cpp
	SurrealEngine/UI/Launcher/PlayGamePage.h
	SurrealEngine/UI/Launcher/VideoSettingsPage.cpp
	SurrealEngine/UI/Launcher/VideoSettingsPage.h
	SurrealEngine/UI/Launcher/VRSettingsPage.cpp
	SurrealEngine/UI/Launcher/VRSettingsPage.h
	SurrealEngine/UI/Launcher/GameFoldersPage.cpp
	SurrealEngine/UI/Launcher/GameFoldersPage.h
	SurrealEngine/Compiler/Ast/Ast.h
	SurrealEngine/Compiler/Ast/AstAccessType.h
	SurrealEngine/Compiler/Ast/AstArray.h
	SurrealEngine/Compiler/Ast/AstCompilationUnit.h
	SurrealEngine/Compiler/Ast/AstExpression.h
	SurrealEngine/Compiler/Ast/AstExpressionVisitor.h
	SurrealEngine/Compiler/Ast/AstGenerics.h
	SurrealEngine/Compiler/Ast/AstMemberDeclaration.h
	SurrealEngine/Compiler/Ast/AstName.h
	SurrealEngine/Compiler/Ast/AstNameVisitor.h
	SurrealEngine/Compiler/Ast/AstNode.h
	SurrealEngine/Compiler/Ast/AstStatement.h
	SurrealEngine/Compiler/Ast/AstStatementVisitor.h
	SurrealEngine/Compiler/Ast/AstTypeDeclaration.h
	SurrealEngine/Compiler/Frontend/Compiler.cpp
	SurrealEngine/Compiler/Frontend/Compiler.h
	SurrealEngine/Compiler/Frontend/CompilerException.h
	SurrealEngine/Compiler/Frontend/CompilerMessage.h
	SurrealEngine/Compiler/Lex/Tokenizer.cpp
	SurrealEngine/Compiler/Lex/Tokenizer.h
	SurrealEngine/Compiler/Lex/Token.h
	SurrealEngine/Compiler/Lex/TextUtil.cpp
	SurrealEngine/Compiler/Lex/TextUtil.h
	SurrealEngine/Compiler/Parse/Parser.cpp
	SurrealEngine/Compiler/Parse/Parser.h
	SurrealEngine/Compiler/Parse/ParserArray.cpp
	SurrealEngine/Compiler/Parse/ParserExpression.cpp
	SurrealEngine/Compiler/Parse/ParserGenerics.cpp
	SurrealEngine/Compiler/Parse/ParserMemberDeclaration.cpp
	SurrealEngine/Compiler/Parse/ParserName.cpp
	SurrealEngine/Compiler/Parse/ParserStatement.cpp
	SurrealEngine/Compiler/Parse/ParserTypeDeclaration.cpp
	SurrealEngine/Compiler/Typesystem/ReferenceType.h
	SurrealEngine/Compiler/Typesystem/Type.h
	SurrealEngine/Compiler/Typesystem/TypeExpression.h
	SurrealEngine/Compiler/Typesystem/TypeExpressionVisitor.h
	SurrealEngine/Compiler/Typesystem/TypeMember.h
	SurrealEngine/Compiler/Typesystem/TypeMemberVisitor.h
	SurrealEngine/Compiler/Typesystem/TypeSystem.cpp
	SurrealEngine/Compiler/Typesystem/TypeSystem.h
	SurrealEngine/Compiler/Typesystem/TypeVisitor.h
	SurrealEngine/Compiler/Typesystem/ValueType.h
)

set(SURREALCOMMON_WIN32_SOURCES
	SurrealEngine/SurrealEngine.natvis
	SurrealEngine/Utils/ComPtr.h
	SurrealEngine/RenderDevice/D3D11/D3D11RenderDevice.cpp
	SurrealEngine/RenderDevice/D3D11/D3D11RenderDevice.h
	SurrealEngine/RenderDevice/D3D11/D3D11FileResource.cpp
	SurrealEngine/RenderDevice/D3D11/D3D11FileResource.h
	SurrealEngine/RenderDevice/D3D11/D3D11TextureManager.cpp
	SurrealEngine/RenderDevice/D3D11/D3D11TextureManager.h
	SurrealEngine/RenderDevice/D3D11/D3D11TextureUploader.cpp
	SurrealEngine/RenderDevice/D3D11/D3D11TextureUploader.h
	SurrealEngine/RenderDevice/D3D11/D3D11UploadManager.cpp
	SurrealEngine/RenderDevice/D3D11/D3D11UploadManager.h
	SurrealEngine/RenderDevice/D3D11/D3D11CachedTexture.h
)

set(SURREALCOMMON_WEBGPU_SOURCES
	SurrealEngine/RenderDevice/WebGPU/WebGPUContext.cpp
	SurrealEngine/RenderDevice/WebGPU/WebGPUContext.h
	SurrealEngine/RenderDevice/WebGPU/WebGPURenderDevice.cpp
	SurrealEngine/RenderDevice/WebGPU/WebGPURenderDevice.h
	SurrealEngine/RenderDevice/WebGPU/WebGPUShaders.cpp
	SurrealEngine/RenderDevice/WebGPU/WebGPUShaders.h
	SurrealEngine/RenderDevice/WebGPU/WebGPUCachedTexture.h
	SurrealEngine/RenderDevice/WebGPU/WebGPUTextureUploader.cpp
	SurrealEngine/RenderDevice/WebGPU/WebGPUTextureUploader.h
	SurrealEngine/RenderDevice/WebGPU/WebGPUUploadManager.cpp
	SurrealEngine/RenderDevice/WebGPU/WebGPUUploadManager.h
	SurrealEngine/RenderDevice/WebGPU/WebGPUTextureManager.cpp
	SurrealEngine/RenderDevice/WebGPU/WebGPUTextureManager.h
	SurrealEngine/RenderDevice/WebGPU/WebGPUSamplerCache.cpp
	SurrealEngine/RenderDevice/WebGPU/WebGPUSamplerCache.h
	SurrealEngine/RenderDevice/WebGPU/WebGPUPipelineCache.cpp
	SurrealEngine/RenderDevice/WebGPU/WebGPUPipelineCache.h
)

set(THIRDPARTY_SOURCES
	Thirdparty/dr_wav.h
	Thirdparty/minimp3_ex.h
	Thirdparty/minimp3.h
	Thirdparty/MurmurHash3/MurmurHash3.cpp
	Thirdparty/MurmurHash3/MurmurHash3.h
	Thirdparty/resample/CDSPSincFilterGen.h
	Thirdparty/resample/CDSPHBDownsampler.h
	Thirdparty/resample/CDSPResampler.h
	Thirdparty/resample/CDSPRealFFT.h
	Thirdparty/resample/CDSPFIRFilter.h
	Thirdparty/resample/pffft.cpp
	Thirdparty/resample/r8butil.h
	Thirdparty/resample/CDSPHBUpsampler.h
	Thirdparty/resample/CDSPProcessor.h
	Thirdparty/resample/r8bbase.cpp
	Thirdparty/resample/pffft.h
	Thirdparty/resample/CDSPFracInterpolator.h
	Thirdparty/resample/CDSPBlockConvolver.h
	Thirdparty/resample/fft4g.h
	Thirdparty/resample/r8bconf.h
	Thirdparty/resample/r8bbase.h
	Thirdparty/dr_flac.h
	Thirdparty/stb_vorbis.h
	Thirdparty/TinySHA1/TinySHA1.hpp
	Thirdparty/miniz/miniz.c
	Thirdparty/miniz/miniz.h
)

set(THIRDPARTY_WIN32_SOURCES
)

set(THIRDPARTY_UNIX_SOURCES
)

if(EMSCRIPTEN)
	set(SURREALCOMMON_LIBS
		surrealwidgets
		openmpt
	)
else()
	set(SURREALCOMMON_LIBS
		surrealgpu
		surrealwidgets
		SurrealVideo
		openmpt
	)
endif()

set(SURREALCOMMON_WIN32_LIBS
	OpenAL
	DbgHelp
	Shlwapi
)

set(SURREALCOMMON_UNIX_LIBS
	openal
)

set(SURREALENGINE_SOURCES
	SurrealEngine/MainGame.cpp
)

if(EMSCRIPTEN AND (SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS OR SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS_ASYNCIFY))
	list(APPEND SURREALCOMMON_SOURCES
		SurrealEngine/Platform/BrowserGameDataMount.cpp
		SurrealEngine/Platform/BrowserGameDataMount.h
	)
endif()

set(SURREALEDITOR_SOURCES
	SurrealEngine/MainEditor.cpp
)

set(SURREALDEBUGGER_SOURCES
	SurrealEngine/MainDebugger.cpp
)

set(SURREALVIDEO_SOURCES
	SurrealVideo/SurrealVideo.cpp
	SurrealVideo/SurrealVideo.h
	SurrealVideo/libavcodec/indeo5.c
	SurrealVideo/libavcodec/indeo5data.h
	SurrealVideo/libavcodec/ivi.c
	SurrealVideo/libavcodec/ivi.h
	SurrealVideo/libavcodec/ivi_dsp.c
	SurrealVideo/libavcodec/ivi_dsp.h
	SurrealVideo/libavcodec/vlc.c
	SurrealVideo/libavcodec/vlc.h
	SurrealVideo/libavcodec/adpcm.c
	SurrealVideo/libavcodec/adpcm.h
	SurrealVideo/libavcodec/adpcm_data.c
	SurrealVideo/libavcodec/adpcm_data.h
	SurrealVideo/libavcodec/codec_internal.h
	SurrealVideo/libavcodec/decode.h
	SurrealVideo/libavcodec/defs.h
	SurrealVideo/libavcodec/get_bits.h
	SurrealVideo/libavcodec/mathops.h
	SurrealVideo/libavcodec/packet.h
	SurrealVideo/libavutil/reverse.c
	SurrealVideo/libavutil/mem.c
	SurrealVideo/libavutil/attributes.h
	SurrealVideo/libavutil/avassert.h
	SurrealVideo/libavutil/bswap.h
	SurrealVideo/libavutil/config.h
	SurrealVideo/libavutil/dynarray.h
	SurrealVideo/libavutil/error.h
	SurrealVideo/libavutil/frame.h
	SurrealVideo/libavutil/imgutils.h
	SurrealVideo/libavutil/intfloat.h
	SurrealVideo/libavutil/intmath.h
	SurrealVideo/libavutil/intreadwrite.h
	SurrealVideo/libavutil/log.h
	SurrealVideo/libavutil/macros.h
	SurrealVideo/libavutil/mem.c
	SurrealVideo/libavutil/mem.h
	SurrealVideo/libavutil/pixfmt.h
	SurrealVideo/libavutil/qsort.h
	SurrealVideo/libavutil/reverse.h
	SurrealVideo/libavutil/thread.h
)

source_group("SurrealEngine" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/.+")
source_group("SurrealEngine\\Audio" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Audio/.+")
source_group("SurrealEngine\\Math" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Math/.+")
source_group("SurrealEngine\\Native" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Native/.+")
source_group("SurrealEngine\\Package" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Package/.+")
source_group("SurrealEngine\\RenderDevice" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/RenderDevice/.+")
source_group("SurrealEngine\\RenderDevice/Vulkan" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/RenderDevice/Vulkan/.+")
source_group("SurrealEngine\\RenderDevice/D3D11" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/RenderDevice/D3D11/.+")
source_group("SurrealEngine\\RenderDevice/WebGPU" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/RenderDevice/WebGPU/.+")
source_group("SurrealEngine\\Render" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Render/.+")
source_group("SurrealEngine\\Render\\Lightmap" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Render/Lightmap/.+")
source_group("SurrealEngine\\UI" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/UI/.+")
source_group("SurrealEngine\\UI\\ErrorWindow" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/UI/ErrorWindow/.+")
source_group("SurrealEngine\\UI\\Editor" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/UI/Editor/.+")
source_group("SurrealEngine\\UI\\Launcher" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/UI/Launcher/.+")
source_group("SurrealEngine\\UObject" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/UObject/.+")
source_group("SurrealEngine\\Collision" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Collision/.+")
source_group("SurrealEngine\\Collision\\TopLevel" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Collision/TopLevel/.+")
source_group("SurrealEngine\\Collision\\BottomLevel" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Collision/BottomLevel/.+")
source_group("SurrealEngine\\Light" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Light/.+")
source_group("SurrealEngine\\Commandlet" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Commandlet/.+")
source_group("SurrealEngine\\Commandlet\\Debug" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Commandlet/Debug/.+")
source_group("SurrealEngine\\Commandlet\\VM" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Commandlet/VM/.+")
source_group("SurrealEngine\\Commandlet\\Native" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Commandlet/Native/.+")
source_group("SurrealEngine\\Editor" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Editor/.+")
source_group("SurrealEngine\\Utils" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Utils/.+")
source_group("SurrealEngine\\GC" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/GC/.+")
source_group("SurrealEngine\\VM" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/VM/.+")
source_group("SurrealEngine\\Video" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Video/.+")
source_group("SurrealEngine\\Compiler" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Compiler/.+")
source_group("SurrealEngine\\Compiler\\Ast" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Compiler/Ast/.+")
source_group("SurrealEngine\\Compiler\\Frontend" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Compiler/Frontend/.+")
source_group("SurrealEngine\\Compiler\\Lex" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Compiler/Lex/.+")
source_group("SurrealEngine\\Compiler\\Parse" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Compiler/Parse/.+")
source_group("SurrealEngine\\Compiler\\Typesystem" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Compiler/Typesystem/.+")

source_group("SurrealVideo" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealVideo/.+")
source_group("SurrealVideo\\libavcodec" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealVideo/libavcodec/.+")
source_group("SurrealVideo\\libavutil" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/SurrealVideo/libavutil/.+")

source_group("Thirdparty" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/.+")
source_group("Thirdparty\\resample" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/resample/.+")
source_group("Thirdparty\\dumb" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/dumb/.+")
source_group("Thirdparty\\dumb\\src" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/dumb/src/.+")
source_group("Thirdparty\\dumb\\src\\core" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/dumb/src/core.+")
source_group("Thirdparty\\dumb\\src\\helpers" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/dumb/src/helpers/.+")
source_group("Thirdparty\\dumb\\src\\it" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/dumb/src/it/.+")
source_group("Thirdparty\\dumb\\include" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/dumb/include/.+")
source_group("Thirdparty\\dumb\\include\\internal" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/dumb/include/internal/.+")
source_group("Thirdparty\\miniz" REGULAR_EXPRESSION "${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/miniz/.+")

include_directories(SurrealEngine SurrealWidgets/include Thirdparty Thirdparty/openmpt Thirdparty/openal-soft/include Thirdparty/miniz)
if(NOT EMSCRIPTEN)
	include_directories(SurrealGPU/include)
endif()

if(NOT APPLE AND SDL2_FOUND)
	include_directories(SurrealEngine ${SDL2_INCLUDE_DIRS})
	if (NOT DEFINED USE_SDL2)
		add_definitions(-DUSE_SDL2)
	endif()
endif()

if(WIN32)
	set(SURREALCOMMON_SOURCES ${SURREALCOMMON_SOURCES} ${SURREALCOMMON_WIN32_SOURCES})
	set(SURREALENGINE_SOURCES ${SURREALENGINE_SOURCES} Win32Resources/SurrealEngine.rc Win32Resources/SurrealEngine.manifest)
	set(SURREALEDITOR_SOURCES ${SURREALEDITOR_SOURCES} Win32Resources/SurrealEngine.rc Win32Resources/SurrealEngine.manifest)
	set(SURREALDEBUGGER_SOURCES ${SURREALDEBUGGER_SOURCES} Win32Resources/SurrealEngine.rc Win32Resources/SurrealEngine.manifest)
	set(SURREALCOMMON_LIBS ${SURREALCOMMON_LIBS} ${SURREALCOMMON_WIN32_LIBS})
	set(THIRDPARTY_SOURCES ${THIRDPARTY_SOURCES} ${THIRDPARTY_WIN32_SOURCES})
	add_definitions(-DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS)
elseif(APPLE)
	set(SURREALCOMMON_SOURCES ${SURREALCOMMON_SOURCES} ${SURREALCOMMON_UNIX_SOURCES})
	set(THIRDPARTY_SOURCES ${THIRDPARTY_SOURCES} ${THIRDPARTY_UNIX_SOURCES})
	set(SURREALCOMMON_LIBS ${SURREALCOMMON_LIBS} ${SURREALCOMMON_UNIX_LIBS} ${CMAKE_DL_LIBS})
	add_definitions(-DUNIX -D_UNIX)
	add_link_options(-pthread)
elseif(EMSCRIPTEN)
	list(FILTER SURREALCOMMON_SOURCES EXCLUDE REGEX "RenderDevice/Vulkan/")
	list(FILTER SURREALCOMMON_SOURCES EXCLUDE REGEX "UI/Editor/")
	list(FILTER SURREALCOMMON_SOURCES EXCLUDE REGEX "SurrealEngine/EditorApp\\.")
	list(FILTER SURREALCOMMON_SOURCES EXCLUDE REGEX "SurrealEngine/DebuggerApp\\.")
	set(SURREALCOMMON_SOURCES ${SURREALCOMMON_SOURCES} ${SURREALVIDEO_SOURCES})
	set(SURREALCOMMON_SOURCES ${SURREALCOMMON_SOURCES} ${SURREALCOMMON_WEBGPU_SOURCES})
	set(THIRDPARTY_SOURCES ${THIRDPARTY_SOURCES} ${THIRDPARTY_UNIX_SOURCES})
	add_definitions(-DUNIX -D_UNIX)
	add_compile_options(--use-port=emdawnwebgpu)
	if(SURREAL_WEB_USE_PTHREADS)
		add_compile_options(-pthread)
		add_link_options(-pthread)
	endif()
else()
	set(SURREALCOMMON_SOURCES ${SURREALCOMMON_SOURCES})
	set(SURREALCOMMON_LIBS ${SURREALCOMMON_LIBS} ${SURREALCOMMON_UNIX_LIBS} ${CMAKE_DL_LIBS} -lasound)
	set(THIRDPARTY_SOURCES ${THIRDPARTY_SOURCES} ${THIRDPARTY_UNIX_SOURCES})
	add_definitions(-DUNIX -D_UNIX)
	add_link_options(-pthread)
endif()

if(MSVC)
	# Use all cores for compilation
	set(CMAKE_CXX_FLAGS "/MP ${CMAKE_CXX_FLAGS}")

	# Ignore warnings in third party code
	set_source_files_properties(${THIRDPARTY_SOURCES} PROPERTIES COMPILE_FLAGS "/wd4244 /wd4267 /wd4005 /wd4018 /wd4101 -D_CRT_SECURE_NO_WARNINGS")
	set_source_files_properties(${SURREALVIDEO_SOURCES} PROPERTIES COMPILE_FLAGS "/wd4244 /wd4267 /wd4005 /wd4018 /wd4101 /wd4146 /wd4334 -D_CRT_SECURE_NO_WARNINGS /experimental:c11atomics")
endif()

if(NOT EMSCRIPTEN)
	add_library(SurrealVideo SHARED ${SURREALVIDEO_SOURCES})
	target_include_directories(SurrealVideo PRIVATE SurrealVideo)
	set_target_properties(SurrealVideo PROPERTIES CXX_STANDARD 20)
	set_target_properties(SurrealVideo PROPERTIES C_STANDARD 17)
endif()

add_library(SurrealCommon STATIC ${SURREALCOMMON_SOURCES} ${THIRDPARTY_SOURCES})
set_target_properties(SurrealCommon PROPERTIES CXX_STANDARD 20)
if(EMSCRIPTEN)
	target_include_directories(SurrealCommon PRIVATE SurrealVideo)
endif()

include(CTest)
if(BUILD_TESTING AND NOT EMSCRIPTEN)
	add_executable(AudioStreamBufferQueueTests Tests/AudioStreamBufferQueueTests.cpp)
	target_include_directories(AudioStreamBufferQueueTests PRIVATE SurrealEngine)
	set_target_properties(AudioStreamBufferQueueTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME AudioStreamBufferQueue COMMAND AudioStreamBufferQueueTests)
	add_executable(AudioGainPolicyTests Tests/AudioGainPolicyTests.cpp)
	target_include_directories(AudioGainPolicyTests PRIVATE SurrealEngine)
	set_target_properties(AudioGainPolicyTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME AudioGainPolicy COMMAND AudioGainPolicyTests)

	add_executable(VideoFrameSchedulerTests
		Tests/VideoFrameSchedulerTests.cpp
		SurrealEngine/Video/VideoFrameScheduler.cpp
	)
	target_include_directories(VideoFrameSchedulerTests PRIVATE SurrealEngine)
	set_target_properties(VideoFrameSchedulerTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME VideoFrameScheduler COMMAND VideoFrameSchedulerTests)

	add_executable(PropertySerializationTests
		Tests/PropertySerializationTests.cpp
	)
	target_include_directories(PropertySerializationTests PRIVATE SurrealEngine)
	target_link_libraries(PropertySerializationTests SurrealCommon ${SURREALCOMMON_LIBS})
	set_target_properties(PropertySerializationTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PropertySerialization COMMAND PropertySerializationTests)

	add_executable(DeusExTextTokenizerTests
		Tests/DeusExTextTokenizerTests.cpp
		SurrealEngine/GameSupport/DeusEx/TextTokenizer.cpp
	)
	target_include_directories(DeusExTextTokenizerTests PRIVATE SurrealEngine)
	set_target_properties(DeusExTextTokenizerTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME DeusExTextTokenizer COMMAND DeusExTextTokenizerTests)

	add_executable(UE1GameDatabaseTests
		Tests/UE1GameDatabaseTests.cpp
	)
	target_include_directories(UE1GameDatabaseTests PRIVATE SurrealEngine)
	target_link_libraries(UE1GameDatabaseTests SurrealCommon ${SURREALCOMMON_LIBS})
	set_target_properties(UE1GameDatabaseTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME UE1GameDatabase COMMAND UE1GameDatabaseTests)
endif()

if(BUILD_TESTING AND EMSCRIPTEN)
	add_executable(BrowserAudioProbe Tests/BrowserAudioProbe.cpp)
	set_target_properties(BrowserAudioProbe PROPERTIES CXX_STANDARD 20)
	target_link_options(BrowserAudioProbe PRIVATE
		-lopenal
		--js-library ${CMAKE_CURRENT_SOURCE_DIR}/web/browser_audio_library.js
		-sEXPORTED_RUNTIME_METHODS=ccall
		-sEXPORTED_FUNCTIONS=_main,_Probe_Start,_Probe_PlayingSources,_Probe_QueuedMusicBuffers,_Surreal_ResumeBrowserAudio,_Surreal_SuspendBrowserAudio,_Surreal_ShutdownBrowserAudio,_Surreal_SetBrowserAudioOutput,_Surreal_GetBrowserAudioState,_Surreal_GetBrowserAudioCurrentTimeMs)

	add_executable(WebXRMainLoopPauseProbe Tests/WebXRMainLoopPauseProbe.cpp)
	set_target_properties(WebXRMainLoopPauseProbe PROPERTIES CXX_STANDARD 20)
	target_link_options(WebXRMainLoopPauseProbe PRIVATE
		-sASYNCIFY
		-sASYNCIFY_STACK_SIZE=65536
		-sINVOKE_RUN=0
		-sEXIT_RUNTIME=0
		-sEXPORTED_RUNTIME_METHODS=callMain,ccall
		-sEXPORTED_FUNCTIONS=_main,_Probe_PauseAndSuspend)

	if(SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS_ASYNCIFY)
		add_executable(WebXRNativeCallbackGateProbe Tests/WebXRNativeCallbackGateProbe.cpp)
		set_target_properties(WebXRNativeCallbackGateProbe PROPERTIES CXX_STANDARD 20)
		set_property(TARGET WebXRNativeCallbackGateProbe APPEND PROPERTY LINK_DEPENDS
			${CMAKE_CURRENT_SOURCE_DIR}/web/browser_native_call_gate_library.js)
		target_compile_options(WebXRNativeCallbackGateProbe PRIVATE -sUSE_SDL=2)
		target_link_options(WebXRNativeCallbackGateProbe PRIVATE
			--js-library ${CMAKE_CURRENT_SOURCE_DIR}/web/browser_native_call_gate_library.js
			-sUSE_SDL=2
			-sASYNCIFY
			-sASYNCIFY_STACK_SIZE=65536
			-sINVOKE_RUN=0
			-sEXIT_RUNTIME=0
			-sEXPORTED_RUNTIME_METHODS=callMain,ccall
			-sEXPORTED_FUNCTIONS=_main,_Probe_StartTimer,_Probe_CancelDeferredTimer,_Probe_PollSDLEvents,_Probe_Suspend)
	endif()
endif()

target_precompile_headers(SurrealCommon PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine/Precomp.h>)
if(SURREAL_ENABLE_OPENXR)
	target_compile_definitions(SurrealCommon PRIVATE SURREAL_ENABLE_OPENXR)
	target_link_libraries(SurrealCommon openxr_loader)
endif()

add_executable(SurrealEngine WIN32 MACOSX_BUNDLE ${SURREALENGINE_SOURCES})
target_link_libraries(SurrealEngine SurrealCommon ${SURREALCOMMON_LIBS})
set_target_properties(SurrealEngine PROPERTIES CXX_STANDARD 20)
if(EMSCRIPTEN)
	find_program(SURREAL_GIT_EXECUTABLE git REQUIRED)
	if(SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS_ASYNCIFY)
		set(SURREAL_BROWSER_ENTRY_POINT "asyncify-opfs")
	else()
		set(SURREAL_BROWSER_ENTRY_POINT "call-main")
	endif()
	set(SURREAL_BROWSER_BUILD_PROVENANCE "${CMAKE_CURRENT_BINARY_DIR}/build-compliance-provenance.json")
	add_custom_target(SurrealBuildProvenance ALL
		COMMAND ${CMAKE_COMMAND}
			-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
			-DOUTPUT_FILE=${SURREAL_BROWSER_BUILD_PROVENANCE}
			-DGIT_EXECUTABLE=${SURREAL_GIT_EXECUTABLE}
			-DCOMPILER_EXECUTABLE=${CMAKE_CXX_COMPILER}
			-DCOMPILER_ID=${CMAKE_CXX_COMPILER_ID}
			-DCOMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}
			-DCMAKE_VERSION_VALUE=${CMAKE_VERSION}
			-DBROWSER_ENTRY_POINT=${SURREAL_BROWSER_ENTRY_POINT}
			-DBUILD_TYPE=${CMAKE_BUILD_TYPE}
			-DRELEASE_PROFILE=${SURREAL_WEB_RELEASE_PROFILE}
			-DUSE_PTHREADS=${SURREAL_WEB_USE_PTHREADS}
			-DWASMFS_OPFS=${SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS}
			-DWASMFS_OPFS_ASYNCIFY=${SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS_ASYNCIFY}
			-DPROXY_TO_PTHREAD=${SURREAL_WEB_EXPERIMENTAL_PROXY_TO_PTHREAD}
			-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteBrowserBuildProvenance.cmake
		BYPRODUCTS ${SURREAL_BROWSER_BUILD_PROVENANCE}
		VERBATIM)
	add_dependencies(SurrealEngine SurrealBuildProvenance)
endif()

include(CTest)
if(BUILD_TESTING AND NOT EMSCRIPTEN)
	add_executable(InputCompositionTests
		Tests/InputCompositionTests.cpp
		SurrealEngine/Input/InputComposition.cpp
	)
	target_include_directories(InputCompositionTests PRIVATE SurrealEngine)
	set_target_properties(InputCompositionTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME InputComposition COMMAND InputCompositionTests)

	add_executable(DesktopInputDefaultsTests
		Tests/DesktopInputDefaultsTests.cpp
		SurrealEngine/Input/DesktopInputDefaults.cpp
	)
	target_include_directories(DesktopInputDefaultsTests PRIVATE SurrealEngine)
	set_target_properties(DesktopInputDefaultsTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME DesktopInputDefaults COMMAND DesktopInputDefaultsTests)

	add_executable(BrowserRelativeMouseTests
		Tests/BrowserRelativeMouseTests.cpp
		SurrealEngine/Platform/Browser/BrowserRelativeMouse.cpp
		SurrealWidgets/src/window/sdl2/browser_relative_mouse.cpp
	)
	find_package(Threads REQUIRED)
	target_link_libraries(BrowserRelativeMouseTests PRIVATE Threads::Threads)
	target_include_directories(BrowserRelativeMouseTests PRIVATE
		SurrealEngine
		SurrealWidgets/include
	)
	set_target_properties(BrowserRelativeMouseTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BrowserRelativeMouse COMMAND BrowserRelativeMouseTests)

	add_executable(DeterministicRuntimeTests
		Tests/DeterministicRuntimeTests.cpp
		SurrealEngine/Runtime/DeterministicRuntime.cpp
		SurrealEngine/Utils/Random.cpp
	)
	target_include_directories(DeterministicRuntimeTests PRIVATE SurrealEngine)
	set_target_properties(DeterministicRuntimeTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME DeterministicRuntime COMMAND DeterministicRuntimeTests)

	add_executable(HeadlessDriverTests
		Tests/HeadlessDriverTests.cpp
		SurrealEngine/Runtime/HeadlessDriver.cpp
		SurrealEngine/Runtime/DeterministicRuntime.cpp
		SurrealEngine/Utils/Random.cpp
	)
	target_include_directories(HeadlessDriverTests PRIVATE SurrealEngine)
	set_target_properties(HeadlessDriverTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME HeadlessDriver COMMAND HeadlessDriverTests)

	add_executable(BotBenchmarkProtocolTests
		Tests/BotBenchmarkProtocolTests.cpp
		SurrealEngine/BotBenchmark/BotBenchmarkProtocol.cpp
		SurrealEngine/BotBenchmark/BotBenchmarkRoster.cpp
	)
	target_include_directories(BotBenchmarkProtocolTests PRIVATE SurrealEngine)
	set_target_properties(BotBenchmarkProtocolTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotBenchmarkProtocol COMMAND BotBenchmarkProtocolTests)

	add_executable(BotBenchmarkTelemetryTests
		Tests/BotBenchmarkTelemetryTests.cpp
		SurrealEngine/BotBenchmark/BotBenchmarkProtocol.cpp
		SurrealEngine/BotBenchmark/BotBenchmarkRoster.cpp
		SurrealEngine/BotBenchmark/BotBenchmarkTelemetry.cpp
		SurrealEngine/UObject/PawnWalkingStepPreflight.cpp
	)
	target_include_directories(BotBenchmarkTelemetryTests PRIVATE SurrealEngine)
	set_target_properties(BotBenchmarkTelemetryTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotBenchmarkTelemetry COMMAND BotBenchmarkTelemetryTests)

	add_executable(BotBenchmarkDeathAttributionTests
		Tests/BotBenchmarkDeathAttributionTests.cpp
		SurrealEngine/BotBenchmark/BotBenchmarkDeathAttribution.cpp
	)
	target_include_directories(BotBenchmarkDeathAttributionTests PRIVATE SurrealEngine)
	set_target_properties(BotBenchmarkDeathAttributionTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotBenchmarkDeathAttribution COMMAND BotBenchmarkDeathAttributionTests)

	add_executable(BotBenchmarkDeathAttributionCoordinatorTests
		Tests/BotBenchmarkDeathAttributionCoordinatorTests.cpp
		SurrealEngine/BotBenchmark/BotBenchmarkDeathAttribution.cpp
		SurrealEngine/BotBenchmark/BotBenchmarkDeathAttributionCoordinator.cpp
	)
	target_include_directories(BotBenchmarkDeathAttributionCoordinatorTests PRIVATE SurrealEngine)
	set_target_properties(BotBenchmarkDeathAttributionCoordinatorTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotBenchmarkDeathAttributionCoordinator COMMAND BotBenchmarkDeathAttributionCoordinatorTests)

	add_executable(BotBenchmarkDeathAttributionHookContractTests
		Tests/BotBenchmarkDeathAttributionHookContractTests.cpp
		SurrealEngine/BotBenchmark/BotBenchmarkDeathAttributionHookContract.cpp
	)
	target_include_directories(BotBenchmarkDeathAttributionHookContractTests PRIVATE SurrealEngine)
	set_target_properties(BotBenchmarkDeathAttributionHookContractTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotBenchmarkDeathAttributionHookContract COMMAND BotBenchmarkDeathAttributionHookContractTests)

	add_executable(BotBenchmarkGameProfileTests
		Tests/BotBenchmarkGameProfileTests.cpp
		SurrealEngine/BotBenchmark/BotBenchmarkGameProfile.cpp
	)
	target_include_directories(BotBenchmarkGameProfileTests PRIVATE SurrealEngine)
	set_target_properties(BotBenchmarkGameProfileTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotBenchmarkGameProfile COMMAND BotBenchmarkGameProfileTests)

	add_executable(BotBenchmarkRosterTests
		Tests/BotBenchmarkRosterTests.cpp
		SurrealEngine/BotBenchmark/BotBenchmarkRoster.cpp
	)
	target_include_directories(BotBenchmarkRosterTests PRIVATE SurrealEngine)
	set_target_properties(BotBenchmarkRosterTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotBenchmarkRoster COMMAND BotBenchmarkRosterTests)

	add_executable(BotSpectatorMatchConfigTests
		Tests/BotSpectatorMatchConfigTests.cpp
		SurrealEngine/BotBenchmark/BotSpectatorMatchConfig.cpp
		SurrealEngine/BotBenchmark/BotBenchmarkRoster.cpp
	)
	target_include_directories(BotSpectatorMatchConfigTests PRIVATE SurrealEngine)
	set_target_properties(BotSpectatorMatchConfigTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotSpectatorMatchConfig COMMAND BotSpectatorMatchConfigTests)

	add_executable(BotBenchmarkShadowTelemetryTests
		Tests/BotBenchmarkShadowTelemetryTests.cpp
		SurrealEngine/BotBenchmark/BotBenchmarkShadowTelemetry.cpp
		SurrealEngine/BotAI/BotPolicyShadowTelemetry.cpp
	)
	target_include_directories(BotBenchmarkShadowTelemetryTests PRIVATE SurrealEngine)
	set_target_properties(BotBenchmarkShadowTelemetryTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotBenchmarkShadowTelemetry COMMAND BotBenchmarkShadowTelemetryTests)

	add_executable(UtilityBotPolicyTests
		Tests/UtilityBotPolicyTests.cpp
		SurrealEngine/BotAI/UtilityBotPolicy.cpp
	)
	target_include_directories(UtilityBotPolicyTests PRIVATE SurrealEngine)
	set_target_properties(UtilityBotPolicyTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME UtilityBotPolicy COMMAND UtilityBotPolicyTests)

	add_executable(TacticalBotPolicyTests
		Tests/TacticalBotPolicyTests.cpp
		SurrealEngine/BotAI/TacticalBotPolicy.cpp
	)
	target_include_directories(TacticalBotPolicyTests PRIVATE SurrealEngine)
	set_target_properties(TacticalBotPolicyTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME TacticalBotPolicy COMMAND TacticalBotPolicyTests)

	add_executable(BotPolicyRegistryTests
		Tests/BotPolicyRegistryTests.cpp
		SurrealEngine/BotAI/BotPolicyRegistry.cpp
		SurrealEngine/BotAI/UtilityBotPolicy.cpp
		SurrealEngine/BotAI/TacticalBotPolicy.cpp
	)
	target_include_directories(BotPolicyRegistryTests PRIVATE SurrealEngine)
	set_target_properties(BotPolicyRegistryTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotPolicyRegistry COMMAND BotPolicyRegistryTests)

	add_executable(BotPolicyShadowTests
		Tests/BotPolicyShadowTests.cpp
		SurrealEngine/BotAI/BotPolicyRegistry.cpp
		SurrealEngine/BotAI/BotPolicyShadow.cpp
		SurrealEngine/BotAI/UtilityBotPolicy.cpp
		SurrealEngine/BotAI/TacticalBotPolicy.cpp
	)
	target_include_directories(BotPolicyShadowTests PRIVATE SurrealEngine)
	set_target_properties(BotPolicyShadowTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotPolicyShadow COMMAND BotPolicyShadowTests)

	add_executable(BotPolicyShadowTelemetryTests
		Tests/BotPolicyShadowTelemetryTests.cpp
		SurrealEngine/BotAI/BotPolicyShadowTelemetry.cpp
	)
	target_include_directories(BotPolicyShadowTelemetryTests PRIVATE SurrealEngine)
	set_target_properties(BotPolicyShadowTelemetryTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotPolicyShadowTelemetry COMMAND BotPolicyShadowTelemetryTests)

	add_executable(BotPolicyObservationBuilderTests
		Tests/BotPolicyObservationBuilderTests.cpp
		SurrealEngine/BotAI/BotPolicyObservationBuilder.cpp
	)
	target_include_directories(BotPolicyObservationBuilderTests PRIVATE SurrealEngine)
	set_target_properties(BotPolicyObservationBuilderTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotPolicyObservationBuilder COMMAND BotPolicyObservationBuilderTests)

	add_executable(BotMovementSafetyTests
		Tests/BotMovementSafetyTests.cpp
		SurrealEngine/BotAI/BotMovementSafety.cpp
	)
	target_include_directories(BotMovementSafetyTests PRIVATE SurrealEngine)
	set_target_properties(BotMovementSafetyTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME BotMovementSafety COMMAND BotMovementSafetyTests)

	add_executable(CollisionActorOrderTests Tests/CollisionActorOrderTests.cpp)
	target_include_directories(CollisionActorOrderTests PRIVATE SurrealEngine)
	set_target_properties(CollisionActorOrderTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME CollisionActorOrder COMMAND CollisionActorOrderTests)

	add_executable(ActorMoveCollisionProbeTests
		Tests/ActorMoveCollisionProbeTests.cpp
		SurrealEngine/UObject/ActorMoveCollisionProbe.cpp
	)
	target_include_directories(ActorMoveCollisionProbeTests PRIVATE SurrealEngine)
	set_target_properties(ActorMoveCollisionProbeTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME ActorMoveCollisionProbe COMMAND ActorMoveCollisionProbeTests)

	add_executable(PawnMovementArrivalTests
		Tests/PawnMovementArrivalTests.cpp
		SurrealEngine/UObject/PawnMovementArrival.cpp
	)
	target_include_directories(PawnMovementArrivalTests PRIVATE SurrealEngine)
	set_target_properties(PawnMovementArrivalTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnMovementArrival COMMAND PawnMovementArrivalTests)

	add_executable(PawnFailedNavigationMemoryTests
		Tests/PawnFailedNavigationMemoryTests.cpp
		SurrealEngine/UObject/PawnFailedNavigationMemory.cpp
	)
	target_include_directories(PawnFailedNavigationMemoryTests PRIVATE SurrealEngine)
	set_target_properties(PawnFailedNavigationMemoryTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnFailedNavigationMemory COMMAND PawnFailedNavigationMemoryTests)

	add_executable(PawnInventoryReachabilityTests
		Tests/PawnInventoryReachabilityTests.cpp
		SurrealEngine/UObject/PawnInventoryReachability.cpp
	)
	target_include_directories(PawnInventoryReachabilityTests PRIVATE SurrealEngine)
	set_target_properties(PawnInventoryReachabilityTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnInventoryReachability COMMAND PawnInventoryReachabilityTests)

	add_executable(PawnLedgeTransitionTests
		Tests/PawnLedgeTransitionTests.cpp
		SurrealEngine/UObject/PawnLedgeTransition.cpp
	)
	target_include_directories(PawnLedgeTransitionTests PRIVATE SurrealEngine)
	set_target_properties(PawnLedgeTransitionTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnLedgeTransition COMMAND PawnLedgeTransitionTests)

	add_executable(PawnPainZoneFallPredictionTests
		Tests/PawnPainZoneFallPredictionTests.cpp
		SurrealEngine/UObject/PawnPainZoneFallPrediction.cpp
	)
	target_include_directories(PawnPainZoneFallPredictionTests PRIVATE SurrealEngine)
	set_target_properties(PawnPainZoneFallPredictionTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnPainZoneFallPrediction COMMAND PawnPainZoneFallPredictionTests)

	add_executable(PawnFallingParityForecastTests
		Tests/PawnFallingParityForecastTests.cpp
		SurrealEngine/UObject/PawnFallingParityForecast.cpp
	)
	target_include_directories(PawnFallingParityForecastTests PRIVATE SurrealEngine)
	set_target_properties(PawnFallingParityForecastTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnFallingParityForecast COMMAND PawnFallingParityForecastTests)

	add_executable(PawnFallingTwoPlaneContactTests
		Tests/PawnFallingTwoPlaneContactTests.cpp
		SurrealEngine/UObject/PawnFallingTwoPlaneContact.cpp
	)
	target_include_directories(PawnFallingTwoPlaneContactTests PRIVATE SurrealEngine)
	set_target_properties(PawnFallingTwoPlaneContactTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnFallingTwoPlaneContact COMMAND PawnFallingTwoPlaneContactTests)

	add_executable(PawnFallingTwoPlaneSafetyTests
		Tests/PawnFallingTwoPlaneSafetyTests.cpp
		SurrealEngine/UObject/PawnFallingTwoPlaneSafety.cpp
		SurrealEngine/UObject/PawnFallingTwoPlaneContact.cpp
		SurrealEngine/UObject/PawnPainZoneFallPrediction.cpp
	)
	target_include_directories(PawnFallingTwoPlaneSafetyTests PRIVATE SurrealEngine)
	set_target_properties(PawnFallingTwoPlaneSafetyTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnFallingTwoPlaneSafety COMMAND PawnFallingTwoPlaneSafetyTests)

	add_executable(PawnWalkingStepPreflightTests
		Tests/PawnWalkingStepPreflightTests.cpp
		SurrealEngine/UObject/PawnWalkingStepPreflight.cpp
	)
	target_include_directories(PawnWalkingStepPreflightTests PRIVATE SurrealEngine)
	set_target_properties(PawnWalkingStepPreflightTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnWalkingStepPreflight COMMAND PawnWalkingStepPreflightTests)

	add_executable(PawnPainLedgeRecoveryTests
		Tests/PawnPainLedgeRecoveryTests.cpp
		SurrealEngine/UObject/PawnPainLedgeRecovery.cpp
	)
	target_include_directories(PawnPainLedgeRecoveryTests PRIVATE SurrealEngine)
	set_target_properties(PawnPainLedgeRecoveryTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnPainLedgeRecovery COMMAND PawnPainLedgeRecoveryTests)

	add_executable(PawnMoveStallWatchdogTests
		Tests/PawnMoveStallWatchdogTests.cpp
		SurrealEngine/UObject/PawnMoveStallWatchdog.cpp
	)
	target_include_directories(PawnMoveStallWatchdogTests PRIVATE SurrealEngine)
	set_target_properties(PawnMoveStallWatchdogTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnMoveStallWatchdog COMMAND PawnMoveStallWatchdogTests)

	add_executable(PawnMoveTowardTests
		Tests/PawnMoveTowardTests.cpp
		SurrealEngine/UObject/PawnMoveToward.cpp
	)
	target_include_directories(PawnMoveTowardTests PRIVATE SurrealEngine)
	set_target_properties(PawnMoveTowardTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnMoveToward COMMAND PawnMoveTowardTests)

	add_executable(PawnPathCostTests
		Tests/PawnPathCostTests.cpp
		SurrealEngine/UObject/PawnPathCost.cpp
	)
	target_include_directories(PawnPathCostTests PRIVATE SurrealEngine)
	set_target_properties(PawnPathCostTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnPathCost COMMAND PawnPathCostTests)

	add_executable(PawnWallAdjustmentTests
		Tests/PawnWallAdjustmentTests.cpp
		SurrealEngine/UObject/PawnWallAdjustment.cpp
		SurrealEngine/UObject/PawnPainZoneFallPrediction.cpp
	)
	target_include_directories(PawnWallAdjustmentTests PRIVATE SurrealEngine)
	set_target_properties(PawnWallAdjustmentTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnWallAdjustment COMMAND PawnWallAdjustmentTests)

	add_executable(PawnWallAdjustRecoveryTests
		Tests/PawnWallAdjustRecoveryTests.cpp
		SurrealEngine/UObject/PawnWallAdjustRecovery.cpp
	)
	target_include_directories(PawnWallAdjustRecoveryTests PRIVATE SurrealEngine)
	set_target_properties(PawnWallAdjustRecoveryTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PawnWallAdjustRecovery COMMAND PawnWallAdjustRecoveryTests)

	add_executable(XRInputAdapterTests
		Tests/XRInputAdapterTests.cpp
		SurrealEngine/Input/InputComposition.cpp
		SurrealEngine/Input/XRInputAdapter.cpp
	)
	target_include_directories(XRInputAdapterTests PRIVATE SurrealEngine)
	set_target_properties(XRInputAdapterTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME XRInputAdapter COMMAND XRInputAdapterTests)

	add_executable(XRInputDiagnosticsTests
		Tests/XRInputDiagnosticsTests.cpp
		SurrealEngine/XR/XRInputDiagnostics.cpp
	)
	target_include_directories(XRInputDiagnosticsTests PRIVATE SurrealEngine)
	set_target_properties(XRInputDiagnosticsTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME XRInputDiagnostics COMMAND XRInputDiagnosticsTests)

	add_executable(XRStartupMenuRouteTests
		Tests/XRStartupMenuRouteTests.cpp
		SurrealEngine/XR/XRStartupMenuRoute.cpp
	)
	target_include_directories(XRStartupMenuRouteTests PRIVATE SurrealEngine)
	set_target_properties(XRStartupMenuRouteTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME XRStartupMenuRoute COMMAND XRStartupMenuRouteTests)

	add_executable(XRCommonTests
		Tests/XRCommonTests.cpp
		SurrealEngine/XR/XRCommon.cpp
	)
	target_include_directories(XRCommonTests PRIVATE SurrealEngine)
	set_target_properties(XRCommonTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME XRCommon COMMAND XRCommonTests)

	add_executable(XRWeaponPoseSolverTests
		Tests/XRWeaponPoseSolverTests.cpp
		SurrealEngine/XR/XRCommon.cpp
		SurrealEngine/XR/XRWeaponPoseSolver.cpp
	)
	target_include_directories(XRWeaponPoseSolverTests PRIVATE SurrealEngine)
	set_target_properties(XRWeaponPoseSolverTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME XRWeaponPoseSolver COMMAND XRWeaponPoseSolverTests)

	add_executable(XRLaunchPolicyTests
		Tests/XRLaunchPolicyTests.cpp
		SurrealEngine/XR/XRLaunchPolicy.cpp
	)
	target_include_directories(XRLaunchPolicyTests PRIVATE SurrealEngine)
	set_target_properties(XRLaunchPolicyTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME XRLaunchPolicy COMMAND XRLaunchPolicyTests)

	add_executable(XRWeaponRuntimeTests Tests/XRWeaponRuntimeTests.cpp)
	target_link_libraries(XRWeaponRuntimeTests SurrealCommon ${SURREALCOMMON_LIBS})
	set_target_properties(XRWeaponRuntimeTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME XRWeaponRuntime COMMAND XRWeaponRuntimeTests)

	add_executable(ExpressionEvaluatorTests Tests/ExpressionEvaluatorTests.cpp)
	target_link_libraries(ExpressionEvaluatorTests SurrealCommon ${SURREALCOMMON_LIBS})
	set_target_properties(ExpressionEvaluatorTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME ExpressionEvaluator COMMAND ExpressionEvaluatorTests)

	add_executable(RenderDeviceSelectionTests
		Tests/RenderDeviceSelectionTests.cpp
		SurrealEngine/RenderDevice/ClipSpaceConversion.cpp
		SurrealEngine/RenderDevice/RenderDeviceSelection.cpp
	)
	target_include_directories(RenderDeviceSelectionTests PRIVATE SurrealEngine)
	set_target_properties(RenderDeviceSelectionTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME RenderDeviceSelectionTests COMMAND RenderDeviceSelectionTests)
endif()

if(NOT EMSCRIPTEN)
	add_executable(SurrealEditor WIN32 MACOSX_BUNDLE ${SURREALEDITOR_SOURCES})
	target_link_libraries(SurrealEditor SurrealCommon ${SURREALCOMMON_LIBS})
	set_target_properties(SurrealEditor PROPERTIES CXX_STANDARD 20)

	add_executable(SurrealDebugger ${SURREALDEBUGGER_SOURCES})
	target_link_libraries(SurrealDebugger SurrealCommon ${SURREALCOMMON_LIBS})
	set_target_properties(SurrealDebugger PROPERTIES CXX_STANDARD 20)
endif()

include(CTest)
if(BUILD_TESTING AND NOT EMSCRIPTEN)
	add_executable(ActorMovementTests
		Tests/ActorMovementTests.cpp
	)
	target_include_directories(ActorMovementTests PRIVATE SurrealEngine)
	set_target_properties(ActorMovementTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME ActorMovement COMMAND ActorMovementTests)
endif()

if(BUILD_TESTING)
	add_executable(DXAIPerceptionTests
		Tests/DXAIPerceptionTests.cpp
		SurrealEngine/GameSupport/DeusEx/AIPerception.cpp
	)
	target_include_directories(DXAIPerceptionTests PRIVATE SurrealEngine)
	set_target_properties(DXAIPerceptionTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME DXAIPerception COMMAND DXAIPerceptionTests)
endif()

if(WIN32)
	add_custom_command(TARGET SurrealEngine POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:OpenAL::OpenAL>" $<TARGET_FILE_DIR:SurrealEngine>)
endif()

if(NOT EMSCRIPTEN)
	add_custom_command(TARGET SurrealEngine POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/SurrealEngine.pk3" $<TARGET_FILE_DIR:SurrealEngine>)
endif()

if(MSVC)
	# Create stripped PDBs for nightly builds
	set_target_properties(SurrealEngine PROPERTIES LINK_FLAGS_RELWITHDEBINFO /PDBSTRIPPED:${CMAKE_CURRENT_BINARY_DIR}/RelWithDebInfo/SurrealEngine-stripped.pdb)
	set_target_properties(SurrealEditor PROPERTIES LINK_FLAGS_RELWITHDEBINFO /PDBSTRIPPED:${CMAKE_CURRENT_BINARY_DIR}/RelWithDebInfo/SurrealEditor-stripped.pdb)
	set_target_properties(SurrealDebugger PROPERTIES LINK_FLAGS_RELWITHDEBINFO /PDBSTRIPPED:${CMAKE_CURRENT_BINARY_DIR}/RelWithDebInfo/SurrealDebugger-stripped.pdb)
endif()

if(EMSCRIPTEN)
	# Browser launcher adapter for the provider-neutral persistent XR hand policy.
	# Kept separate from the WebXR ABI so flat browser builds can configure it too.
	set(SURREAL_BROWSER_EXPORTED_FUNCTIONS "_main,_Surreal_GetTickCount,_Surreal_RequestQuit,_Surreal_ForwardBrowserEscape,_Surreal_ForwardBrowserMouseMotion,_Surreal_ResetBrowserMouseMotion,_Surreal_SetBrowserMouseMotionActive,_Surreal_SetXRFrameLoopActive,_Surreal_ResumeBrowserAudio,_Surreal_SuspendBrowserAudio,_Surreal_ShutdownBrowserAudio,_Surreal_SetBrowserAudioOutput,_Surreal_GetBrowserAudioState,_Surreal_GetBrowserAudioCurrentTimeMs,_Surreal_GetWebXRFrameABIVersion,_Surreal_GetWebXRFrameHeaderSize,_Surreal_GetWebXRViewSize,_Surreal_GetWebXRFrameMaxViews,_Surreal_ValidateWebXRFrame,_Surreal_GetWebXRFrameLastError,_Surreal_PrepareWebXRFrame,_Surreal_RenderWebXRFrame,_Surreal_CompleteWebXRFrame,_Surreal_GetWebXRWorldUnitsPerMeter,_Surreal_SetWebXRWorldUnitsPerMeter,_Surreal_ResetWebXRPose,_Surreal_GetWebXRPoseRecenterCount,_Surreal_GetWebXRPointerFeedbackSize,_Surreal_GetWebXRPointerFeedback,_Surreal_GetWebXRInputABIVersion,_Surreal_GetWebXRInputHeaderSize,_Surreal_GetWebXRInputSourceSize,_Surreal_GetWebXRInputMaxSources,_Surreal_ValidateWebXRInputSnapshot,_Surreal_SubmitWebXRInputSnapshot,_Surreal_GetWebXRInputLastError,_Surreal_ClearWebXRInputSnapshot,_Surreal_ApplyWebXRInputSnapshot")
	string(APPEND SURREAL_BROWSER_EXPORTED_FUNCTIONS ",_Surreal_GetXRDominantHand,_Surreal_SetXRDominantHand")
	if(SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS OR SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS_ASYNCIFY)
		string(APPEND SURREAL_BROWSER_EXPORTED_FUNCTIONS ",_Surreal_GetBrowserOPFSMountABIVersion,_Surreal_GetBrowserOPFSMountMode,_Surreal_GetBrowserWasmHeapSize,_Surreal_BeginBrowserOPFSMount,_Surreal_AddBrowserOPFSMountFile,_Surreal_PrepareBrowserOPFSMount")
		target_compile_definitions(SurrealCommon PRIVATE SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS=1)
		if(SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS_ASYNCIFY)
			string(APPEND SURREAL_BROWSER_EXPORTED_FUNCTIONS ",_Surreal_StartBrowserGame")
			target_compile_definitions(SurrealEngine PRIVATE SURREAL_WEB_WASMFS_OPFS_ASYNCIFY=1)
			target_compile_definitions(SurrealCommon PRIVATE
				SURREAL_WEB_WASMFS_OPFS_ASYNCIFY=1
				SURREAL_WEB_SINGLE_THREADED=1
			)
		endif()
	endif()
	if(SURREAL_WEB_EXPERIMENTAL_PROXY_TO_PTHREAD OR SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS)
		target_compile_definitions(surrealwidgets PRIVATE SURREAL_WEB_PROXY_TO_PTHREAD=1)
	endif()
	set(SURREAL_GAMEDATA_DIR "" CACHE PATH "Local path to a UT99 game-data folder to preload into the wasm build (dev-machine-local only; never commit this path's contents — UT99 is commercial, non-redistributable data)")
	if(NOT SURREAL_GAMEDATA_DIR)
		message(WARNING "SURREAL_GAMEDATA_DIR is not set - the Emscripten build will have no game data preloaded. Pass -DSURREAL_GAMEDATA_DIR=<path-to-ut99-install>.")
	endif()
	target_link_options(SurrealEngine PRIVATE
		-lopenal
		--js-library ${CMAKE_CURRENT_SOURCE_DIR}/web/browser_audio_library.js
		-sSTACK_SIZE=8MB
		-sINVOKE_RUN=0
		-sEXIT_RUNTIME=0
		-sEXPORTED_RUNTIME_METHODS=callMain,FS,ccall
		-sEXPORTED_FUNCTIONS=${SURREAL_BROWSER_EXPORTED_FUNCTIONS}
		-sNO_DISABLE_EXCEPTION_CATCHING
		-sUSE_SDL=2
		--use-port=emdawnwebgpu
	)
	if(SURREAL_WEB_RELEASE_PROFILE STREQUAL "production")
		target_link_options(SurrealEngine PRIVATE -O3 -sASSERTIONS=0 -sSTACK_OVERFLOW_CHECK=0)
	else()
		target_link_options(SurrealEngine PRIVATE -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2)
	endif()
	# Chrome rejects browser-API arguments backed by resizable ArrayBuffers.
	# Keep Emscripten's ordinary Wasm buffers and refresh HEAP views after each
	# growth, rather than exposing resizable views to TextDecoder, crypto, or
	# WebGPU. The heap can still grow beyond its measured 256 MiB baseline.
	target_link_options(SurrealEngine PRIVATE
		-sINITIAL_MEMORY=256MB
		-sALLOW_MEMORY_GROWTH=1
		-sGROWABLE_ARRAYBUFFERS=0
	)
	if(SURREAL_WEB_USE_PTHREADS)
		target_link_options(SurrealEngine PRIVATE
			-pthread
			-sPTHREAD_POOL_SIZE=2
		)
	endif()
	if(SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS)
		target_link_options(SurrealEngine PRIVATE
			-sWASMFS
			-sFORCE_FILESYSTEM
		)
	endif()
	if(SURREAL_WEB_EXPERIMENTAL_PROXY_TO_PTHREAD OR SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS)
		target_link_options(SurrealEngine PRIVATE -sPROXY_TO_PTHREAD)

		add_executable(PthreadSDLPresentationProbe web/pthread_sdl_presentation_probe.cpp)
		target_link_libraries(PthreadSDLPresentationProbe PRIVATE surrealwidgets)
		target_link_options(PthreadSDLPresentationProbe PRIVATE
			-pthread
			-sPROXY_TO_PTHREAD
			-sUSE_SDL=2
			-sASSERTIONS=1
			-sEXIT_RUNTIME=1
		)
		set_target_properties(PthreadSDLPresentationProbe PROPERTIES SUFFIX ".html")

		add_executable(PthreadWebGPUDeviceProbe web/pthread_webgpu_device_probe.cpp)
		target_link_options(PthreadWebGPUDeviceProbe PRIVATE
			-pthread
			-sPROXY_TO_PTHREAD
			-sPTHREAD_POOL_SIZE=1
			-sINVOKE_RUN=0
			-sEXIT_RUNTIME=1
			-sASSERTIONS=1
			-sEXPORTED_RUNTIME_METHODS=callMain
			--use-port=emdawnwebgpu
		)
		set_target_properties(PthreadWebGPUDeviceProbe PROPERTIES SUFFIX ".js")
	endif()
	if(SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS_ASYNCIFY)
		set_property(TARGET SurrealEngine APPEND PROPERTY LINK_DEPENDS
			${CMAKE_CURRENT_SOURCE_DIR}/web/browser_native_call_gate_library.js
			${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyEmscriptenMemoryGrowth.cmake)
		target_link_options(SurrealEngine PRIVATE
			--js-library=${CMAKE_CURRENT_SOURCE_DIR}/web/browser_native_call_gate_library.js
			-sWASMFS
			-sFORCE_FILESYSTEM
			-sASYNCIFY
			-sASYNCIFY_STACK_SIZE=65536
		)
		add_custom_command(TARGET SurrealEngine POST_BUILD
			COMMAND ${CMAKE_COMMAND}
				-DGENERATED_JAVASCRIPT=$<TARGET_FILE:SurrealEngine>
				-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyEmscriptenMemoryGrowth.cmake
			COMMENT "Verifying browser-safe growable WebAssembly memory"
			VERBATIM)
	endif()
	if(SURREAL_GAMEDATA_DIR)
		# Developer convenience only. Production/distributable builds deliberately
		# omit commercial UT99 content and populate /gamedata through the browser
		# import/persistence flow supplied by the browser application.
		target_link_options(SurrealEngine PRIVATE
			--preload-file "${SURREAL_GAMEDATA_DIR}@/gamedata"
		)
	endif()
else()
	install(TARGETS SurrealEngine RUNTIME DESTINATION bin BUNDLE DESTINATION app)
	install(TARGETS SurrealEditor RUNTIME DESTINATION bin BUNDLE DESTINATION app)
	install(TARGETS SurrealDebugger RUNTIME DESTINATION bin)
endif()

if(BUILD_TESTING AND NOT EMSCRIPTEN)
	add_executable(VMCallHookTests SurrealEngine/VM/CallHooksTests.cpp)
	target_link_libraries(VMCallHookTests SurrealCommon ${SURREALCOMMON_LIBS})
	set_target_properties(VMCallHookTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME VMCallHookTests COMMAND VMCallHookTests)

	add_executable(PresentationTests
		Tests/PresentationTests.cpp
		SurrealEngine/Render/ViewFamily.cpp
		SurrealEngine/Math/mat.cpp
	)
	target_include_directories(PresentationTests PRIVATE SurrealEngine)
	set_target_properties(PresentationTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME PresentationTests COMMAND PresentationTests)

	add_executable(VulkanPresentationTargetsTests
		Tests/VulkanPresentationTargetsTests.cpp
		SurrealEngine/RenderDevice/Vulkan/VulkanPresentationTargets.cpp
	)
	target_include_directories(VulkanPresentationTargetsTests PRIVATE SurrealEngine)
	set_target_properties(VulkanPresentationTargetsTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME VulkanPresentationTargetsTests COMMAND VulkanPresentationTargetsTests)

	add_executable(OpenXRViewTests
		Tests/OpenXRViewTests.cpp
		SurrealEngine/Platform/OpenXR/OpenXRView.cpp
		SurrealEngine/XR/XRCommon.cpp
		SurrealEngine/Render/ViewFamily.cpp
		SurrealEngine/Math/mat.cpp
	)
	target_include_directories(OpenXRViewTests PRIVATE SurrealEngine)
	set_target_properties(OpenXRViewTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME OpenXRViewTests COMMAND OpenXRViewTests)

	add_executable(OpenXRExtensionsTests
		Tests/OpenXRExtensionsTests.cpp
		SurrealEngine/Platform/OpenXR/OpenXRExtensions.cpp
	)
	target_include_directories(OpenXRExtensionsTests PRIVATE SurrealEngine)
	set_target_properties(OpenXRExtensionsTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME OpenXRExtensionsTests COMMAND OpenXRExtensionsTests)

	add_executable(WebXRFrameBridgeTests
		Tests/WebXRFrameBridgeTests.cpp
		SurrealEngine/Platform/WebXR/WebXRFrameBridge.cpp
		SurrealEngine/RenderDevice/ClipSpaceConversion.cpp
		SurrealEngine/Math/mat.cpp
	)
	target_include_directories(WebXRFrameBridgeTests PRIVATE SurrealEngine)
	set_target_properties(WebXRFrameBridgeTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME WebXRFrameBridgeTests COMMAND WebXRFrameBridgeTests)

	add_executable(WebXRFramePhaseTests
		Tests/WebXRFramePhaseTests.cpp
		SurrealEngine/Platform/WebXR/WebXRFramePhase.cpp
	)
	target_include_directories(WebXRFramePhaseTests PRIVATE SurrealEngine)
	set_target_properties(WebXRFramePhaseTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME WebXRFramePhaseTests COMMAND WebXRFramePhaseTests)

	add_executable(WebXRUIProviderTests
		Tests/WebXRUIProviderTests.cpp
		SurrealEngine/Platform/WebXR/WebXRUIProvider.cpp
		SurrealEngine/Platform/WebXR/WebXRFrameBridge.cpp
		SurrealEngine/RenderDevice/ClipSpaceConversion.cpp
		SurrealEngine/Render/XRUISurfaces.cpp
		SurrealEngine/Render/XRUISurfaceRuntimeAdapter.cpp
		SurrealEngine/Render/XRUISurfaceEngineBinding.cpp
		SurrealEngine/XR/XRUIRuntime.cpp
		SurrealEngine/XR/XRUIVisuals.cpp
		SurrealEngine/XR/XRHapticFeedbackPolicy.cpp
		SurrealEngine/XR/XRCommon.cpp
		SurrealEngine/Math/mat.cpp
	)
	target_include_directories(WebXRUIProviderTests PRIVATE SurrealEngine)
	set_target_properties(WebXRUIProviderTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME WebXRUIProviderTests COMMAND WebXRUIProviderTests)

	add_executable(XRUISurfaceTests
		Tests/XRUISurfaceTests.cpp
		SurrealEngine/Render/XRUISurfaces.cpp
	)
	target_include_directories(XRUISurfaceTests PRIVATE SurrealEngine)
	set_target_properties(XRUISurfaceTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME XRUISurfaceTests COMMAND XRUISurfaceTests)

	add_executable(WebXRInputBridgeTests
		Tests/WebXRInputBridgeTests.cpp
		SurrealEngine/Platform/WebXR/WebXRInputBridge.cpp
	)
	target_include_directories(WebXRInputBridgeTests PRIVATE SurrealEngine)
	set_target_properties(WebXRInputBridgeTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME WebXRInputBridgeTests COMMAND WebXRInputBridgeTests)
	add_executable(WebXRInputAdapterTests
		Tests/WebXRInputAdapterTests.cpp
		SurrealEngine/Platform/WebXR/WebXRInputAdapter.cpp
		SurrealEngine/Platform/WebXR/WebXRInputBridge.cpp
		SurrealEngine/Input/InputComposition.cpp
	)
	target_include_directories(WebXRInputAdapterTests PRIVATE SurrealEngine)
	set_target_properties(WebXRInputAdapterTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME WebXRInputAdapterTests COMMAND WebXRInputAdapterTests)

	add_executable(WebXRInputRuntimeTests
		Tests/WebXRInputRuntimeTests.cpp
		SurrealEngine/Platform/WebXR/WebXRInputRuntime.cpp
		SurrealEngine/Platform/WebXR/WebXRInputAdapter.cpp
		SurrealEngine/Platform/WebXR/WebXRInputBridge.cpp
		SurrealEngine/Input/InputComposition.cpp
		SurrealEngine/Input/XRInputAdapter.cpp
		SurrealEngine/XR/XRStartupIntroRoute.cpp
	)
	target_include_directories(WebXRInputRuntimeTests PRIVATE SurrealEngine)
	set_target_properties(WebXRInputRuntimeTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME WebXRInputRuntimeTests COMMAND WebXRInputRuntimeTests)

	add_executable(WebXRHapticsTests
		Tests/WebXRHapticsTests.cpp
		SurrealEngine/Platform/WebXR/WebXRHaptics.cpp
		SurrealEngine/XR/XRCommon.cpp
	)
	target_include_directories(WebXRHapticsTests PRIVATE SurrealEngine)
	set_target_properties(WebXRHapticsTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME WebXRHapticsTests COMMAND WebXRHapticsTests)

	add_executable(XRHapticFeedbackPolicyTests
		Tests/XRHapticFeedbackPolicyTests.cpp
		SurrealEngine/XR/XRHapticFeedbackPolicy.cpp
		SurrealEngine/XR/XRCommon.cpp
	)
	target_include_directories(XRHapticFeedbackPolicyTests PRIVATE SurrealEngine)
	set_target_properties(XRHapticFeedbackPolicyTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME XRHapticFeedbackPolicyTests COMMAND XRHapticFeedbackPolicyTests)

	add_executable(XRUISurfaceRuntimeAdapterTests
		Tests/XRUISurfaceRuntimeAdapterTests.cpp
		SurrealEngine/Render/XRUISurfaces.cpp
		SurrealEngine/Render/XRUISurfaceRuntimeAdapter.cpp
	)
	target_include_directories(XRUISurfaceRuntimeAdapterTests PRIVATE SurrealEngine)
	set_target_properties(XRUISurfaceRuntimeAdapterTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME XRUISurfaceRuntimeAdapterTests COMMAND XRUISurfaceRuntimeAdapterTests)

	add_executable(XRUISurfaceEngineBindingTests
		Tests/XRUISurfaceEngineBindingTests.cpp
		SurrealEngine/Render/XRUISurfaces.cpp
		SurrealEngine/Render/XRUISurfaceRuntimeAdapter.cpp
		SurrealEngine/Render/XRUISurfaceEngineBinding.cpp
	)
	target_include_directories(XRUISurfaceEngineBindingTests PRIVATE SurrealEngine)
	set_target_properties(XRUISurfaceEngineBindingTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME XRUISurfaceEngineBindingTests COMMAND XRUISurfaceEngineBindingTests)

	add_executable(OpenXRUIRuntimeTests
		Tests/OpenXRUIRuntimeTests.cpp
		SurrealEngine/Platform/OpenXR/OpenXRUIRuntime.cpp
		SurrealEngine/XR/XRUIRuntime.cpp
		SurrealEngine/XR/XRUIVisuals.cpp
		SurrealEngine/XR/XRHapticFeedbackPolicy.cpp
		SurrealEngine/XR/XRCommon.cpp
		SurrealEngine/XR/XRStartupIntroRoute.cpp
		SurrealEngine/Render/XRUISurfaces.cpp
		SurrealEngine/Render/XRUISurfaceRuntimeAdapter.cpp
		SurrealEngine/Render/XRUISurfaceEngineBinding.cpp
		SurrealEngine/Math/mat.cpp
	)
	target_include_directories(OpenXRUIRuntimeTests PRIVATE SurrealEngine)
	set_target_properties(OpenXRUIRuntimeTests PROPERTIES CXX_STANDARD 20)
	add_test(NAME OpenXRUIRuntimeTests COMMAND OpenXRUIRuntimeTests)
endif()
