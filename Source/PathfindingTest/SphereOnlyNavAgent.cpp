#include "SphereOnlyNavAgent.h"

ASphereOnlyNavAgent::ASphereOnlyNavAgent()
{
	SetStateLabelEnabled(false);
	PrimaryActorTick.bCanEverTick = false;
	SetActorTickEnabled(false);
}
