#include "MobileSurfaceNavigationQuery.h"

#include "MobileSurfacePathfinder.h"

namespace MobileSurfaceNavigation::Query
{
	static bool ComputeBarycentrics(
		const FVector& Point,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		double& OutU,
		double& OutV,
		double& OutW)
	{
		const FVector V0 = B - A;
		const FVector V1 = C - A;
		const FVector V2 = Point - A;

		const double D00 = FVector::DotProduct(V0, V0);
		const double D01 = FVector::DotProduct(V0, V1);
		const double D11 = FVector::DotProduct(V1, V1);
		const double D20 = FVector::DotProduct(V2, V0);
		const double D21 = FVector::DotProduct(V2, V1);
		const double Denom = D00 * D11 - D01 * D01;
		if (FMath::IsNearlyZero(Denom))
		{
			return false;
		}

		OutV = (D11 * D20 - D01 * D21) / Denom;
		OutW = (D00 * D21 - D01 * D20) / Denom;
		OutU = 1.0 - OutV - OutW;
		return true;
	}

	static double DistanceSquaredToTriangle(
		const FVector& Point,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& TriangleNormal)
	{
		const double SignedDistance = FVector::DotProduct(Point - A, TriangleNormal);
		const FVector ProjectedPoint = Point - TriangleNormal * SignedDistance;

		double U, V, W;
		if (ComputeBarycentrics(ProjectedPoint, A, B, C, U, V, W) &&
			U >= 0.0 && V >= 0.0 && W >= 0.0)
		{
			return FMath::Square(SignedDistance);
		}

		const double DistAB = FMath::PointDistToSegmentSquared(Point, A, B);
		const double DistBC = FMath::PointDistToSegmentSquared(Point, B, C);
		const double DistCA = FMath::PointDistToSegmentSquared(Point, C, A);
		return FMath::Min3(DistAB, DistBC, DistCA);
	}
}

int32 FMobileSurfaceNavigationQuery::FindContainingTriangle(const FMobileSurfaceNavData& NavData, const FVector& LocalPosition)
{
	for (int32 TriangleIndex = 0; TriangleIndex < NavData.Triangles.Num(); ++TriangleIndex)
	{
		if (NavData.TriangleBounds.IsValidIndex(TriangleIndex) &&
			!NavData.TriangleBounds[TriangleIndex].LocalBounds.ExpandBy(1.0).IsInsideOrOn(LocalPosition))
		{
			continue;
		}

		const FMobileSurfaceNavTriangle& Triangle = NavData.Triangles[TriangleIndex];
		const FVector A = NavData.Vertices[Triangle.VertexIndices.X].LocalPosition;
		const FVector B = NavData.Vertices[Triangle.VertexIndices.Y].LocalPosition;
		const FVector C = NavData.Vertices[Triangle.VertexIndices.Z].LocalPosition;

		const double SignedDistance = FVector::DotProduct(LocalPosition - A, Triangle.Normal);
		if (FMath::Abs(SignedDistance) > 1.0)
		{
			continue;
		}

		double U, V, W;
		if (MobileSurfaceNavigation::Query::ComputeBarycentrics(LocalPosition - Triangle.Normal * SignedDistance, A, B, C, U, V, W) &&
			U >= -KINDA_SMALL_NUMBER && V >= -KINDA_SMALL_NUMBER && W >= -KINDA_SMALL_NUMBER)
		{
			return TriangleIndex;
		}
	}

	return INDEX_NONE;
}

int32 FMobileSurfaceNavigationQuery::FindNearestTriangle(const FMobileSurfaceNavData& NavData, const FVector& LocalPosition)
{
	int32 BestTriangleIndex = INDEX_NONE;
	double BestDistanceSquared = TNumericLimits<double>::Max();

	for (int32 TriangleIndex = 0; TriangleIndex < NavData.Triangles.Num(); ++TriangleIndex)
	{
		if (NavData.TriangleBounds.IsValidIndex(TriangleIndex))
		{
			const double BoundsDistanceSquared = NavData.TriangleBounds[TriangleIndex].LocalBounds.ComputeSquaredDistanceToPoint(LocalPosition);
			if (BoundsDistanceSquared > BestDistanceSquared)
			{
				continue;
			}
		}

		const FMobileSurfaceNavTriangle& Triangle = NavData.Triangles[TriangleIndex];
		const FVector A = NavData.Vertices[Triangle.VertexIndices.X].LocalPosition;
		const FVector B = NavData.Vertices[Triangle.VertexIndices.Y].LocalPosition;
		const FVector C = NavData.Vertices[Triangle.VertexIndices.Z].LocalPosition;
		const double DistanceSquared = MobileSurfaceNavigation::Query::DistanceSquaredToTriangle(LocalPosition, A, B, C, Triangle.Normal);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestTriangleIndex = TriangleIndex;
		}
	}

	return BestTriangleIndex;
}

bool FMobileSurfaceNavigationQuery::FindPath(
	const FMobileSurfaceNavData& NavData,
	const FVector& StartLocalPosition,
	const FVector& EndLocalPosition,
	FMobileSurfaceNavPath& OutPath,
	const float AgentRadius)
{
	FMobileSurfacePathQueryParams Params;
	Params.AgentRadius = AgentRadius;
	return FMobileSurfacePathfinder::FindPath(NavData, StartLocalPosition, EndLocalPosition, Params, OutPath);
}
