#include "BotBenchmarkQualityObservation.h"

#include <cmath>
#include <unordered_map>
#include <utility>

namespace BotBenchmarkQualityObservation
{
	namespace
	{
		bool IsFiniteNonNegative(float value)
		{
			return std::isfinite(value) && value >= 0.0f;
		}

		bool IsValidPosition(const NavigationPosition& position)
		{
			return std::isfinite(position.X) && std::isfinite(position.Y)
				&& std::isfinite(position.Z);
		}

		bool IsValidNode(const NavigationCoverageNode& node)
		{
			return !node.StableId.empty() && IsValidPosition(node.Position)
				&& IsFiniteNonNegative(node.CollisionRadius)
				&& IsFiniteNonNegative(node.CollisionHeight);
		}

		bool IsValidObserver(const NavigationObserver& observer)
		{
			return IsValidPosition(observer.Position)
				&& IsFiniteNonNegative(observer.CollisionRadius)
				&& IsFiniteNonNegative(observer.CollisionHeight);
		}
	}

	PickupTransitionEvidence ClassifyPickupTransition(const PickupTransitionInput& input)
	{
		if (!input.SourceWasUnowned)
			return PickupTransitionEvidence::NotConfirmed;
		if (input.CollectorOwnsSourceAfterTouch)
			return PickupTransitionEvidence::OwnershipTransferred;
		if (input.SourceDestroyedAfterTouch)
			return PickupTransitionEvidence::SourceConsumedUnconfirmed;
		return PickupTransitionEvidence::NotConfirmed;
	}

	bool IsConfirmedPickupAcquisition(PickupTransitionEvidence evidence)
	{
		return evidence == PickupTransitionEvidence::OwnershipTransferred;
	}

	NavigationCoverageCatalog BuildNavigationCoverageCatalog(
		std::vector<NavigationCoverageNode> nodes, size_t maximumNodes)
	{
		NavigationCoverageCatalog catalog;
		if (nodes.empty())
		{
			catalog.Failure = NavigationCoverageCatalogFailure::Empty;
			return catalog;
		}
		if (maximumNodes == 0 || nodes.size() > maximumNodes)
		{
			catalog.Failure = NavigationCoverageCatalogFailure::CapacityExceeded;
			return catalog;
		}

		std::unordered_map<std::string, size_t> seenIds;
		seenIds.reserve(nodes.size());
		for (const auto& node : nodes)
		{
			if (node.StableId.empty())
			{
				catalog.Failure = NavigationCoverageCatalogFailure::EmptyStableId;
				return catalog;
			}
			if (!IsValidNode(node))
			{
				catalog.Failure = NavigationCoverageCatalogFailure::InvalidNodeGeometry;
				return catalog;
			}
			if (!seenIds.emplace(node.StableId, seenIds.size()).second)
			{
				catalog.Failure = NavigationCoverageCatalogFailure::DuplicateStableId;
				return catalog;
			}
		}

		catalog.Nodes = std::move(nodes);
		catalog.Failure = NavigationCoverageCatalogFailure::None;
		return catalog;
	}

	bool IsNavigationNodeProximate(const NavigationObserver& observer,
		const NavigationCoverageNode& node)
	{
		if (!IsValidObserver(observer) || !IsValidNode(node))
			return false;

		const double horizontalRadius = static_cast<double>(observer.CollisionRadius)
			+ static_cast<double>(node.CollisionRadius);
		const double verticalHeight = static_cast<double>(observer.CollisionHeight)
			+ static_cast<double>(node.CollisionHeight);
		if (!std::isfinite(horizontalRadius) || !std::isfinite(verticalHeight))
			return false;

		const double dx = static_cast<double>(observer.Position.X) - node.Position.X;
		const double dy = static_cast<double>(observer.Position.Y) - node.Position.Y;
		const double dz = static_cast<double>(observer.Position.Z) - node.Position.Z;
		return dx * dx + dy * dy <= horizontalRadius * horizontalRadius
			&& std::abs(dz) <= verticalHeight;
	}

	NavigationCoverageAccumulator::NavigationCoverageAccumulator(NavigationCoverageCatalog catalog)
		: Catalog(std::move(catalog))
	{
		if (Catalog.IsValid())
			Visited.resize(Catalog.Nodes.size());
	}

	NavigationVisitResult NavigationCoverageAccumulator::MarkVisited(std::string_view stableId)
	{
		if (!Catalog.IsValid())
			return NavigationVisitResult::InvalidCatalog;
		for (size_t index = 0; index < Catalog.Nodes.size(); index++)
		{
			if (Catalog.Nodes[index].StableId != stableId)
				continue;
			if (Visited[index])
				return NavigationVisitResult::AlreadyVisited;
			Visited[index] = true;
			UniqueVisited++;
			return NavigationVisitResult::NewlyVisited;
		}
		return NavigationVisitResult::UnknownStableId;
	}

	NavigationCoverageUpdate NavigationCoverageAccumulator::Observe(const NavigationObserver& observer)
	{
		NavigationCoverageUpdate update;
		if (!Catalog.IsValid() || !IsValidObserver(observer))
			return update;

		update.ObservationValid = true;
		for (const auto& node : Catalog.Nodes)
		{
			if (!IsNavigationNodeProximate(observer, node))
				continue;
			update.MatchingNodeCount++;
			if (MarkVisited(node.StableId) == NavigationVisitResult::NewlyVisited)
				update.NewlyVisitedNodeCount++;
		}
		update.UniqueVisitedNodeCount = UniqueVisited;
		return update;
	}
}
