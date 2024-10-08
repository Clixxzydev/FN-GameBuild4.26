// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionTrailEditorToolset.h"
#include "MotionTrailEditorMode.h"

#include "BaseGizmos/TransformGizmo.h"
#include "InteractiveGizmoManager.h"

#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/ClickDragBehavior.h"

#define LOCTEXT_NAMESPACE "MotionTrailEditorToolset"

UInteractiveTool* UTrailToolManagerBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UTrailToolManager* NewTool = NewObject<UTrailToolManager>(SceneState.ToolManager);
	NewTool->SetTrailToolName(TrailToolName);
	NewTool->SetMotionTrailEditorMode(EditorMode);
	NewTool->SetWorld(SceneState.World, SceneState.GizmoManager);
	return NewTool;
}

FString UTrailToolManager::TrailKeyTransformGizmoInstanceIdentifier = TEXT("TrailKeyTransformGizmoInstanceIdentifier");

FInputRayHit UTrailToolManager::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	FInputRayHit ReturnHit = FInputRayHit();
	for (FInteractiveTrailTool* TrailTool : EditorMode->GetTrailTools()[TrailToolName])
	{
		FInputRayHit TestHit = TrailTool->IsHitByClick(ClickPos);
		if (TestHit.bHit)
		{
			ReturnHit = TestHit;
		}
	}

	return ReturnHit;
}

void UTrailToolManager::OnClicked(const FInputDeviceRay& ClickPos)
{
	for (FInteractiveTrailTool* TrailTool : EditorMode->GetTrailTools()[TrailToolName])
	{
		TrailTool->OnClicked(ClickPos);
	}
}

FInputRayHit UTrailToolManager::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	FInputRayHit ReturnHit = FInputRayHit();
	for (FInteractiveTrailTool* TrailTool : EditorMode->GetTrailTools()[TrailToolName])
	{
		FInputRayHit TestHit = TrailTool->CanBeginClickDragSequence(PressPos);
		if (TestHit.bHit)
		{
			ReturnHit = TestHit;
		}
	}
	return ReturnHit;
}

void UTrailToolManager::OnClickPress(const FInputDeviceRay& PressPos)
{
	for (FInteractiveTrailTool* TrailTool : EditorMode->GetTrailTools()[TrailToolName])
	{
		TrailTool->OnClickPress(PressPos);
	}
}

void UTrailToolManager::OnClickDrag(const FInputDeviceRay& DragPos)
{
	for (FInteractiveTrailTool* TrailTool : EditorMode->GetTrailTools()[TrailToolName])
	{
		TrailTool->OnClickDrag(DragPos);
	}
}

void UTrailToolManager::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	for (FInteractiveTrailTool* TrailTool : EditorMode->GetTrailTools()[TrailToolName])
	{
		TrailTool->OnClickRelease(ReleasePos);
	}
}

void UTrailToolManager::OnTerminateDragSequence()
{
	for (FInteractiveTrailTool* TrailTool : EditorMode->GetTrailTools()[TrailToolName])
	{
		TrailTool->OnTerminateDragSequence();
	}
}

void UTrailToolManager::Setup()
{
	UInteractiveTool::Setup();

	// add default button input behaviors for devices
	USingleClickInputBehavior* MouseBehavior = NewObject<USingleClickInputBehavior>(this);
	MouseBehavior->Initialize(this);
	AddInputBehavior(MouseBehavior);

	UClickDragInputBehavior* ClickDragBehavior = NewObject<UClickDragInputBehavior>(this);
	ClickDragBehavior->Initialize(this);
	AddInputBehavior(ClickDragBehavior);

	for (FInteractiveTrailTool* TrailTool : EditorMode->GetTrailTools()[TrailToolName])
	{
		TrailTool->SetMotionTrailEditorMode(EditorMode);
		TrailTool->Setup();
	}

	if (EditorMode->GetTrailTools()[TrailToolName].Num() > 0)
	{
		ToolProperties = (*EditorMode->GetTrailTools()[TrailToolName].CreateConstIterator())->GetStaticToolProperties();
	}
}

void UTrailToolManager::Shutdown(EToolShutdownType ShutdownType)
{
	for (FInteractiveTrailTool* TrailTool : EditorMode->GetTrailTools()[TrailToolName])
	{
		TrailTool->SetMotionTrailEditorMode(nullptr);
	}
}

void UTrailToolManager::Render(IToolsContextRenderAPI* RenderAPI)
{
	for (FInteractiveTrailTool* TrailTool : EditorMode->GetTrailTools()[TrailToolName])
	{
		TrailTool->Render(RenderAPI);
	}
}

void UTrailToolManager::OnTick(float DeltaTime)
{
	for (FInteractiveTrailTool* TrailTool : EditorMode->GetTrailTools()[TrailToolName])
	{
		TrailTool->Tick(DeltaTime);
	}
}

TArray<UObject*> UTrailToolManager::GetToolProperties(bool bEnabledOnly) const
{
	return EditorMode->GetTrailTools()[TrailToolName].Num() > 0 ? (*EditorMode->GetTrailTools()[TrailToolName].CreateConstIterator())->GetStaticToolProperties() : TArray<UObject*>();
}

#undef LOCTEXT_NAMESPACE
