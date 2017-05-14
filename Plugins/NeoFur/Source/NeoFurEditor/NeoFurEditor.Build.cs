using UnrealBuildTool;

public class NeoFurEditor : ModuleRules
{
	public NeoFurEditor(TargetInfo Target)
	{
	    
		PublicIncludePaths.AddRange(
			new string[] { "NeoFurEditor/Public" });


		PrivateIncludePaths.AddRange(
			new string[] { "NeoFurEditor/Private" });


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core", "CoreUObject", "Engine", "InputCore", "RHI", "Renderer", "RenderCore", "NeoFur"
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject", "Engine", "Slate", "SlateCore",
                "UnrealEd", "NeoFur"
			}
			);
			
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
	}
}




