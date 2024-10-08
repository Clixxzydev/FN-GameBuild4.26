// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterCameraComponent.h"
#include "Config/DisplayClusterConfigTypes.h"

#include "Camera/CameraComponent.h"

#include "CoreGlobals.h"


UDisplayClusterCameraComponent::UDisplayClusterCameraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, EyeDist(0.064f)
	, bEyeSwap(false)
	, ForceEyeOffset(0)
{
	// Children of UDisplayClusterSceneComponent must always Tick to be able to process VRPN tracking
	PrimaryComponentTick.bCanEverTick = true;
}

void UDisplayClusterCameraComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UDisplayClusterCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UDisplayClusterCameraComponent::SetSettings(const FDisplayClusterConfigSceneNode* ConfigData)
{
	check(ConfigData);
	if (ConfigData)
	{
		const FDisplayClusterConfigCamera* const CameraCfg = static_cast<const FDisplayClusterConfigCamera*>(ConfigData);

		EyeDist        = CameraCfg->EyeDist;
		bEyeSwap       = CameraCfg->EyeSwap;
		ForceEyeOffset = CameraCfg->ForceOffset;
	}

	Super::SetSettings(ConfigData);
}

bool UDisplayClusterCameraComponent::ApplySettings()
{
	return Super::ApplySettings();
}
