#include "Precomp.h"
#include "GameSupport.h"
#include "GameFolder.h"
#include "Native/N227Emitter.h"
#include "Native/N227Projector.h"
#include "Native/NBorderWindow.h"
#include "Native/NButtonWindow.h"
#include "Native/NCheckboxWindow.h"
#include "Native/NClipWindow.h"
#include "Native/NComputerWindow.h"
#include "Native/NConEvent.h"
#include "Native/NConEventRandomLabel.h"
#include "Native/NConversation.h"
#include "Native/NDebugInfo.h"
#include "Native/NDeusExDecoration.h"
#include "Native/NDeusExPlayer.h"
#include "Native/NDeusExSaveInfo.h"
#include "Native/NDeusExTextParser.h"
#include "Native/NDumpLocation.h"
#include "Native/NEditWindow.h"
#include "Native/NExtensionObject.h"
#include "Native/NExtString.h"
#include "Native/NFlagBase.h"
#include "Native/NGC.h"
#include "Native/NGameDirectory.h"
#include "Native/NLargeTextWindow.h"
#include "Native/NListWindow.h"
#include "Native/NModalWindow.h"
#include "Native/NParticleIterator.h"
#include "Native/NPlayerPawnExt.h"
#include "Native/NRadioBoxWindow.h"
#include "Native/NRootWindow.h"
#include "Native/NScaleManagerWindow.h"
#include "Native/NScaleWindow.h"
#include "Native/NScriptedPawn.h"
#include "Native/NScrollAreaWindow.h"
#include "Native/NTextLogWindow.h"
#include "Native/NTextWindow.h"
#include "Native/NTileWindow.h"
#include "Native/NTimeDemo.h"
#include "Native/NToggleWindow.h"
#include "Native/NUPakPathNodeIterator.h"
#include "Native/NUPakPawnPathNodeIterator.h"
#include "Native/NViewportWindow.h"
#include "Native/NWebRequest.h"
#include "Native/NWebResponse.h"
#include "Native/NWindow.h"

namespace
{
	void RegisterUnrealNativeFunctions(const GameLaunchInfo& launchInfo)
	{
		NUPakPathNodeIterator::RegisterFunctions();
		NUPakPawnPathNodeIterator::RegisterFunctions();

		if (launchInfo.IsUnreal1_227())
		{
			NXParticleEmitter::RegisterFunctions();
			NXEmitter::RegisterFunctions();
			N227Projector::RegisterFunctions();
		}
	}

	void RegisterDeusExNativeFunctions(const GameLaunchInfo&)
	{
		NDebugInfo::RegisterFunctions();
		NDeusExDecoration::RegisterFunctions();
		NDeusExPlayer::RegisterFunctions();
		NDeusExSaveInfo::RegisterFunctions();
		NDumpLocation::RegisterFunctions();
		NGameDirectory::RegisterFunctions();
		NParticleIterator::RegisterFunctions();
		NScriptedPawn::RegisterFunctions();
		NPlayerPawnExt::RegisterFunctions();
		NBorderWindow::RegisterFunctions();
		NButtonWindow::RegisterFunctions();
		NCheckboxWindow::RegisterFunctions();
		NClipWindow::RegisterFunctions();
		NComputerWindow::RegisterFunctions();
		NConEvent::RegisterFunctions();
		NConEventRandomLabel::RegisterFunctions();
		NConversation::RegisterFunctions();
		NDeusExTextParser::RegisterFunctions();
		NEditWindow::RegisterFunctions();
		NExtensionObject::RegisterFunctions();
		NExtString::RegisterFunctions();
		NFlagBase::RegisterFunctions();
		NGC::RegisterFunctions();
		NLargeTextWindow::RegisterFunctions();
		NListWindow::RegisterFunctions();
		NModalWindow::RegisterFunctions();
		NRadioBoxWindow::RegisterFunctions();
		NRootWindow::RegisterFunctions();
		NScaleManagerWindow::RegisterFunctions();
		NScaleWindow::RegisterFunctions();
		NScrollAreaWindow::RegisterFunctions();
		NTextLogWindow::RegisterFunctions();
		NTextWindow::RegisterFunctions();
		NTileWindow::RegisterFunctions();
		NTimeDemo::RegisterFunctions();
		NToggleWindow::RegisterFunctions();
		NViewportWindow::RegisterFunctions();
		NWebRequest::RegisterFunctions();
		NWebResponse::RegisterFunctions();
		NWindow::RegisterFunctions();
	}
}

bool GameSupport::HasCapability(GameCapability capability) const
{
	return (capabilities & static_cast<uint32_t>(capability)) != 0;
}

void GameSupport::RegisterNativeFunctions(const GameLaunchInfo& launchInfo) const
{
	if (registerNativeFunctions)
		registerNativeFunctions(launchInfo);
}

const GameSupport& GameSupportRegistry::Find(std::string_view executableName)
{
	static constexpr uint32_t deusExCapabilities =
		static_cast<uint32_t>(GameCapability::SaveInfoPackages) |
		static_cast<uint32_t>(GameCapability::PostPostBeginPlayEvent);

	static const GameSupport unknown(GameId::Unknown, {}, 0, nullptr);
	static const GameSupport games[] =
	{
		{ GameId::Unreal, "Unreal", 0, RegisterUnrealNativeFunctions },
		{ GameId::UnrealTournament, "UnrealTournament", 0, nullptr },
		{ GameId::DeusEx, "DeusEx", deusExCapabilities, RegisterDeusExNativeFunctions }
	};

	for (const GameSupport& game : games)
	{
		if (game.ExecutableName() == executableName)
			return game;
	}

	return unknown;
}
