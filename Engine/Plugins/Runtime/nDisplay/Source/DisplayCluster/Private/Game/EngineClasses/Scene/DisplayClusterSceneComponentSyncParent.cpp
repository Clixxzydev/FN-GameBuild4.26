// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterSceneComponentSyncParent.h"
#include "GameFramework/Actor.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"


UDisplayClusterSceneComponentSyncParent::UDisplayClusterSceneComponentSyncParent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Children of UDisplayClusterSceneComponent must always Tick to be able to process VRPN tracking
	PrimaryComponentTick.bCanEverTick = true;
}

void UDisplayClusterSceneComponentSyncParent::BeginPlay()
{
	Super::BeginPlay();
}


void UDisplayClusterSceneComponentSyncParent::TickComponent( float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent( DeltaTime, TickType, ThisTickFunction );
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterClusterSyncObject
//////////////////////////////////////////////////////////////////////////////////////////////
bool UDisplayClusterSceneComponentSyncParent::IsDirty() const
{
	USceneComponent* const pParent = GetAttachParent();
	if (pParent && !pParent->IsPendingKill())
	{
		const bool bIsDirty = (LastSyncLoc != pParent->GetRelativeLocation() || LastSyncRot != pParent->GetRelativeRotation() || LastSyncScale != pParent->GetRelativeScale3D());
		UE_LOG(LogDisplayClusterGame, Verbose, TEXT("SYNC_PARENT: %s dirty state is %s"), *GetSyncId(), *DisplayClusterHelpers::str::BoolToStr(bIsDirty));
		return bIsDirty;
	}

	return false;
}

void UDisplayClusterSceneComponentSyncParent::ClearDirty()
{
	USceneComponent* const pParent = GetAttachParent();
	if (pParent && !pParent->IsPendingKill())
	{
		LastSyncLoc   = pParent->GetRelativeLocation();
		LastSyncRot   = pParent->GetRelativeRotation();
		LastSyncScale = pParent->GetRelativeScale3D();
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// UDisplayClusterSceneComponentSync
//////////////////////////////////////////////////////////////////////////////////////////////
FString UDisplayClusterSceneComponentSyncParent::GenerateSyncId()
{
	return FString::Printf(TEXT("SP_%s.%s"), *GetOwner()->GetName(), *GetAttachParent()->GetName());
}

FTransform UDisplayClusterSceneComponentSyncParent::GetSyncTransform() const
{
	return GetAttachParent()->GetRelativeTransform();
}

void UDisplayClusterSceneComponentSyncParent::SetSyncTransform(const FTransform& t)
{
	GetAttachParent()->SetRelativeTransform(t);
}
