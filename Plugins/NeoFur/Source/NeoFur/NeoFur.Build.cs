using UnrealBuildTool;
using System;
using System.IO;

public class NeoFur : ModuleRules
{
    public NeoFur(TargetInfo Target)
    {
        
        PublicIncludePaths.AddRange(
            new string[] {
                "NeoFur/Public"
            }
            );


        PrivateIncludePaths.AddRange(
            new string[] {
                "NeoFur/Private",
            }
            );


        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core", "CoreUObject", "Engine", "InputCore", "RHI", "Renderer", "RenderCore", "ShaderCore"
            }
            );

            
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core", "CoreUObject", "Engine", "Slate", "SlateCore"
            }
            );

        if(UEBuildConfiguration.bBuildEditor) {
            PublicDependencyModuleNames.Add("FBX");
            Definitions.Add("NEOFUR_FBX=1");
        }
        
        // FIXME: Hide this from Pro versions.
        if (UEBuildConfiguration.bBuildEditor == true) {
            PrivateDependencyModuleNames.Add("Http");
        }
    }
}




