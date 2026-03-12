// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;

public class CyberninjaVREditorTarget : TargetRules
{
	public CyberninjaVREditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		//WindowsPlatform.bEnableAddressSanitizer = true;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		if (StaticAnalyzer == StaticAnalyzer.PVSStudio)
		{
			bUseUnityBuild = false;
		}

		ExtraModuleNames.AddRange( new string[] { "CyberninjaVR" } );
	}
}
