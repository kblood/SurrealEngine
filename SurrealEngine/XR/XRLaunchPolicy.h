#pragma once

// Command-line overrides are explicit and deterministic: --no-openxr wins if
// both are supplied, otherwise --openxr wins over the persisted launcher value.
bool ResolveOpenXRLaunchRequest(bool launcherEnabled, bool commandLineEnable,
	bool commandLineDisable);

// Avatar sampling is inactive unless either the player-facing option or the
// separate developer diagnostics path is explicitly enabled.
bool ShouldUpdateXRAvatar(bool avatarEnabled, bool diagnosticsEnabled);
