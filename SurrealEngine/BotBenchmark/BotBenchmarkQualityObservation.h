#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace BotBenchmarkQualityObservation
{
	constexpr size_t MaximumNavigationCoverageNodes = 4096;

	enum class PickupTransitionEvidence
	{
		NotConfirmed,
		OwnershipTransferred,
		SourceConsumedUnconfirmed
	};

	struct PickupTransitionInput
	{
		bool SourceWasUnowned = false;
		bool CollectorOwnsSourceAfterTouch = false;
		bool SourceDestroyedAfterTouch = false;
	};

	PickupTransitionEvidence ClassifyPickupTransition(const PickupTransitionInput& input);
	bool IsConfirmedPickupAcquisition(PickupTransitionEvidence evidence);

	struct NavigationPosition
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
	};

	struct NavigationCoverageNode
	{
		std::string StableId;
		NavigationPosition Position;
		float CollisionRadius = 0.0f;
		float CollisionHeight = 0.0f;
	};

	struct NavigationObserver
	{
		NavigationPosition Position;
		float CollisionRadius = 0.0f;
		float CollisionHeight = 0.0f;
	};

	enum class NavigationCoverageCatalogFailure
	{
		None,
		Empty,
		EmptyStableId,
		DuplicateStableId,
		InvalidNodeGeometry,
		CapacityExceeded
	};

	struct NavigationCoverageCatalog
	{
		std::vector<NavigationCoverageNode> Nodes;
		NavigationCoverageCatalogFailure Failure = NavigationCoverageCatalogFailure::Empty;

		bool IsValid() const { return Failure == NavigationCoverageCatalogFailure::None; }
	};

	NavigationCoverageCatalog BuildNavigationCoverageCatalog(
		std::vector<NavigationCoverageNode> nodes,
		size_t maximumNodes = MaximumNavigationCoverageNodes);
	bool IsNavigationNodeProximate(const NavigationObserver& observer,
		const NavigationCoverageNode& node);

	enum class NavigationVisitResult
	{
		InvalidCatalog,
		UnknownStableId,
		AlreadyVisited,
		NewlyVisited
	};

	struct NavigationCoverageUpdate
	{
		bool ObservationValid = false;
		size_t MatchingNodeCount = 0;
		size_t NewlyVisitedNodeCount = 0;
		size_t UniqueVisitedNodeCount = 0;
	};

	class NavigationCoverageAccumulator
	{
	public:
		explicit NavigationCoverageAccumulator(NavigationCoverageCatalog catalog);

		bool IsCatalogValid() const { return Catalog.IsValid(); }
		size_t CatalogNodeCount() const { return Catalog.Nodes.size(); }
		size_t UniqueVisitedNodeCount() const { return UniqueVisited; }
		NavigationVisitResult MarkVisited(std::string_view stableId);
		NavigationCoverageUpdate Observe(const NavigationObserver& observer);

	private:
		NavigationCoverageCatalog Catalog;
		std::vector<bool> Visited;
		size_t UniqueVisited = 0;
	};
}
