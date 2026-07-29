#include "BotBenchmark/BotBenchmarkQualityObservation.h"

#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace BotBenchmarkQualityObservation;

static int Failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		Failures++;
	}
}

static NavigationCoverageNode Node(std::string id, float x, float y, float z,
	float radius = 20.0f, float height = 30.0f)
{
	return { std::move(id), { x, y, z }, radius, height };
}

static NavigationObserver Observer(float x, float y, float z,
	float radius = 34.0f, float height = 39.0f)
{
	return { { x, y, z }, radius, height };
}

static void TestPickupTransitionEvidenceDoesNotOverclaimAcquisition()
{
	Check(ClassifyPickupTransition({
		.SourceWasUnowned = true,
		.CollectorOwnsSourceAfterTouch = true
	}) == PickupTransitionEvidence::OwnershipTransferred,
		"an unowned source transferred to the collector is confirmed");
	Check(IsConfirmedPickupAcquisition(PickupTransitionEvidence::OwnershipTransferred),
		"only ownership transfer is an acquisition");

	const auto consumed = ClassifyPickupTransition({
		.SourceWasUnowned = true,
		.SourceDestroyedAfterTouch = true
	});
	Check(consumed == PickupTransitionEvidence::SourceConsumedUnconfirmed,
		"destruction after touching an unowned source remains unconfirmed");
	Check(!IsConfirmedPickupAcquisition(consumed),
		"a consumed source is never counted as an acquisition");

	Check(ClassifyPickupTransition({
		.SourceWasUnowned = false,
		.CollectorOwnsSourceAfterTouch = true,
		.SourceDestroyedAfterTouch = true
	}) == PickupTransitionEvidence::NotConfirmed,
		"a source that was already owned cannot prove a world pickup");
}

static void TestNavigationProximityUsesClosedCollisionCylinder()
{
	const auto node = Node("node", 0.0f, 0.0f, 0.0f);
	Check(IsNavigationNodeProximate(Observer(54.0f, 0.0f, 69.0f), node),
		"the exact horizontal and vertical collision boundary is included");
	Check(!IsNavigationNodeProximate(Observer(54.01f, 0.0f, 0.0f), node),
		"a point just outside the horizontal collision boundary is excluded");
	Check(!IsNavigationNodeProximate(Observer(0.0f, 0.0f, 69.01f), node),
		"a point just outside the vertical collision boundary is excluded");
	Check(IsNavigationNodeProximate(Observer(38.0f, 38.0f, 0.0f), node),
		"diagonal distance is evaluated as a circle rather than a square");
}

static void TestInvalidGeometryFailsClosed()
{
	const auto node = Node("node", 0.0f, 0.0f, 0.0f);
	Check(!IsNavigationNodeProximate(Observer(
		std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f), node),
		"non-finite observer positions fail closed");
	Check(!IsNavigationNodeProximate(Observer(0.0f, 0.0f, 0.0f, -1.0f), node),
		"negative observer radii fail closed");
	Check(!IsNavigationNodeProximate(Observer(0.0f, 0.0f, 0.0f),
		Node("node", 0.0f, 0.0f, 0.0f, 20.0f,
			std::numeric_limits<float>::infinity())),
		"non-finite node geometry fails closed");
}

static void TestCatalogValidationAndBoundedCoverage()
{
	Check(BuildNavigationCoverageCatalog({}).Failure
		== NavigationCoverageCatalogFailure::Empty,
		"an empty catalog is rejected");
	Check(BuildNavigationCoverageCatalog({ Node("", 0.0f, 0.0f, 0.0f) }).Failure
		== NavigationCoverageCatalogFailure::EmptyStableId,
		"an empty stable identifier is rejected");
	Check(BuildNavigationCoverageCatalog({
		Node("same", 0.0f, 0.0f, 0.0f), Node("same", 100.0f, 0.0f, 0.0f)
	}).Failure == NavigationCoverageCatalogFailure::DuplicateStableId,
		"duplicate stable identifiers are rejected");
	Check(BuildNavigationCoverageCatalog({
		Node("a", 0.0f, 0.0f, 0.0f), Node("b", 100.0f, 0.0f, 0.0f)
	}, 1).Failure == NavigationCoverageCatalogFailure::CapacityExceeded,
		"catalogs above their explicit capacity are rejected");

	auto catalog = BuildNavigationCoverageCatalog({
		Node("alpha", 0.0f, 0.0f, 0.0f),
		Node("beta", 100.0f, 0.0f, 0.0f),
		Node("gamma", 200.0f, 0.0f, 0.0f)
	});
	NavigationCoverageAccumulator coverage(std::move(catalog));
	Check(coverage.IsCatalogValid() && coverage.CatalogNodeCount() == 3,
		"a finite unique catalog initializes coverage");
	Check(coverage.MarkVisited("unknown") == NavigationVisitResult::UnknownStableId,
		"unknown stable identifiers cannot alter coverage");
	Check(coverage.MarkVisited("alpha") == NavigationVisitResult::NewlyVisited,
		"a known identifier records its first visit");
	Check(coverage.MarkVisited("alpha") == NavigationVisitResult::AlreadyVisited,
		"a repeated known identifier cannot inflate coverage");
	Check(coverage.UniqueVisitedNodeCount() == 1,
		"explicit visit deduplication preserves exact unique count");

	const auto betaVisit = coverage.Observe(Observer(100.0f, 0.0f, 0.0f));
	Check(betaVisit.ObservationValid && betaVisit.MatchingNodeCount == 1
		&& betaVisit.NewlyVisitedNodeCount == 1 && betaVisit.UniqueVisitedNodeCount == 2,
		"a proximity observation records only the newly reached node");
	const auto repeat = coverage.Observe(Observer(100.0f, 0.0f, 0.0f));
	Check(repeat.ObservationValid && repeat.MatchingNodeCount == 1
		&& repeat.NewlyVisitedNodeCount == 0 && repeat.UniqueVisitedNodeCount == 2,
		"a repeated proximity observation cannot inflate coverage");
	const auto invalid = coverage.Observe(Observer(
		std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f));
	Check(!invalid.ObservationValid && invalid.UniqueVisitedNodeCount == 0,
		"an invalid observation does not report a fabricated coverage total");
}

int main()
{
	TestPickupTransitionEvidenceDoesNotOverclaimAcquisition();
	TestNavigationProximityUsesClosedCollisionCylinder();
	TestInvalidGeometryFailsClosed();
	TestCatalogValidationAndBoundedCoverage();
	if (Failures == 0)
		std::cout << "Bot benchmark quality observation tests passed\n";
	return Failures == 0 ? 0 : 1;
}
