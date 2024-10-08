// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "Components/SkeletalMeshComponent.h"


struct FSkeletalMeshEditorParams
{
	void SaveState(USkeletalMeshComponent* InSkelMeshComp)
	{
		if (InSkelMeshComp)
		{
			ChildSkelMesh = InSkelMeshComp;
			VisibilityBasedAnimTickOption = InSkelMeshComp->VisibilityBasedAnimTickOption;
			InSkelMeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

#if WITH_EDITOR
			bUpdateAnimationInEditor = InSkelMeshComp->GetUpdateAnimationInEditor();
			//bUpdateClothInEditor = InSkelMeshComp->GetUpdateClothInEditor();
			InSkelMeshComp->SetUpdateAnimationInEditor(true);
			//InSkelMeshComp->SetUpdateClothInEditor(true);
#endif
		}
	}

	void RestoreState()
	{
		if (ChildSkelMesh.IsValid())
		{
			ChildSkelMesh->VisibilityBasedAnimTickOption = VisibilityBasedAnimTickOption;
#if WITH_EDITOR

			ChildSkelMesh->SetUpdateAnimationInEditor(bUpdateAnimationInEditor);
			//ChildSkelMesh->SetUpdateClothInEditor(bUpdateClothInEditor);
#endif
		}
	}
	TWeakObjectPtr<USkeletalMeshComponent> ChildSkelMesh;
	EVisibilityBasedAnimTickOption VisibilityBasedAnimTickOption;
#if WITH_EDITOR
	bool bUpdateAnimationInEditor;
	bool bUpdateClothInEditor;
#endif
};

struct FSkeletalMeshRestoreState
{
	void SaveState(USkeletalMeshComponent* InComponent)
	{
		SkeletalMeshCompEditorParams.SetNum(0);
		FSkeletalMeshEditorParams Parent;
		Parent.SaveState(InComponent);
		SkeletalMeshCompEditorParams.Add(Parent);
		TArray<USceneComponent*> ChildComponents;
		InComponent->GetChildrenComponents(true, ChildComponents);
		for (USceneComponent* ChildComponent : ChildComponents)
		{
			USkeletalMeshComponent* SkelMeshComp = Cast<USkeletalMeshComponent>(ChildComponent);
			if (SkelMeshComp)
			{
				FSkeletalMeshEditorParams Params;
				Params.SaveState(SkelMeshComp);
				SkeletalMeshCompEditorParams.Add(Params);
			}
		}
	}

	void RestoreState(USkeletalMeshComponent* InComponent)
	{
		for (FSkeletalMeshEditorParams& ChildParams : SkeletalMeshCompEditorParams)
		{
			ChildParams.RestoreState();
		}
	}
	TArray<FSkeletalMeshEditorParams> SkeletalMeshCompEditorParams;

};

