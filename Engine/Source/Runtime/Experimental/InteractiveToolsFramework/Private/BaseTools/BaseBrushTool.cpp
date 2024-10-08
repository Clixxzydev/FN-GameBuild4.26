// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseTools/BaseBrushTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ToolBuilderUtil.h"
#include "InteractiveTool.h"
#include "BaseGizmos/BrushStampIndicator.h"


#define LOCTEXT_NAMESPACE "UBaseBrushTool"



UBrushBaseProperties::UBrushBaseProperties()
{
	BrushSize = 0.25f;
	bSpecifyRadius = false;
	BrushRadius = 10.0f;
	BrushStrength = 0.5f;
	BrushFalloffAmount = 1.0f;
}

UBaseBrushTool::UBaseBrushTool()
{
	PropertyClass = UBrushBaseProperties::StaticClass();
}


void UBaseBrushTool::Setup()
{
	UMeshSurfacePointTool::Setup();
	BrushProperties = NewObject<UBrushBaseProperties>(this, PropertyClass.Get(), TEXT("Brush"));
	double MaxDimension = EstimateMaximumTargetDimension();
	BrushRelativeSizeRange = TInterval<float>(MaxDimension*0.01, MaxDimension);

	RecalculateBrushRadius();

	// initialize our properties
	AddToolPropertySource(BrushProperties);

	SetupBrushStampIndicator();
}


void UBaseBrushTool::Shutdown(EToolShutdownType ShutdownType)
{
	ShutdownBrushStampIndicator();
}


void UBaseBrushTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (PropertySet == BrushProperties)
	{
		RecalculateBrushRadius();
	}
}



void UBaseBrushTool::IncreaseBrushSizeAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize + 0.025f, 0.0f, 1.0f);
	RecalculateBrushRadius();
}

void UBaseBrushTool::DecreaseBrushSizeAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize - 0.025f, 0.0f, 1.0f);
	RecalculateBrushRadius();
}


void UBaseBrushTool::IncreaseBrushStrengthAction()
{
	const float ChangeAmount = 0.02f;
	const float OldValue = BrushProperties->BrushStrength;

	float NewValue = OldValue + ChangeAmount;
	BrushProperties->BrushStrength = FMath::Clamp(NewValue, 0.f, 1.f);
}

void UBaseBrushTool::DecreaseBrushStrengthAction()
{
	const float ChangeAmount = 0.02f;
	const float OldValue = BrushProperties->BrushStrength;

	float NewValue = OldValue - ChangeAmount;
	BrushProperties->BrushStrength = FMath::Clamp(NewValue, 0.f, 1.f);
}


void UBaseBrushTool::IncreaseBrushFalloffAction()
{
	const float ChangeAmount = 0.02f;
	const float OldValue = BrushProperties->BrushFalloffAmount;

	float NewValue = OldValue + ChangeAmount;
	BrushProperties->BrushFalloffAmount = FMath::Clamp(NewValue, 0.f, 1.f);
}

void UBaseBrushTool::DecreaseBrushFalloffAction()
{
	const float ChangeAmount = 0.02f;
	const float OldValue = BrushProperties->BrushFalloffAmount;

	float NewValue = OldValue - ChangeAmount;
	BrushProperties->BrushFalloffAmount = FMath::Clamp(NewValue, 0.f, 1.f);
}

void UBaseBrushTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 10,
		TEXT("BrushIncreaseSize"), 
		LOCTEXT("BrushIncreaseSize", "Increase Brush Size"),
		LOCTEXT("BrushIncreaseSizeTooltip", "Press this key to increase brush radius by a percentage of its current size."),
		EModifierKey::None, EKeys::RightBracket,
		[this]() { IncreaseBrushSizeAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 11,
		TEXT("BrushDecreaseSize"), 
		LOCTEXT("BrushDecreaseSize", "Decrease Brush Size"),
		LOCTEXT("BrushDecreaseSizeTooltip", "Press this key to decrease brush radius by a percentage of its current size."),
		EModifierKey::None, EKeys::LeftBracket,
		[this]() { DecreaseBrushSizeAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 12,
		TEXT("BrushIncreaseFalloff"),
		LOCTEXT("BrushIncreaseFalloff", "Increase Brush Falloff"),
		LOCTEXT("BrushIncreaseFalloffTooltip", "Press this key to increase brush falloff by a fixed increment."),
		EModifierKey::Shift | EModifierKey::Control, EKeys::RightBracket,
		[this]() { IncreaseBrushFalloffAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 13,
		TEXT("BrushDecreaseFalloff"),
		LOCTEXT("BrushDecreaseFalloff", "Decrease Brush Falloff"),
		LOCTEXT("BrushDecreaseFalloffTooltip", "Press this key to decrease brush falloff by a fixed increment."),
		EModifierKey::Shift | EModifierKey::Control, EKeys::LeftBracket,
		[this]() { DecreaseBrushFalloffAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 14,
		TEXT("BrushIncreaseStrength"),
		LOCTEXT("BrushIncreaseStrength", "Increase Brush Strength"),
		LOCTEXT("BrushIncreaseStrengthTooltip", "Press this key to increase brush strength by a fixed increment."),
		EModifierKey::Control, EKeys::RightBracket,
		[this]() { IncreaseBrushStrengthAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 15,
		TEXT("BrushDecreaseStrength"),
		LOCTEXT("BrushDecreaseStrength", "Decrease Brush Strength"),
		LOCTEXT("BrushDecreaseStrengthTooltip", "Press this key to decrease brush strength by a fixed increment."),
		EModifierKey::Control, EKeys::LeftBracket,
		[this]() { DecreaseBrushStrengthAction(); });
}




void UBaseBrushTool::RecalculateBrushRadius()
{
	CurrentBrushRadius = 0.5 * BrushRelativeSizeRange.Interpolate(BrushProperties->BrushSize);
	if (BrushProperties->bSpecifyRadius)
	{
		CurrentBrushRadius = BrushProperties->BrushRadius;
	}
	else
	{
		BrushProperties->BrushRadius = CurrentBrushRadius;
	}
}


void UBaseBrushTool::OnBeginDrag(const FRay& Ray)
{
	FHitResult OutHit;
	if (HitTest(Ray, OutHit))
	{
		LastBrushStamp.Radius = BrushProperties->BrushRadius;
		LastBrushStamp.WorldPosition = OutHit.ImpactPoint;
		LastBrushStamp.WorldNormal = OutHit.Normal;
		LastBrushStamp.HitResult = OutHit;
		LastBrushStamp.Falloff = BrushProperties->BrushFalloffAmount;
	}
	bInBrushStroke = true;
}

void UBaseBrushTool::OnUpdateDrag(const FRay& Ray)
{
	FHitResult OutHit;
	if (HitTest(Ray, OutHit))
	{
		LastBrushStamp.Radius = BrushProperties->BrushRadius;
		LastBrushStamp.WorldPosition = OutHit.ImpactPoint;
		LastBrushStamp.WorldNormal = OutHit.Normal;
		LastBrushStamp.HitResult = OutHit;
		LastBrushStamp.Falloff = BrushProperties->BrushFalloffAmount;
	}
}

void UBaseBrushTool::OnEndDrag(const FRay& Ray)
{
	bInBrushStroke = false;
}


bool UBaseBrushTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	FHitResult OutHit;
	if (HitTest(DevicePos.WorldRay, OutHit))
	{
		LastBrushStamp.Radius = BrushProperties->BrushRadius;
		LastBrushStamp.WorldPosition = OutHit.ImpactPoint;
		LastBrushStamp.WorldNormal = OutHit.Normal;
		LastBrushStamp.HitResult = OutHit;
		LastBrushStamp.Falloff = BrushProperties->BrushFalloffAmount;
	}
	return true;
}



void UBaseBrushTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UMeshSurfacePointTool::Render(RenderAPI);

	UpdateBrushStampIndicator();
}

const FString BaseBrushIndicatorGizmoType = TEXT("BrushIndicatorGizmoType");

void UBaseBrushTool::SetupBrushStampIndicator()
{
	// register and spawn brush indicator gizmo
	GetToolManager()->GetPairedGizmoManager()->RegisterGizmoType(BaseBrushIndicatorGizmoType, NewObject<UBrushStampIndicatorBuilder>());
	BrushStampIndicator = GetToolManager()->GetPairedGizmoManager()->CreateGizmo<UBrushStampIndicator>(BaseBrushIndicatorGizmoType, FString(), this);
}

void UBaseBrushTool::UpdateBrushStampIndicator()
{
	BrushStampIndicator->Update(LastBrushStamp.Radius, LastBrushStamp.WorldPosition, LastBrushStamp.WorldNormal, LastBrushStamp.Falloff);
}

void UBaseBrushTool::ShutdownBrushStampIndicator()
{
	GetToolManager()->GetPairedGizmoManager()->DestroyAllGizmosByOwner(this);
	BrushStampIndicator = nullptr;
	GetToolManager()->GetPairedGizmoManager()->DeregisterGizmoType(BaseBrushIndicatorGizmoType);
}


#undef LOCTEXT_NAMESPACE
