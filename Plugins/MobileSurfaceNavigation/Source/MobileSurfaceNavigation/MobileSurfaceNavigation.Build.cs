using UnrealBuildTool;

public class MobileSurfaceNavigation : ModuleRules
{
	public MobileSurfaceNavigation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"GeometryCore"
			});

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"RenderCore",
				"RHI"
			});
	}
}
