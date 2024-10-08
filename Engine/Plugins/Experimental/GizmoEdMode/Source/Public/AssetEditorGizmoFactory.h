// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UEdMode.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "UnrealWidget.h"

#include "AssetEditorGizmoFactory.generated.h"

class UTransformGizmo;
class UInteractiveGizmoManager;

UENUM()
enum class EAssetEditorGizmoFactoryPriority
{
	Default,
	Normal,
	High
};

UINTERFACE()
class GIZMOEDMODE_API UAssetEditorGizmoFactory : public UInterface
{
	GENERATED_BODY()
};

class GIZMOEDMODE_API IAssetEditorGizmoFactory
{
	GENERATED_BODY()
public:
	virtual bool CanBuildGizmoForSelection(FEditorModeTools* ModeTools) const = 0;
	virtual UTransformGizmo* BuildGizmoForSelection(FEditorModeTools* ModeTools, UInteractiveGizmoManager* GizmoManager) const = 0;
	virtual EAssetEditorGizmoFactoryPriority GetPriority() const { return EAssetEditorGizmoFactoryPriority::Normal; }
	virtual void ConfigureGridSnapping(bool bGridEnabled, bool bRotGridEnabled, UTransformGizmo* Gizmo) const = 0;
};
