//=============================================================================
// GlobalTickProbeChild
//
// Declares nothing of its own. Deus Ex's pawns are in this position: Terrorist
// extends HumanMilitary extends ScriptedPawn, and only ScriptedPawn declares
// Tick. If an inherited Tick is dispatched to the parent but not to the child,
// the difference is in how the class is decided to handle the probe, not in
// the script.
//=============================================================================
class GlobalTickProbeChild extends GlobalTickProbe;
