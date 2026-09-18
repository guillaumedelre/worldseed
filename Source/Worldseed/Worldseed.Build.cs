// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Worldseed : ModuleRules
{
	public Worldseed(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"ProceduralMeshComponent",
			// Marching cubes et structures de grille, pour le terrain voxel.
			// C'est un module d'EXECUTION -- Engine et Chaos en dependent --
			// donc rien a activer, pas de plugin, et cela part en build final.
			"GeometryCore",
			// Ocean : vagues, caustiques, rendu sous-marin, flottabilite et
			// nage, qu'un maillage nu n'aura jamais. C'est le seul corps d'eau
			// du monde depuis le retrait de l'hydrologie, le 18 septembre 2026.
			"Water",
			"SlateCore",
			"Json"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Worldseed",
			"Worldseed/Procedural",
			"Worldseed/Variant_Platforming",
			"Worldseed/Variant_Platforming/Animation",
			"Worldseed/Variant_Combat",
			"Worldseed/Variant_Combat/AI",
			"Worldseed/Variant_Combat/Animation",
			"Worldseed/Variant_Combat/Gameplay",
			"Worldseed/Variant_Combat/Interfaces",
			"Worldseed/Variant_Combat/UI",
			"Worldseed/Variant_SideScrolling",
			"Worldseed/Variant_SideScrolling/AI",
			"Worldseed/Variant_SideScrolling/Gameplay",
			"Worldseed/Variant_SideScrolling/Interfaces",
			"Worldseed/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
