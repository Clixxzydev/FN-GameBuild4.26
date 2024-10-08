// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Input/CursorReply.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Layout/Margin.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "Templates/IsIntegral.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

/*
 * This function compute a slider position by simulating two log on both side of the neutral value
 * Example a slider going from 0.0 to 2.0 with a neutral value of 1.0, the user will have a lot of precision around the neutral value
 * on both side.
 |
 ||                              |
 | -_                          _-
 |   --__                  __--
 |       ----__________----
 ----------------------------------
  0              1               2

  The function return a float representing the slider fraction used to position the slider handle
  FractionFilled: this is the value slider position with no exponent
  StartFractionFilled: this is the neutral value slider position with no exponent
  SliderExponent: this is the slider exponent
*/
SLATE_API float SpinBoxComputeExponentSliderFraction(float FractionFilled, float StartFractionFilled, float SliderExponent);

/**
 * A Slate SpinBox resembles traditional spin boxes in that it is a widget that provides
 * keyboard-based and mouse-based manipulation of a numeric value.
 * Mouse-based manipulation: drag anywhere on the spinbox to change the value.
 * Keyboard-based manipulation: click on the spinbox to enter text mode.
 */
template<typename NumericType>
class SSpinBox
	: public SCompoundWidget
{
public:

	/** Notification for numeric value change */
	DECLARE_DELEGATE_OneParam( FOnValueChanged, NumericType );

	/** Notification for numeric value committed */
	DECLARE_DELEGATE_TwoParams( FOnValueCommitted, NumericType, ETextCommit::Type);

	/** Notification when the max/min spinner values are changed (only apply if SupportDynamicSliderMaxValue or SupportDynamicSliderMinValue are true) */
	DECLARE_DELEGATE_FourParams(FOnDynamicSliderMinMaxValueChanged, NumericType, TWeakPtr<SWidget>, bool, bool);

	SLATE_BEGIN_ARGS(SSpinBox<NumericType>)
		: _Style(&FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
		, _Value(0)
		, _MinFractionalDigits(DefaultMinFractionalDigits)
		, _MaxFractionalDigits(DefaultMaxFractionalDigits)
		, _AlwaysUsesDeltaSnap(false)
		, _Delta(0)
		, _ShiftMouseMovePixelPerDelta(1)
		, _SupportDynamicSliderMaxValue(false)
		, _SupportDynamicSliderMinValue(false)
		, _SliderExponent(1)
		, _Font( FCoreStyle::Get().GetFontStyle( TEXT( "NormalFont" ) ) )
		, _ContentPadding(  FMargin( 2.0f, 1.0f) )
		, _OnValueChanged()
		, _OnValueCommitted()
		, _ClearKeyboardFocusOnCommit( false )
		, _SelectAllTextOnCommit( true )
		, _MinDesiredWidth(0.0f)
		{}
	
		/** The style used to draw this spinbox */
		SLATE_STYLE_ARGUMENT( FSpinBoxStyle, Style )

		/** The value to display */
		SLATE_ATTRIBUTE( NumericType, Value )
		/** The minimum value that can be entered into the text edit box */
		SLATE_ATTRIBUTE( TOptional< NumericType >, MinValue )
		/** The maximum value that can be entered into the text edit box */
		SLATE_ATTRIBUTE( TOptional< NumericType >, MaxValue )
		/** The minimum value that can be specified by using the slider, defaults to MinValue */
		SLATE_ATTRIBUTE( TOptional< NumericType >, MinSliderValue )
		/** The maximum value that can be specified by using the slider, defaults to MaxValue */
		SLATE_ATTRIBUTE( TOptional< NumericType >, MaxSliderValue )
		/** The minimum fractional digits the spin box displays, defaults to 1 */
		SLATE_ATTRIBUTE(TOptional< int32 >, MinFractionalDigits)
		/** The maximum fractional digits the spin box displays, defaults to 6 */
		SLATE_ATTRIBUTE(TOptional< int32 >, MaxFractionalDigits)
		/** Whether typed values should use delta snapping, defaults to false */
		SLATE_ATTRIBUTE(bool, AlwaysUsesDeltaSnap)
		/** Delta to increment the value as the slider moves.  If not specified will determine automatically */
		SLATE_ATTRIBUTE( NumericType, Delta )
		/** How many pixel the mouse must move to change the value of the delta step */
		SLATE_ATTRIBUTE( int32, ShiftMouseMovePixelPerDelta )
		/** If we're an unbounded spinbox, what value do we divide mouse movement by before multiplying by Delta. Requires Delta to be set. */
		SLATE_ATTRIBUTE( int32, LinearDeltaSensitivity)
		/** Tell us if we want to support dynamically changing of the max value using ctrl */
		SLATE_ATTRIBUTE(bool, SupportDynamicSliderMaxValue)
		/** Tell us if we want to support dynamically changing of the min value using ctrl */
		SLATE_ATTRIBUTE(bool, SupportDynamicSliderMinValue)
		/** Called right after the max slider value is changed (only relevant if SupportDynamicSliderMaxValue is true) */
		SLATE_EVENT(FOnDynamicSliderMinMaxValueChanged, OnDynamicSliderMaxValueChanged)
		/** Called right after the min slider value is changed (only relevant if SupportDynamicSliderMinValue is true) */
		SLATE_EVENT(FOnDynamicSliderMinMaxValueChanged, OnDynamicSliderMinValueChanged)
		/** Use exponential scale for the slider */
		SLATE_ATTRIBUTE( float, SliderExponent )
		/** When use exponential scale for the slider which is the neutral value */
		SLATE_ATTRIBUTE( NumericType, SliderExponentNeutralValue )
		/** Font used to display text in the slider */
		SLATE_ATTRIBUTE( FSlateFontInfo, Font )
		/** Padding to add around this widget and its internal widgets */
		SLATE_ATTRIBUTE( FMargin, ContentPadding )
		/** Called when the value is changed by slider or typing */
		SLATE_EVENT( FOnValueChanged, OnValueChanged )
		/** Called when the value is committed (by pressing enter) */
		SLATE_EVENT( FOnValueCommitted, OnValueCommitted )
		/** Called right before the slider begins to move */
		SLATE_EVENT( FSimpleDelegate, OnBeginSliderMovement )
		/** Called right after the slider handle is released by the user */
		SLATE_EVENT( FOnValueChanged, OnEndSliderMovement )
		/** Whether to clear keyboard focus when pressing enter to commit changes */
		SLATE_ATTRIBUTE( bool, ClearKeyboardFocusOnCommit )
		/** Whether to select all text when pressing enter to commit changes */
		SLATE_ATTRIBUTE( bool, SelectAllTextOnCommit )
		/** Minimum width that a spin box should be */
		SLATE_ATTRIBUTE( float, MinDesiredWidth )
		/** How should the value be justified in the spinbox. */
		SLATE_ATTRIBUTE( ETextJustify::Type, Justification )
		/** Provide custom type conversion functionality to this spin box */
		SLATE_ARGUMENT( TSharedPtr< INumericTypeInterface<NumericType> >, TypeInterface )
		/** If refresh requests for the viewport should happen for all value changes **/
		SLATE_ARGUMENT(bool, PreventThrottling)
		/** Menu extender for the right-click context menu */
		SLATE_EVENT( FMenuExtensionDelegate, ContextMenuExtender )

	SLATE_END_ARGS()

	SSpinBox()
	{
	}
		
	/**
	 * Construct the widget
	 * 
	 * @param InArgs   A declaration from which to construct the widget
	 */
	void Construct( const FArguments& InArgs )
	{
		check(InArgs._Style);

		Style = InArgs._Style;

		ForegroundColor = InArgs._Style->ForegroundColor;
		Interface = InArgs._TypeInterface.IsValid() ? InArgs._TypeInterface : MakeShareable( new TDefaultNumericTypeInterface<NumericType> );

		ValueAttribute = InArgs._Value;
		OnValueChanged = InArgs._OnValueChanged;
		OnValueCommitted = InArgs._OnValueCommitted;
		OnBeginSliderMovement = InArgs._OnBeginSliderMovement;
		OnEndSliderMovement = InArgs._OnEndSliderMovement;
		MinDesiredWidth = InArgs._MinDesiredWidth;
	
		MinValue = InArgs._MinValue;
		MaxValue = InArgs._MaxValue;
		MinSliderValue = (InArgs._MinSliderValue.Get().IsSet()) ? InArgs._MinSliderValue : MinValue;
		MaxSliderValue = (InArgs._MaxSliderValue.Get().IsSet()) ? InArgs._MaxSliderValue : MaxValue;

		MinFractionalDigits = (InArgs._MinFractionalDigits.Get().IsSet()) ? InArgs._MinFractionalDigits : DefaultMinFractionalDigits;
		MaxFractionalDigits = (InArgs._MaxFractionalDigits.Get().IsSet()) ? InArgs._MaxFractionalDigits : DefaultMaxFractionalDigits;

		AlwaysUsesDeltaSnap = InArgs._AlwaysUsesDeltaSnap;

		SupportDynamicSliderMaxValue = InArgs._SupportDynamicSliderMaxValue;
		SupportDynamicSliderMinValue = InArgs._SupportDynamicSliderMinValue;
		OnDynamicSliderMaxValueChanged = InArgs._OnDynamicSliderMaxValueChanged;
		OnDynamicSliderMinValueChanged = InArgs._OnDynamicSliderMinValueChanged;

		bPreventThrottling = InArgs._PreventThrottling;

		// Update the max slider value based on the current value if we're in dynamic mode
		NumericType CurrentMaxValue = GetMaxValue();
		NumericType CurrentMinValue = GetMinValue();

		if (SupportDynamicSliderMaxValue.Get() && ValueAttribute.Get() > GetMaxSliderValue())
		{			
			ApplySliderMaxValueChanged(ValueAttribute.Get() - GetMaxSliderValue(), true);
		}
		else if (SupportDynamicSliderMinValue.Get() && ValueAttribute.Get() < GetMinSliderValue())
		{
			ApplySliderMinValueChanged(ValueAttribute.Get() - GetMinSliderValue(), true);
		}

		UpdateIsSpinRangeUnlimited();
	
		SliderExponent = InArgs._SliderExponent;

		SliderExponentNeutralValue = InArgs._SliderExponentNeutralValue;

		DistanceDragged = 0.0f;
		PreDragValue = 0;

		Delta = InArgs._Delta;
		ShiftMouseMovePixelPerDelta = InArgs._ShiftMouseMovePixelPerDelta;
		LinearDeltaSensitivity = InArgs._LinearDeltaSensitivity;
	
		BackgroundHoveredBrush = &InArgs._Style->HoveredBackgroundBrush;
		BackgroundBrush = &InArgs._Style->BackgroundBrush;
		ActiveFillBrush = &InArgs._Style->ActiveFillBrush;
		InactiveFillBrush = &InArgs._Style->InactiveFillBrush;
		const FMargin& TextMargin = InArgs._Style->TextPadding;

		bDragging = false;
		PointerDraggingSliderIndex = INDEX_NONE;

		CachedExternalValue = ValueAttribute.Get();
		IntermediateDragFractionalValue = 0.0;

		bIsTextChanging = false;

		this->ChildSlot
		.Padding( InArgs._ContentPadding )
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding( TextMargin )
			.HAlign(HAlign_Fill) 
			.VAlign(VAlign_Center)
			[
				SAssignNew(TextBlock, STextBlock)
				.Font(InArgs._Font)
				.Text( this, &SSpinBox<NumericType>::GetValueAsText )
				.MinDesiredWidth( this, &SSpinBox<NumericType>::GetTextMinDesiredWidth )
				.Justification(InArgs._Justification)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding( TextMargin )
			.HAlign(HAlign_Fill) 
			.VAlign(VAlign_Center) 
			[
				SAssignNew(EditableText, SEditableText)
				.Visibility( EVisibility::Collapsed )
				.Font(InArgs._Font)
				.SelectAllTextWhenFocused( true )
				.Text( this, &SSpinBox<NumericType>::GetValueAsText )
				.OnIsTypedCharValid(this, &SSpinBox<NumericType>::IsCharacterValid)
				.OnTextChanged( this, &SSpinBox<NumericType>::TextField_OnTextChanged )
				.OnTextCommitted( this, &SSpinBox<NumericType>::TextField_OnTextCommitted )
				.ClearKeyboardFocusOnCommit( InArgs._ClearKeyboardFocusOnCommit )
				.SelectAllTextOnCommit( InArgs._SelectAllTextOnCommit )
				.MinDesiredWidth( this, &SSpinBox<NumericType>::GetTextMinDesiredWidth )
				.VirtualKeyboardType(EKeyboardType::Keyboard_Number)
				.Justification(InArgs._Justification)
				.VirtualKeyboardTrigger(EVirtualKeyboardTrigger::OnAllFocusEvents)
				.ContextMenuExtender( InArgs._ContextMenuExtender )
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Fill) 
			.VAlign(VAlign_Center) 
			[
				SNew(SImage)
				.Image( &InArgs._Style->ArrowsImage )
				.ColorAndOpacity( FSlateColor::UseForeground())
			]
		];
	}
	
	virtual int32 OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const override
	{
		const bool bActiveFeedback = IsHovered() || bDragging;

		const FSlateBrush* BackgroundImage = bActiveFeedback ?
			BackgroundHoveredBrush :
			BackgroundBrush;

		const FSlateBrush* FillImage = bActiveFeedback ?
			ActiveFillBrush :
			InactiveFillBrush;

		const int32 BackgroundLayer = LayerId;

		const bool bEnabled = ShouldBeEnabled( bParentEnabled );
		const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			BackgroundLayer,
			AllottedGeometry.ToPaintGeometry(),
			BackgroundImage,
			DrawEffects,
			BackgroundImage->GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint()
			);

		const int32 FilledLayer = BackgroundLayer + 1;

		//if there is a spin range limit, draw the filler bar
		if (!bUnlimitedSpinRange)
		{
			NumericType Value = ValueAttribute.Get();
			NumericType CurrentDelta = Delta.Get();
			if( CurrentDelta != 0.0f )
			{
				Value = FMath::GridSnap(Value, CurrentDelta); // snap floating point value to nearest Delta
			}

			float FractionFilled = Fraction(Value, GetMinSliderValue(), GetMaxSliderValue());
			const float CachedSliderExponent = SliderExponent.Get();
			if (CachedSliderExponent != 1)
			{
				if (SliderExponentNeutralValue.IsSet() && SliderExponentNeutralValue.Get() > GetMinSliderValue() && SliderExponentNeutralValue.Get() < GetMaxSliderValue())
				{
					//Compute a log curve on both side of the neutral value
					float StartFractionFilled = Fraction(SliderExponentNeutralValue.Get(), GetMinSliderValue(), GetMaxSliderValue());
					FractionFilled = SpinBoxComputeExponentSliderFraction(FractionFilled, StartFractionFilled, CachedSliderExponent);
				}
				else
				{
					FractionFilled = 1.0f - FMath::Pow( 1.0f - FractionFilled, CachedSliderExponent);
				}
			}
			const FVector2D FillSize( AllottedGeometry.GetLocalSize().X * FractionFilled, AllottedGeometry.GetLocalSize().Y );

			if ( ! IsInTextMode() )
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					FilledLayer,
					AllottedGeometry.ToPaintGeometry(FVector2D(0,0), FillSize),
					FillImage,
					DrawEffects,
					FillImage->GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint()
					);
			}
		}

		return FMath::Max( FilledLayer, SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, FilledLayer, InWidgetStyle, bEnabled ) );
	}

	/**
	 * The system calls this method to notify the widget that a mouse button was pressed within it. This event is bubbled.
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param MouseEvent Information about the input event
	 * @return Whether the event was handled along with possible requests for the system to take action.
	 */
	virtual FReply OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override
	{
		if ( MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && PointerDraggingSliderIndex == INDEX_NONE )
		{
			DistanceDragged = 0;
			PreDragValue = ValueAttribute.Get();
			IntermediateDragFractionalValue = 0.0;
			PointerDraggingSliderIndex = MouseEvent.GetPointerIndex();
			CachedMousePosition = MouseEvent.GetScreenSpacePosition().IntPoint();

			FReply ReturnReply = FReply::Handled().CaptureMouse(SharedThis(this)).UseHighPrecisionMouseMovement(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
			if (bPreventThrottling) 
			{
				ReturnReply.PreventThrottling();
			}
			return ReturnReply;
		}
		else
		{

			return FReply::Unhandled();
		}
	}
	
	/**
	 * The system calls this method to notify the widget that a mouse button was release within it. This event is bubbled.
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param MouseEvent Information about the input event
	 * @return Whether the event was handled along with possible requests for the system to take action.
	 */
	virtual FReply OnMouseButtonUp( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override
	{
		if ( MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && PointerDraggingSliderIndex == MouseEvent.GetPointerIndex() )
		{
			if(!this->HasMouseCapture())
			{
				// Lost Capture - ensure reset
				bDragging = false;
				PointerDraggingSliderIndex = INDEX_NONE;
				
				return FReply::Unhandled();
			}
		
			if( bDragging )
			{
				NotifyValueCommitted();
			}

			bDragging = false;
			PointerDraggingSliderIndex = INDEX_NONE;

			FReply Reply = FReply::Handled().ReleaseMouseCapture();

			if ( !MouseEvent.IsTouchEvent() )
			{
				Reply.SetMousePos(CachedMousePosition);
			}

			if ( DistanceDragged < FSlateApplication::Get().GetDragTriggerDistance() )
			{
				EnterTextMode();
				Reply.SetUserFocus(EditableText.ToSharedRef(), EFocusCause::SetDirectly);
			}

			return Reply;
		}
		
		return FReply::Unhandled();
	}

	void ApplySliderMaxValueChanged(float SliderDeltaToAdd, bool UpdateOnlyIfHigher)
	{
		check(SupportDynamicSliderMaxValue.Get());

		NumericType NewMaxSliderValue = TNumericLimits<NumericType>::Min();
		
		if (MaxSliderValue.IsSet() && MaxSliderValue.Get().IsSet())
		{
			NewMaxSliderValue = GetMaxSliderValue();

			if ((NewMaxSliderValue + SliderDeltaToAdd > GetMaxSliderValue() && UpdateOnlyIfHigher) || !UpdateOnlyIfHigher)
			{
				NewMaxSliderValue += SliderDeltaToAdd;

				if (!MaxSliderValue.IsBound()) // simple value so we can update it without breaking the mechanic otherwise it must be handle by the callback implementer
				{
					SetMaxSliderValue(NewMaxSliderValue);
				}
			}
		}

		if (OnDynamicSliderMaxValueChanged.IsBound())
		{
			OnDynamicSliderMaxValueChanged.Execute(NewMaxSliderValue, TWeakPtr<SWidget>(AsShared()), true, UpdateOnlyIfHigher);
		}
	}

	void ApplySliderMinValueChanged(float SliderDeltaToAdd, bool UpdateOnlyIfLower)
	{
		check(SupportDynamicSliderMaxValue.Get());

		NumericType NewMinSliderValue = TNumericLimits<NumericType>::Min();
		
		if (MinSliderValue.IsSet() && MinSliderValue.Get().IsSet())
		{
			NewMinSliderValue = GetMinSliderValue();

			if ((NewMinSliderValue + SliderDeltaToAdd < GetMinSliderValue() && UpdateOnlyIfLower) || !UpdateOnlyIfLower)
			{
				NewMinSliderValue += SliderDeltaToAdd;

				if (!MinSliderValue.IsBound()) // simple value so we can update it without breaking the mechanic otherwise it must be handle by the callback implementer
				{
					SetMinSliderValue(NewMinSliderValue);
				}
			}
		}		

		if (OnDynamicSliderMinValueChanged.IsBound())
		{
			OnDynamicSliderMinValueChanged.Execute(NewMinSliderValue, TWeakPtr<SWidget>(AsShared()), true, UpdateOnlyIfLower);
		}
	}
	
	/**
	 * The system calls this method to notify the widget that a mouse moved within it. This event is bubbled.
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param MouseEvent Information about the input event
	 * @return Whether the event was handled along with possible requests for the system to take action.
	 */
	virtual FReply OnMouseMove( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override
	{
		if ( PointerDraggingSliderIndex == MouseEvent.GetPointerIndex() )
		{
			if(!this->HasMouseCapture())
			{
				// Lost the mouse capture - ensure reset
				bDragging = false;
				PointerDraggingSliderIndex = INDEX_NONE;
				
				return FReply::Unhandled();
			}
			
			if (!bDragging)
			{
				DistanceDragged += FMath::Abs(MouseEvent.GetCursorDelta().X);
				if ( DistanceDragged > FSlateApplication::Get().GetDragTriggerDistance() )
				{
					ExitTextMode();
					bDragging = true;
					OnBeginSliderMovement.ExecuteIfBound();
				}

				// Cache the mouse, even if not dragging cache it.
				CachedMousePosition = MouseEvent.GetScreenSpacePosition().IntPoint();
			}
			else
			{
				// Increments the spin based on delta mouse movement.

				// A minimum slider width to use for calculating deltas in the slider-range space
				const float MinSliderWidth = 100.f;
				float SliderWidthInSlateUnits = FMath::Max(MyGeometry.GetDrawSize().X, MinSliderWidth);
				
				const int32 CachedShiftMouseMovePixelPerDelta = ShiftMouseMovePixelPerDelta.Get();
				if (CachedShiftMouseMovePixelPerDelta > 1 && MouseEvent.IsShiftDown())
				{
					SliderWidthInSlateUnits *= CachedShiftMouseMovePixelPerDelta;
				}

				if (MouseEvent.IsControlDown())
				{
					float DeltaToAdd = MouseEvent.GetCursorDelta().X / SliderWidthInSlateUnits;

					if (SupportDynamicSliderMaxValue.Get() && CachedExternalValue == GetMaxSliderValue())
					{
						ApplySliderMaxValueChanged(DeltaToAdd, false);
					}
					else if (SupportDynamicSliderMinValue.Get() && CachedExternalValue == GetMinSliderValue())
					{
						ApplySliderMinValueChanged(DeltaToAdd, false);
					}
				}
				
				//if we have a range to draw in
				if ( !bUnlimitedSpinRange) 
				{
					bool HasValidExponentNeutralValue = SliderExponentNeutralValue.IsSet() && SliderExponentNeutralValue.Get() > GetMinSliderValue() && SliderExponentNeutralValue.Get() < GetMaxSliderValue();

					const float CachedSliderExponent = SliderExponent.Get();
					// The amount currently filled in the spinbox, needs to be calculated to do deltas correctly.
					float FractionFilled = Fraction(PreDragValue, GetMinSliderValue(), GetMaxSliderValue());
						
					if (CachedSliderExponent != 1)
					{
						if (HasValidExponentNeutralValue)
						{
							//Compute a log curve on both side of the neutral value
							float StartFractionFilled = Fraction(SliderExponentNeutralValue.Get(), GetMinSliderValue(), GetMaxSliderValue());
							FractionFilled = SpinBoxComputeExponentSliderFraction(FractionFilled, StartFractionFilled, CachedSliderExponent);
						}
						else
						{
							FractionFilled = 1.0f - FMath::Pow(1.0f - FractionFilled, CachedSliderExponent);
						}
					}
					FractionFilled *= SliderWidthInSlateUnits;

					// Now add the delta to the fraction filled, this causes the spin.
					float MouseDelta = MouseEvent.GetScreenSpacePosition().IntPoint().X - CachedMousePosition.X;
					FractionFilled += MouseDelta;
						
					// Clamp the fraction to be within the bounds of the geometry.
					FractionFilled = FMath::Clamp(FractionFilled, 0.0f, SliderWidthInSlateUnits);

					// Convert the fraction filled to a percent.
					float Percent = FMath::Clamp(FractionFilled / SliderWidthInSlateUnits, 0.0f, 1.0f);
					if (CachedSliderExponent != 1)
					{
						// Have to convert the percent to the proper value due to the exponent component to the spin.
						if (HasValidExponentNeutralValue)
						{
							//Compute a log curve on both side of the neutral value
							float StartFractionFilled = Fraction(SliderExponentNeutralValue.Get(), GetMinSliderValue(), GetMaxSliderValue());
							Percent = SpinBoxComputeExponentSliderFraction(Percent, StartFractionFilled, 1.0/CachedSliderExponent);
						}
						else
						{
							Percent = 1.0f - FMath::Pow(1.0f - Percent, 1.0 / CachedSliderExponent);
						}
						
						
					}

					double ValueToRound = FMath::LerpStable<double>(GetMinSliderValue(), GetMaxSliderValue(), Percent);
					NumericType NewValue = TIsIntegral<NumericType>::Value
						? (NumericType)FMath::FloorToDouble(ValueToRound + 0.5)
						: (NumericType)ValueToRound;
					CommitValue(NewValue, CommittedViaSpin, ETextCommit::OnEnter);
				}
				else
				{
					// If this control has a specified delta and sensitivity then we use that instead of the current value for determining how much to change.
					const float Sign = (MouseEvent.GetCursorDelta().X > 0) ? 1.f : -1.f;
					if (LinearDeltaSensitivity.IsSet() && Delta.IsSet() && Delta.Get() > 0)
					{
						const float MouseDelta = FMath::Abs(MouseEvent.GetCursorDelta().X / LinearDeltaSensitivity.Get());
						IntermediateDragFractionalValue += (Sign * MouseDelta * FMath::Pow(Delta.Get(), SliderExponent.Get()));
					}
					else
					{
						const float MouseDelta = FMath::Abs(MouseEvent.GetCursorDelta().X / SliderWidthInSlateUnits);
						const double CurrentValue = FMath::Clamp<double>(FMath::Abs(static_cast<double>(CachedExternalValue)), 1.0, TNumericLimits<NumericType>::Max());
						IntermediateDragFractionalValue += (Sign * MouseDelta * FMath::Pow(CurrentValue, SliderExponent.Get()));
					}

					NumericType NewValue = UpdateDraggingValues(CachedExternalValue);
					CommitValue(NewValue, CommittedViaSpin, ETextCommit::OnEnter);
				}
			}

			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	virtual FCursorReply OnCursorQuery( const FGeometry& MyGeometry, const FPointerEvent& CursorEvent ) const override
	{
		return bDragging ? 
			FCursorReply::Cursor( EMouseCursor::None ) :
			FCursorReply::Cursor( EMouseCursor::ResizeLeftRight );
	}

	virtual bool SupportsKeyboardFocus() const override
	{
		// SSpinBox is focusable.
		return true;
	}


	virtual FReply OnFocusReceived( const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent ) override
	{
		if ( !bDragging && (InFocusEvent.GetCause() == EFocusCause::Navigation || InFocusEvent.GetCause() == EFocusCause::SetDirectly) )
		{
			EnterTextMode();
			return FReply::Handled().SetUserFocus(EditableText.ToSharedRef(), InFocusEvent.GetCause());
		}
		else
		{
			return FReply::Unhandled();
		}
	}
	
	virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent ) override
	{
		const FKey Key = InKeyEvent.GetKey();
		if ( Key == EKeys::Escape && HasMouseCapture())
		{
			bDragging = false;
			PointerDraggingSliderIndex = INDEX_NONE;

			CachedExternalValue = PreDragValue;
			NotifyValueCommitted();
			return FReply::Handled().ReleaseMouseCapture().SetMousePos(CachedMousePosition);
		}
		else if ( Key == EKeys::Up || Key == EKeys::Right )
		{
			CommitValue( ValueAttribute.Get() + Delta.Get(), CommittedViaArrowKey, ETextCommit::OnEnter );
			ExitTextMode();
			return FReply::Handled();	
		}
		else if ( Key == EKeys::Down || Key == EKeys::Left )
		{
			CommitValue( ValueAttribute.Get() - Delta.Get(), CommittedViaArrowKey, ETextCommit::OnEnter );
			ExitTextMode();
			return FReply::Handled();
		}
		else if ( Key == EKeys::Enter )
		{
			CachedExternalValue = ValueAttribute.Get();
			EnterTextMode();
			return FReply::Handled().SetUserFocus(EditableText.ToSharedRef(), EFocusCause::Navigation);
		}
		else
		{
			return FReply::Unhandled();
		}
	}
	
	virtual bool HasKeyboardFocus() const override
	{
		// The spinbox is considered focused when we are typing it text.
		return SCompoundWidget::HasKeyboardFocus() || (EditableText.IsValid() && EditableText->HasKeyboardFocus());
	}

	/** Return the Value attribute */
	TAttribute<NumericType> GetValueAttribute() const { return ValueAttribute; }

	/** See the Value attribute */
	float GetValue() const { return ValueAttribute.Get(); }
	void SetValue(const TAttribute<NumericType>& InValueAttribute) 
	{
		ValueAttribute = InValueAttribute; 
		CommitValue(InValueAttribute.Get(), ECommitMethod::CommittedViaCode, ETextCommit::Default);
	}

	/** See the MinValue attribute */
	NumericType GetMinValue() const { return MinValue.Get().Get(TNumericLimits<NumericType>::Lowest()); }
	void SetMinValue(const TAttribute<TOptional<NumericType>>& InMinValue) 
	{ 
		MinValue = InMinValue;
		UpdateIsSpinRangeUnlimited();
	}

	/** See the MaxValue attribute */
	NumericType GetMaxValue() const { return MaxValue.Get().Get(TNumericLimits<NumericType>::Max()); }
	void SetMaxValue(const TAttribute<TOptional<NumericType>>& InMaxValue) 
	{ 
		MaxValue = InMaxValue; 
		UpdateIsSpinRangeUnlimited();
	}

	/** See the MinSliderValue attribute */
	bool IsMinSliderValueBound() const { return MinSliderValue.IsBound(); }

	NumericType GetMinSliderValue() const { return MinSliderValue.Get().Get(TNumericLimits<NumericType>::Lowest()); }
	void SetMinSliderValue(const TAttribute<TOptional<NumericType>>& InMinSliderValue) 
	{ 
		MinSliderValue = (InMinSliderValue.Get().IsSet()) ? InMinSliderValue : MinValue;
		UpdateIsSpinRangeUnlimited();
	}

	/** See the MaxSliderValue attribute */
	bool IsMaxSliderValueBound() const { return MaxSliderValue.IsBound(); }

	NumericType GetMaxSliderValue() const { return MaxSliderValue.Get().Get(TNumericLimits<NumericType>::Max()); }
	void SetMaxSliderValue(const TAttribute<TOptional<NumericType>>& InMaxSliderValue) 
	{ 
		MaxSliderValue = (InMaxSliderValue.Get().IsSet()) ? InMaxSliderValue : MaxValue;;
		UpdateIsSpinRangeUnlimited();
	}

	/** See the MinFractionalDigits attribute */
	int32 GetMinFractionalDigits() const { return Interface->GetMinFractionalDigits(); }
	void SetMinFractionalDigits(const TAttribute<TOptional<int32>>& InMinFractionalDigits)
	{
		Interface->SetMinFractionalDigits((InMinFractionalDigits.Get().IsSet()) ? InMinFractionalDigits.Get() : MinFractionalDigits);
	}

	/** See the MaxFractionalDigits attribute */
	int32 GetMaxFractionalDigits() const { return Interface->GetMaxFractionalDigits(); }
	void SetMaxFractionalDigits(const TAttribute<TOptional<int32>>& InMaxFractionalDigits)
	{
		Interface->SetMaxFractionalDigits((InMaxFractionalDigits.Get().IsSet()) ? InMaxFractionalDigits.Get() : MaxFractionalDigits);
	}

	/** See the AlwaysUsesDeltaSnap attribute */
	bool GetAlwaysUsesDeltaSnap() const { return AlwaysUsesDeltaSnap.Get(); }
	void SetAlwaysUsesDeltaSnap(bool bNewValue) { AlwaysUsesDeltaSnap.Set(bNewValue); }

	/** See the Delta attribute */
	NumericType GetDelta() const { return Delta.Get(); }
	void SetDelta(NumericType InDelta) { Delta.Set( InDelta ); }

	/** See the SliderExponent attribute */
	float GetSliderExponent() const { return SliderExponent.Get(); }
	void SetSliderExponent(const TAttribute<float>& InSliderExponent) { SliderExponent = InSliderExponent; }
	
	/** See the MinDesiredWidth attribute */
	float GetMinDesiredWidth() const { return SliderExponent.Get(); }
	void SetMinDesiredWidth(const TAttribute<float>& InMinDesiredWidth) { MinDesiredWidth = InMinDesiredWidth; }

protected:
	/** Make the spinbox switch to keyboard-based input mode. */
	void EnterTextMode()
	{
		TextBlock->SetVisibility( EVisibility::Collapsed );
		EditableText->SetVisibility( EVisibility::Visible );
	}
	
	/** Make the spinbox switch to mouse-based input mode. */
	void ExitTextMode()
	{
		TextBlock->SetVisibility( EVisibility::Visible );
		EditableText->SetVisibility( EVisibility::Collapsed );
	}

	/** @return the value being observed by the spinbox as a string */
	FString GetValueAsString() const
	{
		return Interface->ToString(ValueAttribute.Get());
	}

	/** @return the value being observed by the spinbox as FText - todo: spinbox FText support (reimplement me) */
	FText GetValueAsText() const
	{
		return FText::FromString(GetValueAsString());
	}

	/**
	 * Invoked when the text in the text field changes
	 *
	 * @param NewText		The value of the text in the text field
	 */
	void TextField_OnTextChanged( const FText& NewText)
	{
		if (!bIsTextChanging)
		{
			TGuardValue<bool> TextChangedGuard(bIsTextChanging, true);

			// Validate the text on change, and only accept text up until the first invalid character
			FString Data = NewText.ToString();
			int32 NumValidChars = Data.Len();

			for (int32 Index = 0; Index < Data.Len(); ++Index)
			{
				if (!Interface->IsCharacterValid(Data[Index]))
				{
					NumValidChars = Index;
					break;
				}
			}

			if (NumValidChars < Data.Len())
			{
				FString ValidData = NumValidChars > 0 ? Data.Left(NumValidChars) : GetValueAsString();
				EditableText->SetText(FText::FromString(ValidData));
			}
		}
	}
	
	/**
	 * Invoked when the text field commits its text.
	 *
	 * @param NewText		The value of text coming from the editable text field.
	 * @param CommitInfo	Information about the source of the commit
	 */
	void TextField_OnTextCommitted( const FText& NewText, ETextCommit::Type CommitInfo )
	{
		if (CommitInfo != ETextCommit::OnEnter)
		{
			ExitTextMode();
		}

		TOptional<NumericType> NewValue = Interface->FromString(NewText.ToString(), ValueAttribute.Get());
		if (NewValue.IsSet())
		{
			CommitValue(NewValue.GetValue(), CommittedViaTypeIn, CommitInfo);
		}
	}


	/** How user changed the value in the spinbox */
	enum ECommitMethod
	{
		CommittedViaSpin,
		CommittedViaTypeIn,
		CommittedViaArrowKey,
		CommittedViaCode
	};

	/**
	 * Call this method when the user's interaction has changed the value
	 *
	 * @param NewValue               Value resulting from the user's interaction
	 * @param CommitMethod           Did the user type in the value or spin to it.
	 * @param OriginalCommitInfo	 If the user typed in the value, information about the source of the commit
	 */
	void CommitValue( NumericType NewValue, ECommitMethod CommitMethod, ETextCommit::Type OriginalCommitInfo )
	{
		NumericType ValueToCommit = NewValue;
		if( CommitMethod == CommittedViaSpin || CommitMethod == CommittedViaArrowKey )
		{
			ValueToCommit = FMath::Clamp<NumericType>(ValueToCommit, GetMinSliderValue(), GetMaxSliderValue());
		}

		ValueToCommit = FMath::Clamp<NumericType>(ValueToCommit, GetMinValue(), GetMaxValue());

		// If not in spin mode, there is no need to jump to the value from the external source, continue to use the committed value.
		if(CommitMethod == CommittedViaSpin)
		{
			// This will detect if an external force has changed the value. Internally it will abandon the delta calculated this tick and update the internal value instead.
			const NumericType CurrentValue = ValueAttribute.Get();
			if(CurrentValue != CachedExternalValue)
			{
				ValueToCommit = CurrentValue;
			}
		}
		else
		{
			// Reset intermediate spin value
			IntermediateDragFractionalValue = 0.0;
		}

		const bool bAlwaysUsesDeltaSnap = GetAlwaysUsesDeltaSnap();
		// If needed, round this value to the delta. Internally the value is not held to the Delta but externally it appears to be.
		if ( CommitMethod == CommittedViaSpin || CommitMethod == CommittedViaArrowKey || bAlwaysUsesDeltaSnap)
		{
			NumericType CurrentDelta = Delta.Get();
			if( CurrentDelta != 0 )
			{
				ValueToCommit = FMath::GridSnap((double)ValueToCommit, (double)CurrentDelta); // snap numeric point value to nearest Delta
			}
		}		

		// Update the max slider value based on the current value if we're in dynamic mode
		if (SupportDynamicSliderMaxValue.Get() && ValueToCommit > GetMaxSliderValue())
		{
			ApplySliderMaxValueChanged(ValueToCommit - GetMaxSliderValue(), true);
		}
		else if (SupportDynamicSliderMinValue.Get() && ValueToCommit < GetMinSliderValue())
		{
			ApplySliderMinValueChanged(ValueToCommit - GetMinSliderValue(), true);
		}

		if( CommitMethod == CommittedViaTypeIn || CommitMethod == CommittedViaArrowKey )
		{
			OnValueCommitted.ExecuteIfBound(ValueToCommit, OriginalCommitInfo);
		}

		OnValueChanged.ExecuteIfBound(ValueToCommit);

		if (!ValueAttribute.IsBound())
		{
			ValueAttribute.Set(ValueToCommit);
		}

		// Update the cache of the external value to what the user believes the value is now.
		CachedExternalValue = ValueAttribute.Get();

		// This ensures that dragging is cleared if focus has been removed from this widget in one of the delegate calls, such as when spawning a modal dialog.
		if ( !this->HasMouseCapture() )
		{
			bDragging = false;
			PointerDraggingSliderIndex = INDEX_NONE;
		}
	}
	
	void NotifyValueCommitted() const
	{
		OnValueCommitted.ExecuteIfBound( CachedExternalValue, ETextCommit::OnEnter );
		OnEndSliderMovement.ExecuteIfBound( CachedExternalValue );
	}

	/** @return true when we are in keyboard-based input mode; false otherwise */
	bool IsInTextMode() const
	{
		return ( EditableText->GetVisibility() == EVisibility::Visible );
	}

	/** Calculates range fraction. Possible to use on full numeric range  */
	static float Fraction(NumericType InValue, NumericType InMinValue, NumericType InMaxValue)
	{
		const double HalfMax = InMaxValue*0.5;
		const double HalfMin = InMinValue*0.5;
		const double HalfVal = InValue*0.5;

		return (float)FMath::Clamp((HalfVal - HalfMin)/(HalfMax - HalfMin), 0.0, 1.0);
	}

private:

	/** The default minimum fractional digits */
	static const int32 DefaultMinFractionalDigits;

	/** The default maximum fractional digits */
	static const int32 DefaultMaxFractionalDigits;

	TAttribute<NumericType> ValueAttribute;
	FOnValueChanged OnValueChanged;
	FOnValueCommitted OnValueCommitted;
	FSimpleDelegate OnBeginSliderMovement;
	FOnValueChanged OnEndSliderMovement;
	TSharedPtr<STextBlock> TextBlock;
	TSharedPtr<SEditableText> EditableText;

	/** Interface that defines conversion functionality for the templated type */
	TSharedPtr< INumericTypeInterface<NumericType> > Interface;

	/** True when no range is specified, spinner can be spun indefinitely */
	bool bUnlimitedSpinRange;
	void UpdateIsSpinRangeUnlimited()
	{
		bUnlimitedSpinRange = !((MinValue.Get().IsSet() && MaxValue.Get().IsSet()) || (MinSliderValue.Get().IsSet() && MaxSliderValue.Get().IsSet()));
	}

	const FSpinBoxStyle* Style;

	const FSlateBrush* BackgroundHoveredBrush;
	const FSlateBrush* BackgroundBrush;
	const FSlateBrush* ActiveFillBrush;
	const FSlateBrush* InactiveFillBrush;

	float DistanceDragged;
	TAttribute<NumericType> Delta;
	TAttribute<int32> ShiftMouseMovePixelPerDelta;
	TAttribute<int32> LinearDeltaSensitivity;
	TAttribute<float> SliderExponent;
	TAttribute<NumericType> SliderExponentNeutralValue;
	TAttribute< TOptional<NumericType> > MinValue;
	TAttribute< TOptional<NumericType> > MaxValue;
	TAttribute< TOptional<NumericType> > MinSliderValue;
	TAttribute< TOptional<NumericType> > MaxSliderValue;
	TAttribute< TOptional<int32> > MinFractionalDigits;
	TAttribute< TOptional<int32> > MaxFractionalDigits;
	TAttribute<bool> AlwaysUsesDeltaSnap;
	TAttribute<bool> SupportDynamicSliderMaxValue;
	TAttribute<bool> SupportDynamicSliderMinValue;
	FOnDynamicSliderMinMaxValueChanged OnDynamicSliderMaxValueChanged;
	FOnDynamicSliderMinMaxValueChanged OnDynamicSliderMinValueChanged;

	/** Prevents the spinbox from being smaller than desired in certain cases (e.g. when it is empty) */
	TAttribute<float> MinDesiredWidth;
	float GetTextMinDesiredWidth() const
	{
		return FMath::Max(0.0f, MinDesiredWidth.Get() - Style->ArrowsImage.ImageSize.X);
	}

	/** Check whether a typed character is valid */
	bool IsCharacterValid(TCHAR InChar) const
	{
		return Interface->IsCharacterValid(InChar);
	}

	/** Update the IntermediateDragFractionalValue and return the updated CurrentValue */
	NumericType UpdateDraggingValues(NumericType CurrentValue)
	{
		if (TIsIntegral<NumericType>::Value)
		{						
			double IntegralPart = 0.f;
			IntermediateDragFractionalValue = FMath::Modf(IntermediateDragFractionalValue, &IntegralPart);
						
			return CurrentValue + static_cast<NumericType>(IntegralPart);
		}
		else
		{
			NumericType TmpValue = CurrentValue + IntermediateDragFractionalValue;
			IntermediateDragFractionalValue = 0.0;
			return TmpValue;
		}
	}

	/** Whether the user is dragging the slider */
	bool bDragging;

	/** Tracks which cursor is currently dragging the slider (e.g., the mouse cursor or a specific finger) */
	int32 PointerDraggingSliderIndex;
	
	/** Cached mouse position to restore after scrolling. */
	FIntPoint CachedMousePosition;

	/*
	 * This is the cached value the user believes it to be. Used for identifying external forces on the spinbox
	 * and syncing the internal value to them. Synced when a value is committed to the spinbox. 
	 */
	NumericType CachedExternalValue;
	
	/** The state of CachedExternalValue before a drag operation was started */
	NumericType PreDragValue;

	/**
	 * This value represents the fractional part of the spinbox when using integers. 
	 * The spinbox will always count using floats between values, this is important to keep it flowing smoothly and feeling right, 
	 * and most importantly not conflicting with the user truncating the value to an int.
	 */
	double IntermediateDragFractionalValue;

	/** Re-entrant guard for the text changed handler */
	bool bIsTextChanging;

	/*
	 * Holds whether or not to prevent throttling during mouse capture
	 * When true, the viewport will be updated with every single change to the value during dragging
	 */
	bool bPreventThrottling;
};

template<typename NumericType>
const int32 SSpinBox<NumericType>::DefaultMinFractionalDigits = 1;

template<typename NumericType>
const int32 SSpinBox<NumericType>::DefaultMaxFractionalDigits = 6;
