// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;
using System.Linq;

public class CyberninjaVRTarget : TargetRules
{
	public CyberninjaVRTarget(TargetInfo Target) : base(Target)
	{
		
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		// BuildEnvironment = TargetBuildEnvironment.Unique;
		// bOverrideBuildEnvironment = true;
		// bUseLoggingInShipping = true;
		// bUseChecksInShipping = true;
		
		ExtraModuleNames.AddRange( new string[] { "CyberninjaVR" } );
	}
}
