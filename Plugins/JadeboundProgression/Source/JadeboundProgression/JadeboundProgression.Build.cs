using UnrealBuildTool;

public class JadeboundProgression : ModuleRules
{
    public JadeboundProgression(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore", "UMG", "InputCore", "EnhancedInput", "Json", "JsonUtilities", "NavigationSystem" });
    }
}
