#include "SJadeboundProgressionPanel.h"
#include "Presentation/StackSaleQuote.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateBoxBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Layout/WidgetPath.h"
#include "Layout/SlateRect.h"
#include "HAL/PlatformTime.h"
#include "Engine/Texture2D.h"
#if WITH_EDITOR
#include "TextureCompiler.h"
#endif
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "CoreGlobals.h"
#include "Fonts/FontMeasure.h"
#include "Misc/PackageName.h"
#include "UObject/GCObject.h"
#include "UObject/UObjectGlobals.h"
#include "InputCoreTypes.h"
#include "Input/DragAndDrop.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include <initializer_list>

#define LOCTEXT_NAMESPACE "JadeboundProgressionPanel"

namespace
{
// Original interface palette: charcoal lining, worn bronze, jade accents and warm ivory.
const FLinearColor PanelDark(.016f, .035f, .043f, 1.f);
const FLinearColor SectionDark(.023f, .054f, .065f, 1.f);
const FLinearColor WellDark(.006f, .016f, .023f, 1.f);
const FLinearColor Jade(.043f, .18f, .126f, 1.f);
const FLinearColor SelectedJade(.064f, .27f, .187f, 1.f);
const FLinearColor Gold(.80f, .62f, .32f, 1.f);
const FLinearColor Bronze(.43f, .29f, .13f, 1.f);
const FLinearColor BronzeDim(.13f, .085f, .044f, 1.f);
const FLinearColor Ink(.91f, .90f, .81f, 1.f);
const FLinearColor Muted(.60f, .65f, .60f, 1.f);
const FLinearColor QuietIndex(.32f, .37f, .34f, 1.f);
const FLinearColor Warning(.98f, .66f, .28f, 1.f);
const FLinearColor Good(.30f, .74f, .52f, 1.f);
const FSlateRoundedBoxBrush CardBrush(SectionDark, 2.f, BronzeDim, 1.f);
const FSlateRoundedBoxBrush InsetBrush(WellDark, 2.f, BronzeDim, 1.f);
const FSlateRoundedBoxBrush FrameBrush(PanelDark, 4.f, Bronze, 1.f);
const FSlateRoundedBoxBrush BadgeBrush(FLinearColor(.018f,.031f,.031f,1.f), 1.f, BronzeDim, 1.f);

// HUD and spatial pack share the existing native-pixel floor. The pack's
// layout-only owner fits its three panels to actual allotted geometry.
float GameplayHudScaleFloor(TWeakObjectPtr<AJadeboundProgressionActor> Weak)
{
    const AJadeboundProgressionActor* Model = Weak.Get();
    UWorld* World = IsValid(Model) ? Model->GetWorld() : nullptr;
    UGameViewportClient* RuntimeViewport = IsValid(World) ? World->GetGameViewport() : nullptr;
    if (!IsValid(RuntimeViewport) || RuntimeViewport->GetWorld() != World) return 1.f;
    FVector2D ViewportSize = FVector2D::ZeroVector;
    RuntimeViewport->GetViewportSize(ViewportSize);
    if (!FMath::IsFinite(ViewportSize.X) || !FMath::IsFinite(ViewportSize.Y) ||
        ViewportSize.X < 1.0 || ViewportSize.Y < 1.0 ||
        ViewportSize.X >= MAX_int32 || ViewportSize.Y >= MAX_int32) return 1.f;
    const UUserInterfaceSettings* Settings = GetDefault<UUserInterfaceSettings>();
    if (!IsValid(Settings)) return 1.f;
    const float GameUIScale = Settings->GetDPIScaleBasedOnSize(
        FIntPoint(FMath::RoundToInt(ViewportSize.X),FMath::RoundToInt(ViewportSize.Y)));
    if (!FMath::IsFinite(GameUIScale) || GameUIScale <= SMALL_NUMBER) return 1.f;
    return FMath::Max(1.f,GameUIScale) / GameUIScale;
}

// Optional, root-approved originals only. No render/import/texture creation here.
// A shared widget-lifetime GC referencer keeps every brush resource alive.
class FJadeboundOriginalArt final : public FGCObject
{
public:
    FJadeboundOriginalArt()
    {
        const TCHAR* Names[] = {TEXT("T_UI_Glaive_v002"), TEXT("T_UI_Spear_v002"),
            TEXT("T_UI_IronOre_v001"), TEXT("T_UI_Moonleaf_v001"),
            TEXT("T_UI_JadeParcel_v001"), TEXT("T_UI_JadeShard_v001"),
            TEXT("T_UI_RestorativeBalm_v003")};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
            if (UTexture2D* Texture = LoadOptional(Names[Index]))
                Items.Add(Index + 1, FSlateImageBrush(Texture, FVector2D(512.f,512.f)));
        // Skill identities are separate from persisted item IDs.
        const TCHAR* SkillNames[] = {TEXT("T_UI_Guard_Original_v001"), TEXT("T_UI_Current_Original_v001")};
        for (int32 Identity = 0; Identity < UE_ARRAY_COUNT(SkillNames); ++Identity)
            if (UTexture2D* Texture = LoadOptional(SkillNames[Identity]))
                Skills.Add(Identity, FSlateImageBrush(Texture, FVector2D(512.f,512.f)));
        if (UTexture2D* Texture = LoadOptional(TEXT("T_UI_AgedBronzeHUDRail_v001")))
        {
            // Independent 1024x128 rail: 64/24/64/24 source-pixel slices.
            Rail = FSlateBoxBrush(Texture,FVector2D(1024.f,128.f),FMargin(.0625f,.1875f,.0625f,.1875f));
            bHasRail = true;
        }
        UTexture2D* FrameTexture = LoadRuntimeFrame();
        if (!FrameTexture) FrameTexture = LoadOptional(TEXT("T_UI_TravelerFrame_v004"));
        if (!FrameTexture) FrameTexture = LoadOptional(TEXT("T_UI_TravelerFrame_v003"));
        bHasRefinedFrame = FrameTexture != nullptr;
        if (!FrameTexture) FrameTexture = LoadOptional(TEXT("T_UI_TravelerFrame_v001"));
        if (FrameTexture)
        {
            // Actual 256x256 runtime pixels preserve 32-unit corners at scale 1.
            // Original 512x512 fallback pixels retain their 64-unit corners;
            // ImageSize does not scale UTexture-backed Box margin positions.
            Frame = FSlateBoxBrush(FrameTexture,FVector2D(256.f,256.f),FMargin(.125f));
            bHasFrame = true;
        }
        UTexture2D* InsetTexture = LoadOptional(TEXT("T_UI_TravelerInset_v003"));
        if (!InsetTexture) InsetTexture = LoadOptional(TEXT("T_UI_TravelerInset_v001"));
        if (InsetTexture)
        {
            // Quiet lining is stretched, never silently treated as a tile.
            Inset = FSlateImageBrush(InsetTexture,FVector2D(512.f,512.f));
            bHasInset = true;
        }
        const TCHAR* MerchantNames[] = {TEXT("T_UI_MerchantButton_Normal_Runtime_v002"),
            TEXT("T_UI_MerchantButton_Hover_Runtime_v002"), TEXT("T_UI_MerchantButton_Pressed_Runtime_v002"),
            TEXT("T_UI_MerchantButton_Disabled_Runtime_v002")};
        UTexture2D* MerchantTextures[UE_ARRAY_COUNT(MerchantNames)] = {};
        bool bCompleteMerchantSet = true;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(MerchantNames); ++Index)
        {
            MerchantTextures[Index] = LoadMerchantButton(MerchantNames[Index]);
            bCompleteMerchantSet = bCompleteMerchantSet && MerchantTextures[Index] != nullptr;
        }
        if (bCompleteMerchantSet)
        {
            // Slate uses actual 128x48 texture size: 12/8/12/8 source slices.
            // Label and content padding stay on the existing SButton.
            for (int32 Index = 0; Index < UE_ARRAY_COUNT(MerchantNames); ++Index)
                MerchantButtons[Index] = FSlateBoxBrush(MerchantTextures[Index],FVector2D(128.f,48.f),
                    FMargin(.09375f,.16666667f,.09375f,.16666667f));
            bHasMerchantButtons = true;
        }
    }
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        for (TObjectPtr<UTexture2D>& Texture : Textures) Collector.AddReferencedObject(Texture);
    }
    virtual FString GetReferencerName() const override { return TEXT("JadeboundOriginalInterfaceArt"); }
    const FSlateBrush* Item(int32 Id) const { return Items.Find(Id); }
    const FSlateBrush* Skill(int32 Identity) const { return Skills.Find(Identity); }
    const FSlateBrush* HudRail() const { return bHasRail ? &Rail : nullptr; }
    const FSlateBrush* Surface(bool bFrame) const
    { return bFrame ? bHasFrame ? &Frame : nullptr : bHasInset ? &Inset : nullptr; }
    bool UsesRefinedFrame() const { return bHasRefinedFrame; }
    const FSlateBrush* MerchantButton(int32 State) const
    { return bHasMerchantButtons && State >= 0 && State < UE_ARRAY_COUNT(MerchantButtons) ? &MerchantButtons[State] : nullptr; }
private:
    TArray<TObjectPtr<UTexture2D>> Textures;
    TMap<int32,FSlateBrush> Items, Skills;
    FSlateBrush Frame, Inset, Rail;
    FSlateBrush MerchantButtons[4];
    bool bHasFrame = false, bHasInset = false, bHasRefinedFrame = false, bHasRail = false;
    bool bHasMerchantButtons = false;
    UTexture2D* LoadRuntimeFrame()
    {
        if (!IsInGameThread()) return nullptr;
        const TCHAR* Package = TEXT("/Game/Jadebound/Interface/OriginalFrameRuntimeV005/T_UI_TravelerFrame_Runtime_v005");
        if (!FPackageName::DoesPackageExist(Package)) return nullptr;
        const FString ObjectPath = FString(Package) + TEXT(".T_UI_TravelerFrame_Runtime_v005");
        UTexture2D* Texture = LoadObject<UTexture2D>(nullptr,*ObjectPath);
        if (!IsValid(Texture)) return nullptr;
        // Retain before optional editor texture compilation can collect objects.
        Textures.Add(Texture);
#if WITH_EDITOR
        FTextureCompilingManager::Get().FinishCompilation({ Texture });
        if (!IsValid(Texture) || Texture->IsCompiling()) return nullptr;
#endif
        return Texture->GetSizeX() == 256 && Texture->GetSizeY() == 256 ? Texture : nullptr;
    }
    UTexture2D* LoadMerchantButton(const TCHAR* Name)
    {
        if (!IsInGameThread()) return nullptr;
        const FString Package = FString(TEXT("/Game/Jadebound/UI/OriginalMerchantButtonsV002/")) + Name;
        if (!FPackageName::DoesPackageExist(Package)) return nullptr;
        UTexture2D* Texture = LoadObject<UTexture2D>(nullptr,*(Package + TEXT(".") + Name));
        if (!IsValid(Texture)) return nullptr;
        // Retain before an editor compilation callback can collect resources.
        Textures.Add(Texture);
#if WITH_EDITOR
        FTextureCompilingManager::Get().FinishCompilation({ Texture });
        if (!IsValid(Texture) || Texture->IsCompiling()) return nullptr;
#endif
        return Texture->GetSizeX() == 128 && Texture->GetSizeY() == 48 ? Texture : nullptr;
    }
    UTexture2D* LoadOptional(const TCHAR* Name)
    {
        const bool bOriginalSkill = FCString::Strcmp(Name,TEXT("T_UI_Guard_Original_v001")) == 0
            || FCString::Strcmp(Name,TEXT("T_UI_Current_Original_v001")) == 0;
        const bool bOriginalRail = FCString::Strcmp(Name,TEXT("T_UI_AgedBronzeHUDRail_v001")) == 0;
        const bool bRefinedWeapon = FCString::Strcmp(Name,TEXT("T_UI_Glaive_v002")) == 0
            || FCString::Strcmp(Name,TEXT("T_UI_Spear_v002")) == 0;
        const bool bNarrowFrame = FCString::Strcmp(Name,TEXT("T_UI_TravelerFrame_v004")) == 0;
        const bool bRefinedInterface = FCString::Strcmp(Name,TEXT("T_UI_RestorativeBalm_v003")) == 0
            || FCString::Strcmp(Name,TEXT("T_UI_TravelerFrame_v003")) == 0
            || FCString::Strcmp(Name,TEXT("T_UI_TravelerInset_v003")) == 0;
        const FString Package = FString(bOriginalSkill || bOriginalRail
            ? TEXT("/Game/Jadebound/UI/ArtV005/") : bNarrowFrame
            ? TEXT("/Game/Jadebound/Interface/OriginalArtV004/")
            : bRefinedInterface ? TEXT("/Game/Jadebound/Interface/OriginalArtV003/")
            : bRefinedWeapon ? TEXT("/Game/Jadebound/Interface/OriginalArtV002/")
            : TEXT("/Game/Jadebound/Interface/OriginalArtV001/")) + Name;
        // Missing packages use the procedural fallback without failed-load spam.
        if (!FPackageName::DoesPackageExist(Package)) return nullptr;
        const FString ObjectPath = Package + TEXT(".") + Name;
        UTexture2D* Texture = LoadObject<UTexture2D>(nullptr,*ObjectPath);
        if (!Texture) return nullptr;
#if WITH_EDITOR
        // Newly loaded editor textures may still expose placeholder dimensions.
        // Finish only this texture during ordinary widget construction.
        if (!IsInGameThread() || !IsValid(Texture)) return nullptr;
        FTextureCompilingManager::Get().FinishCompilation({ Texture });
        if (!IsValid(Texture) || Texture->IsCompiling()) return nullptr;
#endif
        const int32 Width = Texture->GetSizeX(), Height = Texture->GetSizeY();
        const bool bDimensions = bOriginalRail ? Width == 1024 && Height == 128 :
            bOriginalSkill ? Width == 512 && Height == 512 : Width > 0 && Width == Height;
        if (!bDimensions) return nullptr;
        Textures.Add(Texture);
        return Texture;
    }
};

TSharedRef<FJadeboundOriginalArt> AcquireOriginalArt()
{
    // Weak process cache owns no UObject after the last HUD/panel/tooltip dies.
    // Widgets hold strong references; loads happen only on first construction.
    static TWeakPtr<FJadeboundOriginalArt> Cache;
    TSharedPtr<FJadeboundOriginalArt> Art = Cache.Pin();
    if (!Art.IsValid()) { Art = MakeShared<FJadeboundOriginalArt>(); Cache = Art; }
    return Art.ToSharedRef();
}

// Original woven lining and cloud-hook metalwork. Paint only, no hit targets.
// Geometry is bounded independently of viewport or content size.
void PaintMartialTrim(const FGeometry& Geometry, FSlateWindowElementList& Elements,
    int32 Layer, const FWidgetStyle& Style, bool bEnabled, bool bFrame, bool bQuiet = false)
{
    if (!FSlateApplication::IsInitialized()) return;
    const FVector2D Local = Geometry.GetLocalSize();
    const float Width = static_cast<float>(Local.X), Height = static_cast<float>(Local.Y);
    if (!FMath::IsFinite(Width) || !FMath::IsFinite(Height) || Width < 24.f || Height < 24.f) return;
    const FLinearColor Tint = Style.GetColorAndOpacityTint();
    const FLinearColor MetalLight(.73f,.56f,.29f,1.f), MetalDark(.12f,.075f,.031f,1.f);
    TArray<FSlateVertex> Vertices; TArray<SlateIndex> Indices;
    Vertices.Reserve(2600); Indices.Reserve(3900);
    auto Polygon = [&](std::initializer_list<FVector2f> Shape,FLinearColor Color)
    {
        const int32 Start=Vertices.Num();
        for (const FVector2f& P:Shape)
            Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Geometry.GetAccumulatedRenderTransform(),
                P,FVector2f(.5f,.5f),(Color*Tint).ToFColor(true)));
        for (int32 N=1;N+1<static_cast<int32>(Shape.size());++N)
        { Indices.Add(static_cast<SlateIndex>(Start)); Indices.Add(static_cast<SlateIndex>(Start+N)); Indices.Add(static_cast<SlateIndex>(Start+N+1)); }
    };
    auto Stroke = [&](FVector2f A,FVector2f B,float Weight,FLinearColor Color)
    {
        const FVector2f D=B-A; const float Span=FMath::Sqrt(D.X*D.X+D.Y*D.Y);
        if (Span<=SMALL_NUMBER) return;
        const FVector2f N(-D.Y*Weight/(2.f*Span),D.X*Weight/(2.f*Span));
        Polygon({A+N,B+N,B-N,A-N},Color);
    };
    // Constant-pitch seam weave over the retained original lining. Text
    // centres remain quiet; these finite centred bands never stretch threads.
    // At most 176 stitch sites versus the prior 192-site interior grid.
    if (!bQuiet)
    {
        auto Stitch = [&](FVector2f At,bool Turn)
        {
            auto Offset=[&](float X,float Y) { return At+(Turn ? FVector2f(Y,X) : FVector2f(X,Y)); };
            Stroke(Offset(-2.8f,-.6f),Offset(-.2f,1.7f),.55f,FLinearColor(.10f,.22f,.25f,.065f));
            Stroke(Offset(0.f,-1.f),Offset(2.8f,-.2f),.65f,FLinearColor(.004f,.012f,.018f,.11f));
        };
        const int32 Across=FMath::Clamp(FMath::FloorToInt((Width-32.f)/12.f),0,48);
        const int32 Down=FMath::Clamp(FMath::FloorToInt((Height-32.f)/12.f),0,40);
        const float StartX=(Width-FMath::Max(0,Across-1)*12.f)*.5f;
        const float StartY=(Height-FMath::Max(0,Down-1)*12.f)*.5f;
        for (int32 N=0;N<Across;++N)
        { const float X=StartX+N*12.f;Stitch({X,12.f},false);Stitch({X,Height-12.f},false); }
        for (int32 N=0;N<Down;++N)
        { const float Y=StartY+N*12.f;Stitch({12.f,Y},true);Stitch({Width-12.f,Y},true); }
    }
    // Continuous stepped edge: light at top/left, shadow at bottom/right.
    Stroke({5.f,4.f},{Width-5.f,4.f},1.3f,MetalLight);
    Stroke({4.f,5.f},{4.f,Height-5.f},1.3f,MetalLight*.78f);
    Stroke({5.f,Height-4.f},{Width-5.f,Height-4.f},1.5f,MetalDark);
    Stroke({Width-4.f,5.f},{Width-4.f,Height-5.f},1.5f,MetalDark);
    Stroke({7.f,7.f},{Width-7.f,7.f},.6f,BronzeDim);
    Stroke({7.f,Height-7.f},{Width-7.f,Height-7.f},.6f,MetalLight*.43f);
    const float Length=FMath::Min(bQuiet ? 6.f : bFrame ? 27.f : 12.f,FMath::Min(Width,Height)*.27f);
    for (int32 Corner=0;Corner<4;++Corner)
    {
        const float SX=(Corner&1) ? -1.f : 1.f, SY=(Corner&2) ? -1.f : 1.f;
        const float X=SX>0.f ? 4.f : Width-4.f, Y=SY>0.f ? 4.f : Height-4.f;
        auto P=[&](float U,float V) { return FVector2f(X+SX*U,Y+SY*V); };
        if (bQuiet)
        {
            Stroke(P(0.f,Length),P(Length,0.f),.75f,BronzeDim);
            Stroke(P(1.f,Length),P(Length,1.f),.45f,MetalLight*.48f);
            continue;
        }
        // Two authored hooks meet at an inset jade rivet, without copied scroll art.
        const float K=Length/27.f;
        Polygon({P(0.f,0.f),P(Length,0.f),P(Length,3.f*K),P(0.f,3.f*K)},Bronze);
        Polygon({P(5.f*K,3.f*K),P(9.f*K,3.f*K),P(9.f*K,8.f*K),P(5.f*K,8.f*K)},Bronze);
        Polygon({P(0.f,0.f),P(3.f*K,0.f),P(3.f*K,Length),P(0.f,Length)},MetalDark);
        Polygon({P(3.f*K,5.f*K),P(8.f*K,5.f*K),P(8.f*K,9.f*K),P(3.f*K,9.f*K)},MetalDark);
        Stroke(P(1.f*K,1.f*K),P(Length-2.f*K,1.f*K),.8f,MetalLight);
        Stroke(P(1.f*K,1.f*K),P(1.f*K,Length-2.f*K),.7f,MetalLight);
        Stroke(P(11.f*K,5.f*K),P(17.f*K,5.f*K),.65f,MetalLight*.64f);
        Stroke(P(17.f*K,5.f*K),P(17.f*K,9.f*K),.65f,Bronze);
        Stroke(P(17.f*K,9.f*K),P(13.f*K,9.f*K),.65f,MetalDark);
        const FVector2f Curl[] = {P(5.f*K,10.f*K),P(7.f*K,8.f*K),P(11.f*K,8.f*K),P(13.f*K,11.f*K),
            P(13.f*K,15.f*K),P(10.f*K,17.f*K),P(7.f*K,15.f*K),P(7.f*K,13.f*K)};
        for (int32 N=1;N<UE_ARRAY_COUNT(Curl);++N)
        {
            Stroke(Curl[N-1]+FVector2f(SX*.55f,SY*.55f),Curl[N]+FVector2f(SX*.55f,SY*.55f),.8f,MetalDark);
            Stroke(Curl[N-1],Curl[N],.45f,MetalLight*.58f);
        }
        const float R=2.f*K;
        Polygon({P(6.f*K,6.f*K-R),P(6.f*K+R,6.f*K),P(6.f*K,6.f*K+R),P(6.f*K-R,6.f*K)},SelectedJade);
        Stroke(P(5.f*K,6.f*K),P(6.f*K,5.f*K),.6f,FLinearColor(.28f,.55f,.43f,1.f));
    }
    const FSlateResourceHandle Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
    FSlateDrawElement::MakeCustomVerts(Elements,Layer,Resource,Vertices,Indices,nullptr,0,0,
        bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect);
}

// Backing and original chrome share the content's lifetime. The procedural
// backing remains opaque under a frame's transparent centre.
class SJadeboundArtBorder final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundArtBorder) : _Frame(true), _Padding(0.f), _Fallback(nullptr), _QuietTrim(false), _HudRail(false) {}
        SLATE_ARGUMENT(bool, Frame)
        SLATE_ARGUMENT(FMargin, Padding)
        SLATE_ARGUMENT(const FSlateBrush*, Fallback)
        SLATE_ARGUMENT(bool, QuietTrim)
        SLATE_ARGUMENT(bool, HudRail)
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Art = AcquireOriginalArt(); bFrame = Args._Frame; bQuietTrim = Args._QuietTrim; bHudRail = Args._HudRail;
        Fallback = Args._Fallback ? Args._Fallback : bFrame ? &FrameBrush : &InsetBrush;
        ChildSlot.Padding(Args._Padding)[Args._Content.Widget];
    }
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Cull,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override
    {
        const auto Effect = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
        FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(),Fallback,Effect,Fallback->GetTint(Style) * Style.GetColorAndOpacityTint());
        const FSlateBrush* Original = bHudRail ? Art->HudRail() : nullptr;
        const bool bOriginalRail = Original != nullptr;
        if (!Original) Original = Art->Surface(bFrame);
        if (Original)
            FSlateDrawElement::MakeBox(Elements,Layer+1,Geometry.ToPaintGeometry(),Original,Effect,Original->GetTint(Style) * Style.GetColorAndOpacityTint());
        if (!bOriginalRail && !(bFrame && Art->UsesRefinedFrame()))
            PaintMartialTrim(Geometry,Elements,Layer+2,Style,bEnabled,bFrame,bQuietTrim);
        return SCompoundWidget::OnPaint(Args,Geometry,Cull,Elements,Layer+3,Style,bEnabled);
    }
private:
    TSharedPtr<FJadeboundOriginalArt> Art;
    const FSlateBrush* Fallback = nullptr;
    bool bFrame = true, bQuietTrim = false, bHudRail = false;
};

// Pure backing replacement for section cards. Child padding and content
// semantics match the previous SBorder; no input or gameplay handlers are added.
class SJadeboundSectionBorder final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundSectionBorder) : _Padding(18.f) {}
        SLATE_ARGUMENT(FMargin, Padding)
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args) { ChildSlot.Padding(Args._Padding)[Args._Content.Widget]; }
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Cull,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override
    {
        const auto Effect=bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
        FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(),&CardBrush,Effect,
            CardBrush.GetTint(Style) * Style.GetColorAndOpacityTint());
        PaintMartialTrim(Geometry,Elements,Layer+1,Style,bEnabled,false);
        return SCompoundWidget::OnPaint(Args,Geometry,Cull,Elements,Layer+2,Style,bEnabled);
    }
};

class SJadeboundFocusRing final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundFocusRing) {}
        SLATE_ARGUMENT(TWeakPtr<SWidget>, Target)
    SLATE_END_ARGS()
    // Sibling focus is not an attribute; repaint this tiny leaf under invalidation caching.
    void Construct(const FArguments& Args) { Target = Args._Target; ForceVolatile(true); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool) const override
    {
        const TSharedPtr<SWidget> Focused = Target.Pin();
        if (Focused.IsValid() && Focused->HasKeyboardFocus())
            FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(),&Outline,
                ESlateDrawEffect::None,Outline.GetTint(Style) * Style.GetColorAndOpacityTint());
        return Layer;
    }
private:
    TWeakPtr<SWidget> Target;
    FSlateRoundedBoxBrush Outline{FLinearColor::Transparent,6.f,Gold,2.f};
};

TSharedRef<SWidget> WithFocusRing(TSharedRef<SWidget> Button)
{
    return SNew(SOverlay)
        + SOverlay::Slot()[Button]
        + SOverlay::Slot()[SNew(SJadeboundFocusRing).Target(TWeakPtr<SWidget>(Button)).Visibility(EVisibility::HitTestInvisible)];
}

TSharedRef<STextBlock> Text(const FText& Value, float Size = 14.f,
    FLinearColor Color = Ink, bool bBold = false, int32 Tracking = 0)
{
    const FName Typeface = bBold ? (Size >= 17.f ? TEXT("Medium") : TEXT("Bold"))
        : (Size >= 24.f ? TEXT("Light") : TEXT("Regular"));
    // Protect small text after the existing viewport ScaleBox reduces the canvas.
    const float ReadableSize = Size >= 12.f && Size < 17.f ? Size + 1.f : Size;
    FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(Typeface, ReadableSize);
    Font.LetterSpacing = Tracking;
    return SNew(STextBlock).Text(Value).Font(Font).ColorAndOpacity(Color)
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis);
}

FLinearColor ItemAccent(int32 Id)
{
    switch (Id)
    {
    case 1: return FLinearColor(.23f,.64f,.43f,1.f);
    case 2: return FLinearColor(.55f,.67f,.72f,1.f);
    case 3: return FLinearColor(.54f,.47f,.34f,1.f);
    case 4: return FLinearColor(.50f,.65f,.28f,1.f);
    case 5: return Gold;
    case 6: return FLinearColor(.20f,.76f,.61f,1.f);
    case 7: return FLinearColor(.39f,.72f,.48f,1.f);
    default: return Muted;
    }
}

// Type accents are presentation only; no invented rarity/drop-rate statistic.
FText ItemKind(int32 Id)
{
    switch (Id)
    {
    case 1: case 2: return LOCTEXT("WeaponKind", "WEAPON");
    case 3: case 4: return LOCTEXT("MaterialKind", "MATERIAL");
    case 5: return LOCTEXT("QuestKind", "QUEST ITEM");
    case 6: return LOCTEXT("RewardKind", "JADE REWARD");
    case 7: return LOCTEXT("BalmKind", "CONSUMABLE");
    default: return FText::GetEmpty();
    }
}

FText ItemLore(int32 Id)
{
    switch (Id)
    {
    case 1: return LOCTEXT("GlaiveLore", "A jade-edged polearm, balanced for a sweeping cut.");
    case 2: return LOCTEXT("SpearLore", "Ashwood, tempered iron and a binding of jade thread.");
    case 3: return LOCTEXT("OreLore", "Dense quarry iron threaded with green mineral.");
    case 4: return LOCTEXT("LeafLore", "A pale leaf veined with silver, prized by courtyard herbalists.");
    case 5: return LOCTEXT("ParcelLore", "The merchant's stolen parcel. Return it to complete the request.");
    case 6: return LOCTEXT("ShardLore", "A fragment of luminous jade, earned through your journey.");
    case 7: return LOCTEXT("BalmLore", "Courtyard salve. Use one to recover 25% of maximum health, up to full health. Full health preserves it.");
    default: return FText::GetEmpty();
    }
}

// Original texture when imported; original filled silhouette otherwise.
// Resource acquisition is construction-only; paint never loads or creates it.
class SJadeboundItemArt final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundItemArt) : _ItemId(0), _Size(64.f) {}
        SLATE_ARGUMENT(int32, ItemId)
        SLATE_ARGUMENT(float, Size)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args) { ItemId = Args._ItemId; Size = Args._Size; ArtResources = AcquireOriginalArt(); }
private:
    int32 ItemId = 0;
    float Size = 64.f;
    TSharedPtr<FJadeboundOriginalArt> ArtResources;
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override
    {
        const auto Local = Geometry.GetLocalSize();
        const FVector2f Extent(static_cast<float>(Local.X), static_cast<float>(Local.Y));
        const FLinearColor Tint = Style.GetColorAndOpacityTint();
        if (const FSlateBrush* Original = ArtResources->Item(ItemId))
        {
            FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(),Original,
                bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,Original->GetTint(Style) * Tint);
            return Layer;
        }
        TArray<FSlateVertex> Vertices;
        TArray<SlateIndex> Indices;
        Vertices.Reserve(192); Indices.Reserve(384);
        auto Polygon = [&](std::initializer_list<FVector2f> Shape, FLinearColor Color)
        {
            const int32 Start = Vertices.Num();
            for (const FVector2f& P : Shape)
                Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
                    Geometry.GetAccumulatedRenderTransform(), FVector2f(P.X * Extent.X, P.Y * Extent.Y),
                    FVector2f(.5f,.5f), (Color * Tint).ToFColor(true)));
            for (int32 N = 1; N + 1 < static_cast<int32>(Shape.size()); ++N)
            {
                Indices.Add(static_cast<SlateIndex>(Start));
                Indices.Add(static_cast<SlateIndex>(Start + N));
                Indices.Add(static_cast<SlateIndex>(Start + N + 1));
            }
        };
        // Material-detail strokes are convex strips in the same bounded mesh.
        auto Stroke = [&](FVector2f A, FVector2f B, float Width, FLinearColor Color)
        {
            const FVector2f D = B - A;
            const float Length = FMath::Sqrt(D.X * D.X + D.Y * D.Y);
            if (Length <= SMALL_NUMBER) return;
            const FVector2f N(-D.Y * Width / (2.f * Length), D.X * Width / (2.f * Length));
            Polygon({A + N, B + N, B - N, A - N}, Color);
        };
        // Every icon uses upper-left key light and a darker lower-right face.
        const FLinearColor Wood(.24f,.13f,.062f,1.f), Steel(.52f,.65f,.64f,1.f);
        const FLinearColor WoodLight(.47f,.29f,.12f,1.f), WoodDeep(.13f,.073f,.035f,1.f);
        const FLinearColor JadeLight(.36f,.72f,.49f,1.f), JadeDeep(.034f,.19f,.14f,1.f);
        const FLinearColor BronzeLight(.78f,.54f,.23f,1.f), BronzeShade(.30f,.18f,.065f,1.f);
        const FLinearColor Edge(.88f,.89f,.78f,1.f), JadeFace(.10f,.40f,.27f,1.f);
        switch (ItemId)
        {
        case 1:
            // Split ashwood shaft, rubbed grain, heel ferrule and wound bronze grip.
            Polygon({{.22f,.90f},{.27f,.94f},{.73f,.24f},{.68f,.20f}}, WoodDeep);
            Polygon({{.24f,.90f},{.26f,.92f},{.71f,.23f},{.69f,.22f}}, Wood);
            Stroke({.25f,.87f},{.65f,.27f},.009f,WoodLight);
            Stroke({.263f,.83f},{.47f,.53f},.004f,BronzeShade);
            Stroke({.282f,.83f},{.45f,.58f},.003f,WoodDeep);
            Polygon({{.215f,.90f},{.27f,.945f},{.30f,.90f},{.245f,.856f}}, BronzeShade);
            Stroke({.225f,.89f},{.279f,.93f},.014f,BronzeLight);
            Polygon({{.38f,.66f},{.425f,.70f},{.50f,.59f},{.455f,.55f}}, BronzeShade);
            Stroke({.392f,.648f},{.435f,.678f},.017f,BronzeLight);
            Stroke({.414f,.615f},{.457f,.645f},.014f,Gold);
            Stroke({.436f,.583f},{.479f,.613f},.014f,BronzeLight);
            Polygon({{.57f,.35f},{.635f,.39f},{.715f,.28f},{.65f,.235f}}, BronzeShade);
            Stroke({.585f,.34f},{.65f,.383f},.017f,BronzeLight);
            // Jade blade is green, with narrow bevels rather than a white face.
            Polygon({{.60f,.32f},{.58f,.19f},{.65f,.068f},{.79f,.075f},{.875f,.17f},{.76f,.34f}}, BronzeShade);
            Polygon({{.61f,.30f},{.604f,.194f},{.665f,.094f},{.78f,.10f},{.839f,.172f},{.741f,.316f}}, JadeFace);
            Polygon({{.665f,.094f},{.78f,.10f},{.739f,.194f},{.625f,.251f}}, JadeLight);
            Polygon({{.739f,.194f},{.839f,.172f},{.741f,.316f},{.68f,.29f}}, JadeDeep);
            Polygon({{.65f,.068f},{.79f,.075f},{.78f,.10f},{.665f,.094f}}, Edge);
            Polygon({{.79f,.075f},{.875f,.17f},{.839f,.172f},{.78f,.10f}}, BronzeLight);
            Polygon({{.875f,.17f},{.76f,.34f},{.741f,.316f},{.839f,.172f}}, JadeLight);
            Stroke({.644f,.224f},{.727f,.176f},.006f,FLinearColor(.57f,.81f,.60f,1.f));
            Stroke({.659f,.254f},{.752f,.205f},.004f,JadeDeep);
            Polygon({{.614f,.281f},{.646f,.28f},{.65f,.305f},{.621f,.309f}}, BronzeLight);
            break;
        case 2:
            Polygon({{.23f,.91f},{.28f,.945f},{.77f,.24f},{.72f,.205f}}, WoodDeep);
            Polygon({{.25f,.905f},{.276f,.925f},{.752f,.239f},{.73f,.224f}}, Wood);
            Stroke({.266f,.86f},{.704f,.235f},.010f,WoodLight);
            Stroke({.281f,.83f},{.535f,.466f},.004f,WoodDeep);
            Polygon({{.223f,.908f},{.282f,.948f},{.307f,.91f},{.248f,.868f}}, BronzeShade);
            Stroke({.237f,.894f},{.293f,.932f},.014f,BronzeLight);
            // Layered iron socket and a restrained pale cutting edge.
            Polygon({{.661f,.271f},{.747f,.047f},{.895f,.094f},{.803f,.323f}}, FLinearColor(.19f,.28f,.29f,1.f));
            Polygon({{.687f,.268f},{.753f,.067f},{.825f,.162f},{.782f,.287f}}, Steel);
            Polygon({{.753f,.067f},{.874f,.106f},{.825f,.162f}}, Edge);
            Polygon({{.825f,.162f},{.874f,.106f},{.782f,.287f}}, FLinearColor(.33f,.44f,.46f,1.f));
            Stroke({.753f,.078f},{.73f,.196f},.006f,FLinearColor(.78f,.84f,.78f,1.f));
            Polygon({{.612f,.313f},{.659f,.345f},{.711f,.272f},{.665f,.239f}}, BronzeShade);
            Stroke({.626f,.308f},{.673f,.34f},.012f,BronzeLight);
            // Jade thread wrapping follows the shaft instead of a generic bar.
            for (int32 N=0;N<5;++N)
            {
                const float Offset=static_cast<float>(N)*.027f;
                Stroke({.419f+Offset*.68f,.653f-Offset},{.468f+Offset*.68f,.681f-Offset},.014f,JadeDeep);
                Stroke({.416f+Offset*.68f,.646f-Offset},{.46f+Offset*.68f,.675f-Offset},.007f,JadeLight);
            }
            break;
        case 3:
            Polygon({{.12f,.59f},{.28f,.205f},{.57f,.13f},{.89f,.405f},{.805f,.79f},{.38f,.895f}}, FLinearColor(.19f,.23f,.22f,1.f));
            Polygon({{.12f,.59f},{.28f,.205f},{.444f,.505f},{.38f,.895f}}, FLinearColor(.39f,.40f,.34f,1.f));
            Polygon({{.28f,.205f},{.57f,.13f},{.444f,.505f}}, FLinearColor(.66f,.64f,.51f,1.f));
            Polygon({{.57f,.13f},{.89f,.405f},{.444f,.505f}}, FLinearColor(.47f,.52f,.45f,1.f));
            Polygon({{.444f,.505f},{.89f,.405f},{.805f,.79f},{.38f,.895f}}, FLinearColor(.27f,.33f,.30f,1.f));
            Polygon({{.28f,.205f},{.32f,.223f},{.207f,.527f},{.12f,.59f}}, FLinearColor(.59f,.58f,.44f,1.f));
            Stroke({.43f,.53f},{.31f,.66f},.018f,JadeDeep);
            Stroke({.31f,.66f},{.38f,.77f},.018f,JadeDeep);
            Stroke({.43f,.532f},{.32f,.66f},.006f,JadeLight);
            Stroke({.32f,.66f},{.382f,.767f},.006f,JadeLight);
            Stroke({.493f,.501f},{.67f,.613f},.020f,FLinearColor(.10f,.24f,.19f,1.f));
            Stroke({.67f,.613f},{.759f,.591f},.016f,FLinearColor(.10f,.24f,.19f,1.f));
            Stroke({.503f,.509f},{.67f,.61f},.007f,FLinearColor(.34f,.57f,.39f,1.f));
            Polygon({{.57f,.61f},{.638f,.59f},{.663f,.659f},{.596f,.683f}}, BronzeShade);
            Polygon({{.57f,.61f},{.635f,.593f},{.618f,.636f},{.583f,.644f}}, BronzeLight);
            Polygon({{.71f,.70f},{.757f,.682f},{.772f,.72f},{.732f,.741f}}, FLinearColor(.53f,.49f,.32f,1.f));
            Stroke({.49f,.244f},{.455f,.426f},.006f,FLinearColor(.80f,.73f,.54f,1.f));
            break;
        case 4:
            Polygon({{.22f,.80f},{.18f,.47f},{.36f,.21f},{.82f,.12f},{.79f,.52f},{.50f,.83f}}, FLinearColor(.16f,.31f,.13f,1.f));
            Polygon({{.22f,.80f},{.36f,.21f},{.82f,.12f},{.48f,.53f}}, FLinearColor(.45f,.61f,.26f,1.f));
            Polygon({{.48f,.53f},{.82f,.12f},{.79f,.52f},{.50f,.83f}}, FLinearColor(.26f,.43f,.20f,1.f));
            Stroke({.25f,.87f},{.798f,.153f},.017f,FLinearColor(.76f,.79f,.54f,1.f));
            Stroke({.337f,.732f},{.237f,.50f},.009f,FLinearColor(.59f,.70f,.43f,1.f));
            Stroke({.43f,.611f},{.337f,.328f},.009f,FLinearColor(.65f,.75f,.45f,1.f));
            Stroke({.54f,.467f},{.47f,.247f},.008f,FLinearColor(.67f,.77f,.47f,1.f));
            Stroke({.62f,.36f},{.615f,.203f},.007f,FLinearColor(.73f,.79f,.53f,1.f));
            Stroke({.342f,.731f},{.499f,.735f},.008f,FLinearColor(.095f,.23f,.12f,1.f));
            Stroke({.445f,.597f},{.684f,.564f},.009f,FLinearColor(.11f,.27f,.14f,1.f));
            Stroke({.548f,.462f},{.751f,.379f},.008f,FLinearColor(.13f,.29f,.16f,1.f));
            Stroke({.259f,.39f},{.344f,.248f},.008f,FLinearColor(.75f,.80f,.49f,1.f));
            Polygon({{.363f,.243f},{.458f,.223f},{.379f,.31f}}, FLinearColor(.66f,.74f,.40f,1.f));
            break;
        case 5:
            Polygon({{.145f,.27f},{.69f,.135f},{.886f,.30f},{.85f,.767f},{.31f,.90f},{.145f,.72f}}, FLinearColor(.30f,.18f,.081f,1.f));
            Polygon({{.155f,.28f},{.687f,.15f},{.87f,.30f},{.31f,.445f}}, FLinearColor(.68f,.50f,.27f,1.f));
            Polygon({{.31f,.445f},{.87f,.30f},{.837f,.75f},{.31f,.885f}}, FLinearColor(.51f,.35f,.18f,1.f));
            Polygon({{.155f,.28f},{.31f,.445f},{.31f,.885f},{.156f,.711f}}, FLinearColor(.39f,.25f,.12f,1.f));
            // Fold lines, cloth edge stitching and two straps make a tied parcel.
            Stroke({.19f,.323f},{.326f,.433f},.009f,FLinearColor(.83f,.65f,.37f,1.f));
            Stroke({.343f,.486f},{.812f,.362f},.009f,FLinearColor(.78f,.57f,.29f,1.f));
            Stroke({.342f,.831f},{.812f,.709f},.008f,FLinearColor(.28f,.18f,.086f,1.f));
            Stroke({.692f,.189f},{.82f,.293f},.008f,FLinearColor(.34f,.23f,.12f,1.f));
            Polygon({{.44f,.20f},{.511f,.184f},{.67f,.354f},{.596f,.375f}}, JadeDeep);
            Polygon({{.596f,.375f},{.67f,.354f},{.64f,.805f},{.565f,.826f}}, JadeDeep);
            Stroke({.454f,.202f},{.604f,.361f},.012f,FLinearColor(.28f,.52f,.29f,1.f));
            Stroke({.61f,.386f},{.579f,.806f},.010f,FLinearColor(.27f,.49f,.29f,1.f));
            Polygon({{.303f,.59f},{.861f,.446f},{.855f,.52f},{.31f,.669f}}, FLinearColor(.11f,.27f,.18f,1.f));
            Stroke({.328f,.606f},{.837f,.476f},.009f,FLinearColor(.27f,.50f,.31f,1.f));
            Polygon({{.548f,.424f},{.691f,.477f},{.632f,.641f},{.485f,.582f}}, BronzeShade);
            Polygon({{.548f,.424f},{.691f,.477f},{.67f,.504f},{.543f,.459f}}, BronzeLight);
            Polygon({{.558f,.465f},{.652f,.501f},{.612f,.593f},{.523f,.556f}}, JadeFace);
            Polygon({{.558f,.465f},{.652f,.501f},{.596f,.528f}}, JadeLight);
            Stroke({.561f,.52f},{.608f,.536f},.009f,Gold);
            Stroke({.575f,.506f},{.591f,.552f},.009f,Gold);
            for(int32 N=0;N<5;++N)
            {
                const float X=.37f+static_cast<float>(N)*.073f;
                Stroke({X,.789f-(X-.37f)*.26f},{X+.018f,.784f-(X-.37f)*.26f},.004f,FLinearColor(.74f,.56f,.31f,1.f));
            }
            break;
        case 6:
            Polygon({{.48f,.067f},{.785f,.26f},{.735f,.77f},{.39f,.944f},{.165f,.622f},{.28f,.24f}}, JadeDeep);
            Polygon({{.48f,.067f},{.785f,.26f},{.50f,.56f}}, FLinearColor(.51f,.84f,.65f,1.f));
            Polygon({{.28f,.24f},{.48f,.067f},{.50f,.56f},{.165f,.622f}}, FLinearColor(.13f,.56f,.38f,1.f));
            Polygon({{.50f,.56f},{.785f,.26f},{.735f,.77f},{.39f,.944f}}, FLinearColor(.08f,.37f,.30f,1.f));
            Polygon({{.165f,.622f},{.50f,.56f},{.39f,.944f}}, FLinearColor(.26f,.68f,.49f,1.f));
            Polygon({{.48f,.067f},{.513f,.09f},{.322f,.27f},{.28f,.24f}}, FLinearColor(.76f,.94f,.73f,1.f));
            Polygon({{.28f,.24f},{.322f,.27f},{.208f,.594f},{.165f,.622f}}, JadeLight);
            Polygon({{.735f,.77f},{.39f,.944f},{.41f,.897f},{.71f,.752f}}, FLinearColor(.18f,.51f,.37f,1.f));
            Stroke({.337f,.406f},{.427f,.343f},.012f,JadeDeep);
            Stroke({.427f,.343f},{.462f,.423f},.010f,JadeDeep);
            Stroke({.583f,.613f},{.626f,.677f},.011f,FLinearColor(.22f,.55f,.43f,1.f));
            Stroke({.626f,.677f},{.601f,.756f},.009f,FLinearColor(.22f,.55f,.43f,1.f));
            Polygon({{.502f,.193f},{.552f,.21f},{.526f,.242f}}, FLinearColor(.82f,.97f,.78f,1.f));
            break;
        case -1: // traveler emblem, never a persistent item
            Polygon({{.50f,.04f},{.91f,.50f},{.50f,.96f},{.09f,.50f}}, BronzeShade);
            Polygon({{.50f,.04f},{.91f,.50f},{.83f,.50f},{.50f,.12f}}, Gold);
            Polygon({{.50f,.04f},{.50f,.12f},{.17f,.50f},{.09f,.50f}}, BronzeLight);
            Polygon({{.50f,.12f},{.83f,.50f},{.50f,.88f},{.17f,.50f}}, JadeFace);
            Polygon({{.50f,.24f},{.65f,.50f},{.50f,.76f},{.35f,.50f}}, JadeLight);
            Polygon({{.50f,.24f},{.65f,.50f},{.50f,.48f}}, Edge);
            Stroke({.28f,.5f},{.40f,.5f},.012f,BronzeLight);
            Stroke({.60f,.5f},{.72f,.5f},.012f,BronzeLight);
            break;
        case -2: // jade coin, never a persistent item
            Polygon({{.32f,.16f},{.67f,.16f},{.86f,.34f},{.86f,.66f},{.67f,.84f},{.32f,.84f},{.14f,.66f},{.14f,.34f}}, BronzeShade);
            Polygon({{.32f,.16f},{.67f,.16f},{.64f,.23f},{.35f,.23f}}, Gold);
            Polygon({{.67f,.16f},{.86f,.34f},{.84f,.40f},{.64f,.23f}}, BronzeLight);
            Polygon({{.32f,.16f},{.35f,.23f},{.21f,.40f},{.14f,.34f}}, Gold);
            Polygon({{.33f,.24f},{.66f,.24f},{.78f,.36f},{.78f,.63f},{.65f,.76f},{.35f,.76f},{.22f,.64f},{.22f,.37f}}, FLinearColor(.44f,.31f,.10f,1.f));
            Polygon({{.50f,.30f},{.67f,.50f},{.50f,.70f},{.33f,.50f}}, FLinearColor(.86f,.72f,.36f,1.f));
            Polygon({{.5f,.38f},{.61f,.5f},{.5f,.62f},{.39f,.5f}}, BronzeShade);
            Stroke({.31f,.34f},{.36f,.31f},.009f,Edge);
            break;
        case 7: // Original sealed salve pot; no reused coin/portrait symbol.
            Polygon({{.24f,.33f},{.76f,.33f},{.83f,.76f},{.70f,.90f},{.30f,.90f},{.17f,.76f}}, JadeDeep);
            Polygon({{.24f,.33f},{.51f,.33f},{.48f,.84f},{.30f,.84f},{.23f,.74f}}, JadeLight);
            Polygon({{.51f,.33f},{.76f,.33f},{.77f,.74f},{.70f,.84f},{.48f,.84f}}, JadeFace);
            Polygon({{.21f,.20f},{.79f,.20f},{.79f,.34f},{.21f,.34f}}, BronzeShade);
            Polygon({{.21f,.20f},{.79f,.20f},{.73f,.25f},{.27f,.25f}}, BronzeLight);
            Polygon({{.30f,.44f},{.69f,.44f},{.69f,.70f},{.30f,.70f}}, Edge);
            Stroke({.42f,.64f},{.56f,.50f},.035f,JadeDeep);
            Stroke({.49f,.57f},{.57f,.61f},.021f,JadeDeep);
            break;
        default: break;
        }
        if (!Vertices.IsEmpty() && FSlateApplication::IsInitialized())
        {
            const FSlateResourceHandle Resource = FSlateApplication::Get().GetRenderer()->GetResourceHandle(
                *FCoreStyle::Get().GetBrush("WhiteBrush"));
            FSlateDrawElement::MakeCustomVerts(Elements, Layer, Resource, Vertices, Indices, nullptr, 0, 0,
                bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect);
        }
        return Layer;
    }
};

// Decoration keys are separate from persisted item IDs; Balm is item7.
TSharedRef<SWidget> Art(int32 Id, float Size = 64.f)
{
    return SNew(SJadeboundItemArt).ItemId(Id).Size(Size).Visibility(EVisibility::HitTestInvisible);
}

TSharedRef<SWidget> Rule(FLinearColor Color = BronzeDim)
{
    return SNew(SBox).HeightOverride(1.f)
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Color).Padding(0.f)];
}

TSharedRef<SWidget> Badge(const FText& Label, FLinearColor Color = Gold)
{
    return SNew(SBorder).BorderImage(&BadgeBrush).Padding(FMargin(7.f,3.f)) [Text(Label,10.f,Color,true,100)];
}

FText WithRestriction(const FText& Description, const FText& Restriction)
{
    return Restriction.IsEmpty() ? Description : FText::Format(
        LOCTEXT("RestrictedTooltip", "{0}\n\n{1}"), Description, Restriction);
}



// Complete passive tooltip content. Only an exact repeated leading heading is
// removed. Structured item details retain every effect, price and instruction;
// their existing paragraph boundaries control visual hierarchy, never actions.
TSharedRef<IToolTip> PackTooltip(const FText& Details, const FText& Heading = FText::GetEmpty(), int32 ItemId = 0, float Width = 240.f)
{
    FString First, Rest;
    const FString Raw = Details.ToString();
    const bool bSplit = Raw.Split(TEXT("\n"), &First, &Rest);
    const FText Title = !Heading.IsEmpty() ? Heading : bSplit ? FText::FromString(First) : LOCTEXT("TooltipTitle","PACK DETAILS");
    const bool bRepeatedHeading = !Heading.IsEmpty() && bSplit && First == Heading.ToString();
    const FText Body = bRepeatedHeading || (Heading.IsEmpty() && bSplit) ? FText::FromString(Rest) : Details;
    const float BodyWidth = FMath::IsFinite(Width) ? FMath::Clamp(Width,120.f,480.f) : 240.f;
    const FText Lore = ItemLore(ItemId);
    // ItemTooltip owns this exact effect/stack-and-trade/controls sequence.
    // Unknown or generic details keep their complete original presentation.
    const bool bItemDetails = ItemId > 0 && !Lore.IsEmpty() && bSplit &&
        Rest.StartsWith(Lore.ToString() + TEXT("\n\n"), ESearchCase::CaseSensitive);
    TSharedRef<SWidget> BodyWidget = SNew(STextBlock).Text(Body)
        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15.f))
        .ColorAndOpacity(Ink).WrapTextAt(BodyWidth).AutoWrapText(false);
    if (bItemDetails)
    {
        TArray<FString> Paragraphs;
        Body.ToString().ParseIntoArray(Paragraphs,TEXT("\n\n"),false);
        TSharedRef<SVerticalBox> Sections = SNew(SVerticalBox);
        for (int32 Index = 0; Index < Paragraphs.Num(); ++Index)
        {
            const bool bStackAndTrade = Index == 1;
            const bool bControls = Index == 2;
            // Additional restriction paragraphs remain fully visible, in ivory.
            Sections->AddSlot().AutoHeight().Padding(0.f,Index > 0 ? 4.f : 0.f,0.f,0.f)
                [SNew(STextBlock).Text(FText::FromString(Paragraphs[Index]))
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),bStackAndTrade || bControls ? 13.f : 14.f))
                    .ColorAndOpacity(bControls ? Muted : bStackAndTrade ? Gold : Ink)
                    .WrapTextAt(BodyWidth).AutoWrapText(false)];
        }
        BodyWidget = Sections;
    }
    return SNew(SToolTip).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).TextMargin(0.f).IsInteractive(false)
        [SNew(SJadeboundArtBorder).Frame(false).Padding(8.f)
        [SNew(SBox).WidthOverride(BodyWidth)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,bItemDetails ? 4.f : 6.f)
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,ItemId > 0 ? 8.f : 0.f,0.f)
                    [SNew(SBox).Visibility(ItemId > 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed)[Art(ItemId,32.f)]]
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                    [SNew(STextBlock).Text(Title).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Medium"),17.f)).ColorAndOpacity(Gold).WrapTextAt(FMath::Max(1.f,BodyWidth-(ItemId > 0 ? 40.f : 0.f))).AutoWrapText(false)]]
                + SVerticalBox::Slot().AutoHeight()[Rule(Bronze)]
                + SVerticalBox::Slot().AutoHeight().Padding(0.f,bItemDetails ? 4.f : 6.f,0.f,0.f)
                [BodyWidget]]]];
}

// Opening-time placement only: weak widgets, public cached geometry, no Tick,
// input hooks, actor access or global delegates. Complete content is never
// height-cropped. Rehover after resize recomputes the placement.
class SJadeboundTooltipHost final : public SOverlay
{
public:
    SLATE_BEGIN_ARGS(SJadeboundTooltipHost) {}
        SLATE_DEFAULT_SLOT(FArguments, Content)
        SLATE_ARGUMENT(TArray<TWeakPtr<SWidget>>, ProtectedWidgets)
        SLATE_ARGUMENT(TWeakPtr<SWidget>, LeftPanel)
        SLATE_ARGUMENT(TWeakPtr<SWidget>, RightPanel)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        SetVisibility(EVisibility::SelfHitTestInvisible);
        MainContent=Args._Content.Widget;
        ProtectedWidgets=Args._ProtectedWidgets;LeftPanel=Args._LeftPanel;RightPanel=Args._RightPanel;
        SOverlay::Construct(SOverlay::FArguments()
            + SOverlay::Slot()[MainContent.ToSharedRef()]
            + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Expose(TooltipSlot)
                [SAssignNew(TooltipBox,SBox).Visibility(EVisibility::Collapsed)]);
        SetClipping(EWidgetClipping::ClipToBounds);
    }
    virtual bool OnVisualizeTooltip(const TSharedPtr<SWidget>& Content) override
    {
        TooltipBox->SetVisibility(EVisibility::Collapsed);
        TooltipBox->SetContent(SNullWidget::NullWidget);
        if (!Content.IsValid()) return true;
        if (!FSlateApplication::IsInitialized()) return false;
        const FGeometry& Geometry=GetCachedGeometry();
        const FVector2D Local=Geometry.GetLocalSize();
        if (!FinitePoint(Local) || Local.X<=16.f || Local.Y<=16.f) return false;
        Content->SlatePrepass(GetPrepassLayoutScaleMultiplier());
        const FVector2D Size=Content->GetDesiredSize();
        FSlateApplication& App=FSlateApplication::Get();
        const FVector2D Cursor=Geometry.AbsoluteToLocal(App.GetCursorPos());
        // Oversized/invalid full content stays in Slate's normal full-size
        // tooltip path. This host never substitutes a clipped short version.
        if (!FinitePoint(Size) || Size.X<=0.f || Size.Y<=0.f || !FinitePoint(Cursor) ||
            Size.X>Local.X-16.f || Size.Y>Local.Y-16.f) return false;

        // Match Slate's own disabled-widget tooltip lookup once at opening.
        // The resulting weak path is not retained or polled between openings.
        const TSharedPtr<FSlateUser> User=App.GetCursorUser();
        if (!User.IsValid()) return false;
        const FWeakWidgetPath Path(App.LocateWindowUnderMouse(App.GetCursorPos(),App.GetInteractiveTopLevelWindows(),true,User->GetUserIndex()));
        bool bOwnPath=false;
        for (const TWeakPtr<SWidget>& Weak : Path.Widgets)
            if (Weak.Pin().Get()==this) bOwnPath=true;
        if (!bOwnPath) return false;
        TWeakPtr<SWidget> HoveredSource;
        for (int32 Index=Path.Widgets.Num()-1;Index>=0;--Index)
        {
            const TSharedPtr<SWidget> Widget=Path.Widgets[Index].Pin();
            if (!Widget.IsValid()) continue;
            const TSharedPtr<IToolTip> Tip=Widget->GetToolTip();
            if (Tip.IsValid() && Tip->AsWidget()==Content.ToSharedRef())
            { HoveredSource=Widget;break; }
        }
        FSlateRect Source;
        if (!WidgetRect(HoveredSource,Geometry,Local,Source)) return false;
        TArray<FSlateRect> Avoid;Avoid.Add(Source);
        for (const TWeakPtr<SWidget>& Weak : ProtectedWidgets)
        {
            if (!Weak.IsValid()) continue;
            FSlateRect Rect;
            if (!WidgetRect(Weak,Geometry,Local,Rect)) return false;
            Avoid.Add(Rect);
        }
        FSlateRect Left,Right;
        const bool bCenter=WidgetRect(LeftPanel,Geometry,Local,Left) && WidgetRect(RightPanel,Geometry,Local,Right);
        FVector2D At;
        if (!Place(Local,Size,Cursor,Avoid,bCenter ? &Left : nullptr,bCenter ? &Right : nullptr,At)) return false;
        TooltipBox->SetWidthOverride(FOptionalSize(static_cast<float>(Size.X)));
        TooltipBox->SetContent(Content.ToSharedRef());
        TooltipSlot->SetPadding(FMargin(static_cast<float>(At.X),static_cast<float>(At.Y),0.f,0.f));
        TooltipBox->SetVisibility(EVisibility::HitTestInvisible);
        return true;
    }
protected:
    virtual FVector2D ComputeDesiredSize(float) const override
    { return MainContent.IsValid() ? FVector2D(MainContent->GetDesiredSize()) : FVector2D::ZeroVector; }
private:
    static bool FinitePoint(const FVector2D& P)
    { return FMath::IsFinite(P.X) && FMath::IsFinite(P.Y) && FMath::Abs(P.X)<=65536.0 && FMath::Abs(P.Y)<=65536.0; }
    static bool WidgetRect(const TWeakPtr<SWidget>& Weak,const FGeometry& Host,const FVector2D& Local,FSlateRect& Out)
    {
        const TSharedPtr<SWidget> Widget=Weak.Pin();
        if (!Widget.IsValid()) return false;
        const FGeometry& Cached=Widget->GetCachedGeometry();
        const FVector2D Extent=Cached.GetLocalSize();
        if (!FinitePoint(Extent) || Extent.X<=0.0 || Extent.Y<=0.0) return false;
        const FSlateRect Absolute=Cached.GetRenderBoundingRect();
        if (!FMath::IsFinite(Absolute.Left) || !FMath::IsFinite(Absolute.Top) ||
            !FMath::IsFinite(Absolute.Right) || !FMath::IsFinite(Absolute.Bottom)) return false;
        double MinX=65536.0,MinY=65536.0,MaxX=-65536.0,MaxY=-65536.0;
        for (const FVector2D& Corner : {FVector2D(Absolute.Left,Absolute.Top),FVector2D(Absolute.Right,Absolute.Top),
            FVector2D(Absolute.Left,Absolute.Bottom),FVector2D(Absolute.Right,Absolute.Bottom)})
        {
            const FVector2D P=Host.AbsoluteToLocal(Corner);
            if (!FinitePoint(P)) return false;
            MinX=FMath::Min(MinX,P.X);MinY=FMath::Min(MinY,P.Y);
            MaxX=FMath::Max(MaxX,P.X);MaxY=FMath::Max(MaxY,P.Y);
        }
        Out=FSlateRect(static_cast<float>(FMath::Clamp(MinX,0.0,Local.X)),static_cast<float>(FMath::Clamp(MinY,0.0,Local.Y)),
            static_cast<float>(FMath::Clamp(MaxX,0.0,Local.X)),static_cast<float>(FMath::Clamp(MaxY,0.0,Local.Y)));
        return Out.Right>Out.Left && Out.Bottom>Out.Top;
    }
    static bool Place(const FVector2D& Local,const FVector2D& Size,const FVector2D& Cursor,
        const TArray<FSlateRect>& Avoid,const FSlateRect* Left,const FSlateRect* Right,FVector2D& Out)
    {
        const double Margin=8.0,Gap=12.0,MaxX=Local.X-Margin-Size.X,MaxY=Local.Y-Margin-Size.Y;
        if (MaxX<Margin || MaxY<Margin) return false;
        TArray<double> X,Y;
        // Prefer the actual clear center between menu panels, near the source's
        // vertical level, then test cursor/viewport and every exclusion edge.
        if (Left && Right && Right->Left-Left->Right>=Size.X+2.0*Gap)
            X.Add((Left->Right+Right->Left-Size.X)*.5);
        X.Add(Cursor.X+Gap);X.Add(Cursor.X-Size.X-Gap);X.Add(Margin);X.Add(MaxX);
        Y.Add(Cursor.Y-Size.Y*.5);Y.Add(Margin);Y.Add(Cursor.Y+18.0);Y.Add(Cursor.Y-Size.Y-14.0);Y.Add(MaxY);
        for (const FSlateRect& R : Avoid)
        { X.Add(R.Right+Gap);X.Add(R.Left-Size.X-Gap);Y.Add(R.Bottom+Gap);Y.Add(R.Top-Size.Y-Gap); }
        bool bFound=false;double BestOverlap=TNumericLimits<double>::Max();
        for (const double RawX : X) for (const double RawY : Y)
        {
            const FVector2D At(FMath::Clamp(RawX,Margin,MaxX),FMath::Clamp(RawY,Margin,MaxY));
            if (!FinitePoint(At)) continue;
            double Overlap=0.0;
            for (const FSlateRect& R : Avoid)
            {
                const double W=FMath::Max(0.0,FMath::Min(At.X+Size.X,static_cast<double>(R.Right)+Gap)-FMath::Max(At.X,static_cast<double>(R.Left)-Gap));
                const double H=FMath::Max(0.0,FMath::Min(At.Y+Size.Y,static_cast<double>(R.Bottom)+Gap)-FMath::Max(At.Y,static_cast<double>(R.Top)-Gap));
                Overlap+=W*H;
            }
            if (!FMath::IsFinite(Overlap)) continue;
            if (!bFound || Overlap<BestOverlap)
            { bFound=true;BestOverlap=Overlap;Out=At; }
            if (Overlap==0.0) return true;
        }
        // Crowded but viewport-fitting: retain the complete tooltip at the
        // least-overlap tested position. No universal unobstructed-fit claim.
        return bFound;
    }
    TSharedPtr<SWidget> MainContent;
    TSharedPtr<SBox> TooltipBox;
    SOverlay::FOverlaySlot* TooltipSlot=nullptr;
    TArray<TWeakPtr<SWidget>> ProtectedWidgets;
    TWeakPtr<SWidget> LeftPanel,RightPanel;
};

FText PlayerFeedback(const FText& Raw, bool bDevelopment)
{
    const FString Value = Raw.ToString();
    if (Value == TEXT("LMB: walk / target attack   Ctrl+LMB: jump   F1-F10: assigned actions   RMB: loot / selected skill   I: bag   Progress saves automatically"))
        return LOCTEXT("AutomaticControlsHelp", "LMB: move / attack. Ctrl+LMB: jump. F1-F10: assigned actions. RMB: loot / selected skill. I: pack. Progress saves automatically.");
    if (bDevelopment && Value == TEXT("Weapon returned to your bag. Equip it to attack."))
        return LOCTEXT("DevelopmentWeaponReturned", "Weapon returned to your pack.");
    if (Value.Contains(TEXT("Local authority foundation"))) return LOCTEXT("CombatUnavailable", "Combat is not available in this expedition yet.");
    if (Value.Contains(TEXT("Exact revision"))) return LOCTEXT("PackLocked", "Your pack is locked to protect your items. Close and reopen it.");
    if (Value.Contains(TEXT("Save validation"))) return LOCTEXT("SaveLocked", "Saving needs attention. Your existing save is protected.");
    if (Value.Contains(TEXT("journal"))) return LOCTEXT("JournalAttention", "Your save needs attention. Existing files are protected; avoid starting over.");
    if (Value.Contains(TEXT("Save in the bag/menu")) || Value.Contains(TEXT("finish Save before interacting")))
        return LOCTEXT("SaveAttention", "Progress could not be saved. Keep the game open while saving recovers.");
    if (Value.Contains(TEXT("current inventory revision"))) return LOCTEXT("RefreshSelection", "Your pack changed. Select the item again.");
    return Raw;
}

// Independent local health presentation. Inventory revisions and the economy
// HUD's 10 Hz Changed gate never control these samples or world projections.
// Shared only by these Slate views. Sizes come from actual child prepass,
// never actor state, global DPI settings, screen guesses or input callbacks.
struct FJadeboundHudDockLayout
{
    float GearWidth = 500.f;
    float GearLeft = 0.f;
    float GearBottom = 72.f;
    float GearHeaderHeight = 28.f;
    float QuickbarLeft = 0.f;
    float QuickbarBottom = 12.f;
    float HealthCenterY = -1.f;
};

class SJadeboundHealthHud final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundHealthHud) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AJadeboundProgressionActor>, ProgressionActor)
        SLATE_ARGUMENT(TSharedPtr<FJadeboundHudDockLayout>, DockLayout)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Actor = Args._ProgressionActor;
        Dock = Args._DockLayout;
        SetCanTick(true);
        ForceVolatile(true);
        SetVisibility(EVisibility::HitTestInvisible);
    }
    virtual ~SJadeboundHealthHud() override { RestoreLegacyText(); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual void Tick(const FGeometry& Geometry, double Now, float DeltaSeconds) override
    {
        SLeafWidget::Tick(Geometry,Now,DeltaSeconds);
        const auto* Model = Actor.Get();
        const FJadeboundHealthPresentation View = Model ? Model->GetHealthPresentation() : FJadeboundHealthPresentation();
        bContext = View.bContextAvailable;
        bMenu = Model && Model->IsMenuOpen();
        bDrawTarget = false;
        if (!bContext)
        {
            RestoreLegacyText(); ConflictText.Reset();
            PlayerSample = {}; TargetSample = {}; LastPlayer.Reset(); LastPawn.Reset(); LastTarget.Reset();
            ZeroObservedAt = -1.0;
            return;
        }
        if (View.Player != LastPlayer)
        {
            RestoreLegacyText(); ConflictText.Reset();
            PlayerSample = {}; TargetSample = {}; LastPawn.Reset(); LastTarget.Reset();
            ZeroObservedAt = -1.0; LastPlayer = View.Player;
        }
        // A replacement pawn cannot inherit a previous life. During an unbound
        // respawn interval, retain only zero that was actually read beforehand.
        if (View.Pawn.IsValid() && View.Pawn != LastPawn)
        {
            PlayerSample = {};
        }
        if (View.PlayerHealth.bAvailable)
        {
            PlayerSample = View.PlayerHealth; LastPawn = View.Pawn;
        }
        else if (View.Pawn.IsValid() || !PlayerSample.bAvailable || PlayerSample.Current > 0.0)
            PlayerSample = {};

        if (View.Target != LastTarget)
        {
            RestoreLegacyText(); ConflictText.Reset();
            LastTarget = View.Target; TargetSample = {}; ZeroObservedAt = -1.0;
        }
        if (View.TargetHealth.bAvailable && View.bTargetAnchorAvailable)
        {
            if (View.TargetHealth.Current <= 0.0)
            {
                if (ZeroObservedAt < 0.0) ZeroObservedAt = Now;
                TargetSample = View.TargetHealth;
                // This is a short, measured defeat indication. It neither
                // unhides nor keeps the defeated gameplay actor alive.
                bDrawTarget = Now - ZeroObservedAt <= .90;
            }
            else
            {
                ZeroObservedAt = -1.0;
                TargetSample = View.TargetHealth;
                bDrawTarget = !View.bTargetHidden && View.bTargetCombatEnabled;
            }
        }
        else { TargetSample = {}; ZeroObservedAt = -1.0; }

        int32 PixelWidth = 0, PixelHeight = 0;
        FVector2D Screen = FVector2D::ZeroVector;
        APlayerController* Player = View.Player.Get();
        const FVector2D Local = Geometry.GetLocalSize();
        if (Player) Player->GetViewportSize(PixelWidth,PixelHeight);
        if (bDrawTarget && Player && PixelWidth > 0 && PixelHeight > 0 &&
            FMath::IsFinite(Local.X) && FMath::IsFinite(Local.Y) && Local.X >= 320.0 && Local.Y >= 260.0 &&
            Player->ProjectWorldLocationToScreen(View.TargetAnchor,Screen,false) &&
            FMath::IsFinite(Screen.X) && FMath::IsFinite(Screen.Y) &&
            Screen.X >= 0.0 && Screen.X <= PixelWidth && Screen.Y >= 0.0 && Screen.Y <= PixelHeight)
        {
            // Native projection uses full viewport pixels. A full-viewport
            // Slate sibling maps once to logical local size, without a second
            // DPI division or constrained-player offset.
            TargetPoint = FVector2f(static_cast<float>(Local.X * Screen.X / PixelWidth),
                static_cast<float>(Local.Y * Screen.Y / PixelHeight));
            // Keep name, bar and percentage clear of the capsule head.
            TargetPoint.Y -= 24.f;
            // Clamp the whole block only after the real anchor projects inside
            // the viewport. Its current envelope is X +/-56, Y -31..+27.
            const float SafeLeft = 64.f, SafeTop = 39.f;
            const float SafeRight = static_cast<float>(Local.X - 64.0);
            const float SafeBottom = static_cast<float>(Local.Y - 35.0);
            bDrawTarget = FMath::IsFinite(TargetPoint.X) && FMath::IsFinite(TargetPoint.Y) &&
                FMath::IsFinite(SafeRight) && FMath::IsFinite(SafeBottom) &&
                SafeRight >= SafeLeft && SafeBottom >= SafeTop;
            if (bDrawTarget)
            {
                TargetPoint.X = FMath::Clamp(TargetPoint.X,SafeLeft,SafeRight);
                TargetPoint.Y = FMath::Clamp(TargetPoint.Y,SafeTop,SafeBottom);
            }
        }
        else bDrawTarget = false;
        UpdateLegacyText(bDrawTarget ? View.LegacyTargetText.Get() : nullptr,View.Target.Get());
    }
private:
    TWeakObjectPtr<AJadeboundProgressionActor> Actor;
    TSharedPtr<FJadeboundHudDockLayout> Dock;
    TWeakObjectPtr<APlayerController> LastPlayer;
    TWeakObjectPtr<APawn> LastPawn;
    TWeakObjectPtr<AActor> LastTarget;
    FJadeboundHealthSample PlayerSample, TargetSample;
    bool bContext = false, bMenu = false, bDrawTarget = false;
    double ZeroObservedAt = -1.0;
    FVector2f TargetPoint = FVector2f::ZeroVector;
    TWeakObjectPtr<UTextRenderComponent> HiddenText, ConflictText;
    TWeakObjectPtr<AActor> HiddenOwner;
    bool bOriginalVisible = false, bOriginalHidden = false;
    bool bOwnVisibleChange = false, bOwnHiddenChange = false;
    FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle(TEXT("Medium"),12.f);
    FSlateFontInfo TargetFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),13.f);

    void RestoreLegacyText()
    {
        // Raw flags preserve the exact component state; IsVisible combines
        // actor/level state and would capture a different contract.
        if (UTextRenderComponent* Text = HiddenText.Get())
            if (IsValid(HiddenOwner.Get()) && Text->GetOwner() == HiddenOwner.Get())
            {
                if (bOwnVisibleChange && !Text->GetVisibleFlag()) Text->SetVisibility(bOriginalVisible,false);
                if (bOwnHiddenChange && Text->bHiddenInGame) Text->SetHiddenInGame(bOriginalHidden,false);
            }
        HiddenText.Reset(); HiddenOwner.Reset();
        bOwnVisibleChange = false; bOwnHiddenChange = false;
    }
    void UpdateLegacyText(UTextRenderComponent* Desired, AActor* Owner)
    {
        if (!IsValid(Desired) || !IsValid(Owner) || Desired->GetOwner() != Owner) Desired = nullptr;
        if (HiddenText.Get() != Desired || HiddenOwner.Get() != Owner) RestoreLegacyText();
        if (!Desired || ConflictText.Get() == Desired) return;
        if (HiddenText.Get() == Desired)
        {
            // Yield to an unexpected visibility writer; do not repeatedly
            // fight it. Known V006 text Tick writes rotation only. Same-value
            // external writes cannot be detected, so this is scoped to that
            // exact standalone contract, not a general visibility framework.
            if ((bOwnVisibleChange && Desired->GetVisibleFlag()) || (bOwnHiddenChange && !Desired->bHiddenInGame))
            { RestoreLegacyText(); ConflictText = Desired; }
            return;
        }
        HiddenText = Desired; HiddenOwner = Owner;
        bOriginalVisible = Desired->GetVisibleFlag(); bOriginalHidden = Desired->bHiddenInGame;
        bOwnVisibleChange = bOriginalVisible; bOwnHiddenChange = !bOriginalHidden;
        if (bOwnVisibleChange) Desired->SetVisibility(false,false);
        if (bOwnHiddenChange) Desired->SetHiddenInGame(true,false);
    }

    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override
    {
        if (!bContext || bMenu || !FSlateApplication::IsInitialized()) return Layer;
        const FVector2D Local = Geometry.GetLocalSize();
        if (Local.X < 320.0 || Local.Y < 260.0) return Layer;
        const FLinearColor Tint = Style.GetColorAndOpacityTint();
        TArray<FSlateVertex> Vertices;
        TArray<SlateIndex> Indices;
        Vertices.Reserve(1400); Indices.Reserve(4200);
        bool bPlayerPaint = true;
        float PlayerCenterY = 0.f;
        auto Vertex = [&](FVector2f Point,FLinearColor Color)
        {
            // Compress only the authored player chrome. Health fill math and
            // all enemy-anchor coordinates remain in their original space.
            if (bPlayerPaint) Point=FVector2f(18.f+(Point.X-22.f)*.76f,
                PlayerCenterY+(Point.Y-PlayerCenterY)*.76f);
            return FSlateVertex::Make<ESlateVertexRounding::Disabled>(Geometry.GetAccumulatedRenderTransform(),
                Point,FVector2f(.5f,.5f),(Color * Tint).ToFColor(true));
        };
        auto Polygon = [&](const TArray<FVector2f>& Shape,FLinearColor Color)
        {
            if (Shape.Num() < 3) return;
            const int32 Start = Vertices.Num();
            for (const FVector2f& Point : Shape) Vertices.Add(Vertex(Point,Color));
            for (int32 N=1;N+1<Shape.Num();++N)
            { Indices.Add(static_cast<SlateIndex>(Start)); Indices.Add(static_cast<SlateIndex>(Start+N)); Indices.Add(static_cast<SlateIndex>(Start+N+1)); }
        };
        auto Shape = [&](std::initializer_list<FVector2f> Points,FLinearColor Color)
        {
            TArray<FVector2f> Array; Array.Reserve(static_cast<int32>(Points.size()));
            for (const FVector2f& Point : Points) Array.Add(Point);
            Polygon(Array,Color);
        };
        auto Box = [&](FVector2f A,FVector2f B,FLinearColor Color)
        { Shape({A,{B.X,A.Y},B,{A.X,B.Y}},Color); };
        auto Stroke = [&](FVector2f A,FVector2f B,float Weight,FLinearColor Color)
        {
            const FVector2f D=B-A;
            const float Span=FMath::Sqrt(D.X*D.X+D.Y*D.Y);
            if (Span<=SMALL_NUMBER) return;
            const FVector2f N(-D.Y*Weight/(2.f*Span),D.X*Weight/(2.f*Span));
            Shape({A+N,B+N,B-N,A-N},Color);
        };
        auto Circle = [&](FVector2f Center,float Radius)
        {
            TArray<FVector2f> Points; Points.Reserve(64);
            for (int32 N=0;N<64;++N)
            {
                const float Angle=2.f*PI*N/64.f;
                Points.Add(Center+FVector2f(FMath::Cos(Angle),FMath::Sin(Angle))*Radius);
            }
            return Points;
        };
        auto Ring = [&](FVector2f Center,float Inner,float Outer,FLinearColor Light,FLinearColor Dark)
        {
            const int32 Start=Vertices.Num();
            for (int32 N=0;N<=64;++N)
            {
                const float Angle=2.f*PI*N/64.f;
                const FVector2f Axis(FMath::Cos(Angle),FMath::Sin(Angle));
                const float Key=FMath::Clamp(.5f-(Axis.X+Axis.Y)*.25f,0.f,1.f);
                const FLinearColor Color=FMath::Lerp(Dark,Light,Key);
                Vertices.Add(Vertex(Center+Axis*Outer,Color));
                Vertices.Add(Vertex(Center+Axis*Inner,Color));
                if (N>0)
                {
                    const int32 P=Start+(N-1)*2;
                    for (int32 Index : {P,P+2,P+1,P+1,P+2,P+3}) Indices.Add(static_cast<SlateIndex>(Index));
                }
            }
        };
        // Wide viewports keep the reservoir near the lower-left edge.
        // Narrow views clear the actual measured gear/quickbar stack above it.
        const float Bottom = Dock.IsValid() && Dock->HealthCenterY >= 0.f
            ? Dock->HealthCenterY : static_cast<float>(Local.Y) - 66.f;
        PlayerCenterY=Bottom;
        const FVector2f Center(72.f,Bottom);
        const FLinearColor BronzeLight(.66f,.45f,.21f,1.f), BronzeShade(.11f,.064f,.026f,1.f);
        Polygon(Circle(Center,50.f),FLinearColor(.002f,.003f,.004f,.95f));
        Ring(Center,46.f,48.f,BronzeLight,BronzeShade);
        Ring(Center,42.f,46.f,FLinearColor(.35f,.22f,.09f,1.f),BronzeDim);
        Ring(Center,39.5f,42.f,Gold*.75f,BronzeShade);
        Polygon(Circle(Center,39.5f),WellDark);
        // Fine, original rim cuts and four jade settings. Their angular shape
        // repeats the merchant frame's authored martial corner vocabulary.
        for (int32 N=0;N<16;++N)
        {
            const float Angle=2.f*PI*N/16.f;
            const FVector2f Axis(FMath::Cos(Angle),FMath::Sin(Angle));
            Stroke(Center+Axis*43.f,Center+Axis*45.f,.65f,BronzeShade);
        }
        for (int32 N=0;N<4;++N)
        {
            const float Angle=PI*.25f+PI*.5f*N;
            const FVector2f Stud=Center+FVector2f(FMath::Cos(Angle),FMath::Sin(Angle))*44.f;
            Shape({Stud+FVector2f(0.f,-3.7f),Stud+FVector2f(3.7f,0.f),Stud+FVector2f(0.f,3.7f),Stud+FVector2f(-3.7f,0.f)},BronzeLight);
            Shape({Stud+FVector2f(0.f,-2.4f),Stud+FVector2f(2.4f,0.f),Stud+FVector2f(0.f,2.4f),Stud+FVector2f(-2.4f,0.f)},SelectedJade);
            Stroke(Stud+FVector2f(-1.4f,-.1f),Stud+FVector2f(0.f,-1.4f),.65f,FLinearColor(.19f,.49f,.32f,1.f));
        }
        // Clip a circular reservoir to its actual bottom-up fill. Zero draws
        // no crimson, meniscus, or glass-fill highlight at all.
        if (PlayerSample.bAvailable && PlayerSample.Ratio > 0.f)
        {
            const float Radius=38.f, Cut=Center.Y+Radius*(1.f-2.f*PlayerSample.Ratio);
            const TArray<FVector2f> Boundary=Circle(Center,Radius);
            TArray<FVector2f> Fill;
            for (int32 N=0;N<Boundary.Num();++N)
            {
                const FVector2f A=Boundary[N], B=Boundary[(N+1)%Boundary.Num()];
                const bool InA=A.Y>=Cut, InB=B.Y>=Cut;
                if (InA) Fill.Add(A);
                if (InA!=InB) Fill.Add(A+(B-A)*((Cut-A.Y)/(B.Y-A.Y)));
            }
            if (Fill.Num()>=3)
            {
                const int32 Start=Vertices.Num();
                for (const FVector2f& P : Fill)
                {
                    const float Depth=FMath::Clamp((P.Y-Center.Y+Radius)/(Radius*2.f),0.f,1.f);
                    const float Side=FMath::Clamp((P.X-Center.X+Radius)/(Radius*2.f),0.f,1.f);
                    const FLinearColor Color=FMath::Lerp(FLinearColor(.69f,.036f,.049f,1.f),FLinearColor(.17f,.004f,.016f,1.f),Depth)
                        * FMath::Lerp(1.08f,.77f,Side);
                    Vertices.Add(Vertex(P,Color));
                }
                for (int32 N=1;N+1<Fill.Num();++N)
                { Indices.Add(static_cast<SlateIndex>(Start)); Indices.Add(static_cast<SlateIndex>(Start+N)); Indices.Add(static_cast<SlateIndex>(Start+N+1)); }
                if (PlayerSample.Ratio < 1.f)
                {
                    const float Half=FMath::Sqrt(FMath::Max(0.f,Radius*Radius-(Cut-Center.Y)*(Cut-Center.Y)));
                    Stroke({Center.X-Half,Cut+.35f},{Center.X+Half,Cut+.35f},.7f,FLinearColor(.86f,.092f,.10f,1.f));
                }
            }
        }
        Ring(Center,38.f,39.5f,FLinearColor(.46f,.28f,.11f,1.f),BronzeShade);
        // Normalized HP sits below its well, joined to the stamina/shortcut dock.
        const bool bCritical=PlayerSample.bAvailable && PlayerSample.Current>0.0 && PlayerSample.Ratio<=.25f;
        if (bCritical) Ring(Center,48.f,49.f,FLinearColor(.64f,.052f,.028f,.85f),FLinearColor(.20f,.015f,.007f,.85f));
        if (Dock.IsValid() && Dock->QuickbarBottom <= 12.f)
        {
            Stroke({122.f,Bottom-16.f},{153.f,Bottom-16.f},1.8f,BronzeLight);
            Stroke({122.f,Bottom+16.f},{153.f,Bottom+16.f},1.8f,BronzeShade);
        }
        bPlayerPaint=false;

        if (bDrawTarget)
        {
            const FVector2f A=TargetPoint+FVector2f(-49.f,-10.f), B=TargetPoint+FVector2f(49.f,0.f);
            Box(A-FVector2f(1.f,1.f),B+FVector2f(1.f,1.f),FLinearColor(.002f,.003f,.004f,.94f));
            Box(A,B,BronzeShade);
            Box(A+FVector2f(1.f,1.f),B-FVector2f(1.f,1.f),WellDark);
            if (TargetSample.Ratio>0.f)
            {
                const float End=A.X+1.f+96.f*TargetSample.Ratio;
                Box(A+FVector2f(1.f,1.f),{End,B.Y-1.f},FLinearColor(.43f,.017f,.027f,1.f));
                Box(A+FVector2f(1.f,1.f),{End,A.Y+3.f},FLinearColor(.77f,.053f,.063f,1.f));
            }
            Stroke({A.X,A.Y+1.f},{A.X,B.Y-1.f},1.f,BronzeLight);
            Stroke({B.X,A.Y+1.f},{B.X,B.Y-1.f},1.f,BronzeLight);
        }
        const FSlateResourceHandle Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
        FSlateDrawElement::MakeCustomVerts(Elements,Layer,Resource,Vertices,Indices,nullptr,0,0,
            bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect);
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        auto Label = [&](const FText& Value,const FSlateFontInfo& Font,FVector2f At,float Width,FLinearColor Color)
        {
            const FVector2D Size=Measure->Measure(Value,Font);
            const FVector2f Position(At.X+FMath::Max(0.f,(Width-static_cast<float>(Size.X))*.5f),At.Y);
            const auto Paint=Geometry.MakeChild(FVector2f(Width,24.f),FSlateLayoutTransform(Position)).ToPaintGeometry();
            FSlateDrawElement::MakeText(Elements,Layer+1,Paint,Value,Font,ESlateDrawEffect::None,Color * Tint);
        };
        const FText HealthLabel=PlayerSample.bAvailable
            ? FText::Format(LOCTEXT("HealthPercent","HP {0}%"),FText::AsNumber(PlayerSample.Percent))
            : LOCTEXT("HealthUnavailable","HP unavailable");
        Label(HealthLabel,SmallFont,{8.f,Bottom+40.f},96.f,PlayerSample.bAvailable ? Ink : Muted);
        if (PlayerSample.bAvailable && PlayerSample.Current<=0.0)
            Label(LOCTEXT("MeasuredDefeat","DEFEATED"),SmallFont,{18.f,Bottom-7.f},76.f,Ink);
        if (bDrawTarget)
        {
            Label(LOCTEXT("LegacyRaiderName","RAIDER"),SmallFont,TargetPoint+FVector2f(-56.f,-31.f),112.f,Ink);
            Label(FText::Format(LOCTEXT("TargetHealthPercent","{0}%"),FText::AsNumber(TargetSample.Percent)),TargetFont,
                TargetPoint+FVector2f(-40.f,3.f),80.f,Ink);
        }
        return Layer+1;
    }
};


// Passive player-only menu health. Read the existing normalized DTO without
// target projection, legacy-text ownership, gameplay calls or input handlers.
class SJadeboundMenuPlayerHealth final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundMenuPlayerHealth) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AJadeboundProgressionActor>, ProgressionActor)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Actor=Args._ProgressionActor;
        SetCanTick(true); ForceVolatile(true);
        SetVisibility(EVisibility::HitTestInvisible);
        SetClipping(EWidgetClipping::ClipToBounds);
    }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(112.f,104.f); }
    virtual bool SupportsKeyboardFocus() const override { return false; }
    virtual void Tick(const FGeometry& Geometry,double Now,float DeltaSeconds) override
    {
        SLeafWidget::Tick(Geometry,Now,DeltaSeconds);
        const auto* Model=Actor.Get();
        const FJadeboundHealthPresentation View=Model ? Model->GetHealthPresentation() : FJadeboundHealthPresentation();
        if (!View.bContextAvailable)
        { Sample={}; LastPlayer.Reset(); LastPawn.Reset(); return; }
        if (View.Player!=LastPlayer)
        { Sample={}; LastPawn.Reset(); LastPlayer=View.Player; }
        // A new pawn cannot inherit the previous life. During an unpossessed
        // respawn interval retain only zero actually observed by this widget.
        if (View.Pawn.IsValid() && View.Pawn!=LastPawn) Sample={};
        const FJadeboundHealthSample& Actual=View.PlayerHealth;
        const bool bValid=Actual.bAvailable && FMath::IsFinite(Actual.Current) &&
            FMath::IsFinite(Actual.Maximum) && Actual.Maximum>0.0 &&
            FMath::IsFinite(Actual.Ratio) && Actual.Ratio>=0.f && Actual.Ratio<=1.f &&
            Actual.Percent>=0 && Actual.Percent<=100;
        if (bValid) { Sample=Actual; LastPawn=View.Pawn; }
        else if (View.Pawn.IsValid() || !Sample.bAvailable || Sample.Current>0.0) Sample={};
    }
private:
    TWeakObjectPtr<AJadeboundProgressionActor> Actor;
    TWeakObjectPtr<APlayerController> LastPlayer;
    TWeakObjectPtr<APawn> LastPawn;
    FJadeboundHealthSample Sample;
    FSlateFontInfo ValueFont=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14.f);
    FSlateFontInfo StateFont=FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14.f);

    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override
    {
        const FVector2D Local=Geometry.GetLocalSize();
        if (!FSlateApplication::IsInitialized() || !FMath::IsFinite(Local.X) || !FMath::IsFinite(Local.Y) ||
            Local.X<112.0 || Local.Y<104.0) return Layer;
        const FLinearColor Tint=Style.GetColorAndOpacityTint();
        const ESlateDrawEffect Effect=bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
        TArray<FSlateVertex> Vertices; TArray<SlateIndex> Indices;
        Vertices.Reserve(1200); Indices.Reserve(3600);
        auto Vertex=[&](FVector2f Point,FLinearColor Color)
        {
            return FSlateVertex::Make<ESlateVertexRounding::Disabled>(Geometry.GetAccumulatedRenderTransform(),
                Point,FVector2f(.5f,.5f),(Color*Tint).ToFColor(true));
        };
        auto Polygon=[&](const TArray<FVector2f>& Points,FLinearColor Color)
        {
            if (Points.Num()<3) return;
            const int32 Start=Vertices.Num();
            for (const FVector2f& Point : Points) Vertices.Add(Vertex(Point,Color));
            for (int32 N=1;N+1<Points.Num();++N)
            { Indices.Add(static_cast<SlateIndex>(Start)); Indices.Add(static_cast<SlateIndex>(Start+N)); Indices.Add(static_cast<SlateIndex>(Start+N+1)); }
        };
        auto Shape=[&](std::initializer_list<FVector2f> Points,FLinearColor Color)
        {
            TArray<FVector2f> Array; Array.Reserve(static_cast<int32>(Points.size()));
            for (const FVector2f& Point : Points) Array.Add(Point);
            Polygon(Array,Color);
        };
        auto Box=[&](FVector2f A,FVector2f B,FLinearColor Color)
        { Shape({A,{B.X,A.Y},B,{A.X,B.Y}},Color); };
        auto Stroke=[&](FVector2f A,FVector2f B,float Weight,FLinearColor Color)
        {
            const FVector2f D=B-A;
            const float Span=FMath::Sqrt(D.X*D.X+D.Y*D.Y);
            if (Span<=SMALL_NUMBER) return;
            const FVector2f N(-D.Y*Weight/(2.f*Span),D.X*Weight/(2.f*Span));
            Shape({A+N,B+N,B-N,A-N},Color);
        };
        auto Circle=[&](FVector2f Center,float Radius)
        {
            TArray<FVector2f> Points; Points.Reserve(64);
            for (int32 N=0;N<64;++N)
            {
                const float Angle=2.f*PI*N/64.f;
                Points.Add(Center+FVector2f(FMath::Cos(Angle),FMath::Sin(Angle))*Radius);
            }
            return Points;
        };
        auto Ring=[&](FVector2f Center,float Inner,float Outer,FLinearColor Light,FLinearColor Dark)
        {
            const int32 Start=Vertices.Num();
            for (int32 N=0;N<=64;++N)
            {
                const float Angle=2.f*PI*N/64.f;
                const FVector2f Axis(FMath::Cos(Angle),FMath::Sin(Angle));
                const float Key=FMath::Clamp(.5f-(Axis.X+Axis.Y)*.25f,0.f,1.f);
                const FLinearColor Color=FMath::Lerp(Dark,Light,Key);
                Vertices.Add(Vertex(Center+Axis*Outer,Color));
                Vertices.Add(Vertex(Center+Axis*Inner,Color));
                if (N>0)
                {
                    const int32 P=Start+(N-1)*2;
                    for (int32 Index : {P,P+2,P+1,P+1,P+2,P+3}) Indices.Add(static_cast<SlateIndex>(Index));
                }
            }
        };
        // Original restrained bronze/jade menu chrome; normal gameplay health
        // and its target paint remain separate and unchanged.
        const FVector2f Center(56.f,40.f);
        const FLinearColor BronzeLight(.66f,.45f,.21f,1.f),BronzeShade(.11f,.064f,.026f,1.f);
        Polygon(Circle(Center,39.5f),FLinearColor(.002f,.003f,.004f,.95f));
        Ring(Center,36.5f,38.f,BronzeLight,BronzeShade);
        Ring(Center,32.f,36.5f,FLinearColor(.35f,.22f,.09f,1.f),BronzeDim);
        Ring(Center,30.5f,32.f,Gold*.65f,BronzeShade);
        Polygon(Circle(Center,30.5f),WellDark);
        for (int32 N=0;N<12;++N)
        {
            const float Angle=2.f*PI*N/12.f;
            const FVector2f Axis(FMath::Cos(Angle),FMath::Sin(Angle));
            Stroke(Center+Axis*34.f,Center+Axis*35.8f,.6f,BronzeShade);
        }
        for (float Side : {-1.f,1.f})
        {
            const FVector2f Stud=Center+FVector2f(Side*36.f,0.f);
            Shape({Stud+FVector2f(0.f,-2.8f),Stud+FVector2f(2.8f,0.f),Stud+FVector2f(0.f,2.8f),Stud+FVector2f(-2.8f,0.f)},BronzeLight);
            Shape({Stud+FVector2f(0.f,-1.7f),Stud+FVector2f(1.7f,0.f),Stud+FVector2f(0.f,1.7f),Stud+FVector2f(-1.7f,0.f)},SelectedJade);
        }
        // Convex circular clipping uses the validated, observed ratio. At zero
        // no crimson polygon or meniscus/highlight is emitted.
        if (Sample.bAvailable && Sample.Ratio>0.f)
        {
            const float Radius=29.5f,Cut=Center.Y+Radius*(1.f-2.f*Sample.Ratio);
            const TArray<FVector2f> Boundary=Circle(Center,Radius);
            TArray<FVector2f> Fill; Fill.Reserve(66);
            for (int32 N=0;N<Boundary.Num();++N)
            {
                const FVector2f A=Boundary[N],B=Boundary[(N+1)%Boundary.Num()];
                const bool InA=A.Y>=Cut,InB=B.Y>=Cut;
                if (InA) Fill.Add(A);
                if (InA!=InB) Fill.Add(A+(B-A)*((Cut-A.Y)/(B.Y-A.Y)));
            }
            if (Fill.Num()>=3)
            {
                const int32 Start=Vertices.Num();
                for (const FVector2f& Point : Fill)
                {
                    const float Depth=FMath::Clamp((Point.Y-Center.Y+Radius)/(Radius*2.f),0.f,1.f);
                    const float Side=FMath::Clamp((Point.X-Center.X+Radius)/(Radius*2.f),0.f,1.f);
                    const FLinearColor Color=FMath::Lerp(FLinearColor(.69f,.036f,.049f,1.f),FLinearColor(.17f,.004f,.016f,1.f),Depth)
                        *FMath::Lerp(1.08f,.77f,Side);
                    Vertices.Add(Vertex(Point,Color));
                }
                for (int32 N=1;N+1<Fill.Num();++N)
                { Indices.Add(static_cast<SlateIndex>(Start)); Indices.Add(static_cast<SlateIndex>(Start+N)); Indices.Add(static_cast<SlateIndex>(Start+N+1)); }
                if (Sample.Ratio<1.f)
                {
                    TArray<FVector2f> Meniscus; Meniscus.Reserve(68);
                    const float Top=Cut+.6f;
                    for (int32 N=0;N<Fill.Num();++N)
                    {
                        const FVector2f A=Fill[N],B=Fill[(N+1)%Fill.Num()];
                        const bool InA=A.Y<=Top,InB=B.Y<=Top;
                        if (InA) Meniscus.Add(A);
                        if (InA!=InB) Meniscus.Add(A+(B-A)*((Top-A.Y)/(B.Y-A.Y)));
                    }
                    Polygon(Meniscus,FLinearColor(.86f,.092f,.10f,1.f));
                }
            }
        }
        Ring(Center,29.5f,30.5f,FLinearColor(.46f,.28f,.11f,1.f),BronzeShade);
        // Independent text bands: a permanent percentage plaque below the
        // reservoir and an empty-state placard only over the empty well.
        Box({4.f,80.f},{108.f,104.f},PanelDark);
        Box({4.f,80.f},{108.f,81.f},BronzeLight);
        Box({4.f,103.f},{108.f,104.f},BronzeShade);
        const bool bState=!Sample.bAvailable || Sample.Current<=0.0;
        if (bState)
        {
            Box({4.f,28.f},{108.f,52.f},WellDark);
            Box({4.f,28.f},{108.f,28.75f},BronzeDim);
            Box({4.f,51.25f},{108.f,52.f},BronzeShade);
        }
        const FSlateResourceHandle Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
        FSlateDrawElement::MakeCustomVerts(Elements,Layer,Resource,Vertices,Indices,nullptr,0,0,Effect);
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        auto Label=[&](const FText& Text,const FSlateFontInfo& Font,float Y,FLinearColor Color)
        {
            const FVector2D Size=Measure->Measure(Text,Font);
            if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y)) return;
            const FVector2f At(4.f+FMath::Max(0.f,(104.f-static_cast<float>(Size.X))*.5f),
                Y+FMath::Max(0.f,(24.f-static_cast<float>(Size.Y))*.5f));
            FSlateDrawElement::MakeText(Elements,Layer+1,
                Geometry.MakeChild(FVector2f(104.f,24.f),FSlateLayoutTransform(At)).ToPaintGeometry(),
                Text,Font,Effect,Color*Tint);
        };
        Label(Sample.bAvailable ? FText::Format(LOCTEXT("HealthPercent","HP {0}%"),FText::AsNumber(Sample.Percent)) :
            LOCTEXT("MenuHealthUnknownValue","HP --"),ValueFont,80.f,Sample.bAvailable ? Ink : Muted);
        if (!Sample.bAvailable) Label(LOCTEXT("MenuHealthUnavailable","unavailable"),StateFont,28.f,Muted);
        else if (Sample.Current<=0.0) Label(LOCTEXT("MeasuredDefeat","DEFEATED"),StateFont,28.f,Ink);
        return Layer+1;
    }
};


// Player-local skill assignment gestures; no drop or cancel invokes combat.
FText LocalSkillName(int32 Identity)
{
    return Identity==0 ? LOCTEXT("LocalGuardName","Rooted Guard") : Identity==1 ?
        LOCTEXT("LocalCurrentName","Passing Current") : LOCTEXT("LocalEmptyName","Unbound");
}
FText LocalHotbarName(FJadeboundHotbarEntry Entry)
{
    return Entry.Kind==EJadeboundHotbarKind::Item ? LOCTEXT("LocalBalmName","Balm") :
        Entry.Kind==EJadeboundHotbarKind::Skill ? LocalSkillName(Entry.Identity) : LOCTEXT("LocalTypedEmpty","Empty");
}
bool PlainSkillPointer(const FPointerEvent& Event)
{
    return !Event.IsTouchEvent() && !Event.IsControlDown() && !Event.IsShiftDown() &&
        !Event.IsAltDown() && !Event.IsCommandDown();
}
class FJadeboundSkillDrag final : public FDragDropOperation
{
public:
    DRAG_DROP_OPERATOR_TYPE(FJadeboundSkillDrag,FDragDropOperation)
    TWeakObjectPtr<AJadeboundProgressionActor> Actor;
    TWeakObjectPtr<APawn> Pawn;
    TWeakPtr<SWidget> Panel;
    FJadeboundHotbarEntry Binding;
    int32 InventorySlot=INDEX_NONE;
    int64 InventoryRevision=-1;
    int64 Revision=-1;
    bool bContextCancelled=false;
    void ObserveContext()
    {
        if (!Actor.IsValid() || Actor->IsActorBeingDestroyed() || !Actor->CanConfigureSkillHotbar() ||
            !Pawn.IsValid() || Actor->GetHealthPresentation().Pawn!=Pawn || !Panel.IsValid() ||
            Actor->GetSkillPresentation().HotbarRevision!=Revision || !Binding.IsValid() ||
            (Binding.Kind==EJadeboundHotbarKind::Item && Actor->GetRevision()!=InventoryRevision) || !FSlateApplication::IsInitialized() ||
            !FSlateApplication::Get().IsActive()) bContextCancelled=true;
    }
    virtual void OnDragged(const FDragDropEvent& Event) override
    { ObserveContext();FDragDropOperation::OnDragged(Event); }
    static TSharedRef<FJadeboundSkillDrag> New(TWeakObjectPtr<AJadeboundProgressionActor> Weak,
        TWeakObjectPtr<APawn> Life,TWeakPtr<SWidget> Owner,FJadeboundHotbarEntry Entry,int64 Token,
        int32 OriginalSlot=INDEX_NONE,int64 OriginalRevision=-1)
    {
        TSharedRef<FJadeboundSkillDrag> Op=MakeShareable(new FJadeboundSkillDrag());
        Op->Actor=Weak;Op->Pawn=Life;Op->Panel=Owner;Op->Binding=Entry;Op->Revision=Token;
        Op->InventorySlot=OriginalSlot;Op->InventoryRevision=OriginalRevision;
        Op->bCreateNewWindow=false; // runtime Slate draws inside the owned game window
        Op->Construct();return Op;
    }
    virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
    { return SNew(SBorder).BorderImage(&FrameBrush).Padding(10.f)[Text(LocalHotbarName(Binding),15.f,Gold,true)]; }
    virtual ~FJadeboundSkillDrag() override { GuardRelease(); }
    virtual void OnDrop(bool Handled,const FPointerEvent& Event) override
    {
        GuardRelease(); // cancellation can supply an empty event; never inspect/cast from it
        FDragDropOperation::OnDrop(Handled,Event);
    }
private:
    void GuardRelease()
    { if (Actor.IsValid() && !Actor->IsActorBeingDestroyed()) Actor->MarkSkillHudGesture(); }
};
class SJadeboundSkillPaletteTile final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundSkillPaletteTile) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AJadeboundProgressionActor>, ProgressionActor)
        SLATE_ARGUMENT(TWeakPtr<SWidget>, PanelToken)
        SLATE_ARGUMENT(int32, SkillIdentity)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Actor=Args._ProgressionActor;Panel=Args._PanelToken;Skill=Args._SkillIdentity;SetCanTick(true);
        ChildSlot[SNew(SBorder).BorderImage(&CardBrush).Padding(8.f)
            .ToolTip(PackTooltip(Skill==0 ? LOCTEXT("AssignGuardTooltip","Glaive: 20 stamina, 4s cooldown. Frontal guard for 0.6s. Drag to assign; dropping never casts.") :
                LOCTEXT("AssignCurrentTooltip","Glaive: 25 stamina, 5s cooldown. Step to clear supported ground 30-180cm away. Drag to assign; dropping never casts."),LocalSkillName(Skill),0,210.f))
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[Text(LocalSkillName(Skill),15.f,Ink,true)]
                + SVerticalBox::Slot().AutoHeight().Padding(0.f,4.f)
                [Text(Skill==0 ? LOCTEXT("GuardPaletteCost","20 stamina / 4s") : LOCTEXT("CurrentPaletteCost","25 stamina / 5s"),12.f,Gold)]
                + SVerticalBox::Slot().AutoHeight()[Text(LOCTEXT("DragSkillHint","Drag to F1-F10"),12.f,Muted)]]];
    }
    virtual void Tick(const FGeometry& Geometry,double Now,float Delta) override
    {
        SCompoundWidget::Tick(Geometry,Now,Delta);
        if (const TSharedPtr<FJadeboundSkillDrag> Op=ActiveDrag.Pin()) Op->ObserveContext();
        if (PressRevision>=0 && (!Actor.IsValid() || !Actor->CanConfigureSkillHotbar() ||
            Actor->GetHealthPresentation().Pawn!=PressPawn || !FSlateApplication::Get().IsActive())) PressRevision=-1;
    }
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& Event) override
    {
        PressRevision=-1;PressPawn.Reset();
        if (Actor.IsValid()) Actor->MarkSkillHudGesture();
        if (Event.GetEffectingButton()!=EKeys::LeftMouseButton || !PlainSkillPointer(Event) ||
            !Actor.IsValid() || !Actor->CanConfigureSkillHotbar() || !Panel.IsValid()) return FReply::Handled();
        PressRevision=Actor->GetSkillPresentation().HotbarRevision;
        PressPawn=Actor->GetHealthPresentation().Pawn;
        return FReply::Handled().DetectDrag(SharedThis(this),EKeys::LeftMouseButton);
    }
    virtual FReply OnDragDetected(const FGeometry&,const FPointerEvent& Event) override
    {
        if (!Actor.IsValid() || !Actor->CanConfigureSkillHotbar() || !Panel.IsValid() || !PressPawn.IsValid() ||
            !PlainSkillPointer(Event) || !Event.IsMouseButtonDown(EKeys::LeftMouseButton) || PressRevision<0 ||
            Actor->GetSkillPresentation().HotbarRevision!=PressRevision || Actor->GetHealthPresentation().Pawn!=PressPawn)
            return FReply::Handled();
        Actor->MarkSkillHudGesture();
        const TSharedRef<FJadeboundSkillDrag> Op=FJadeboundSkillDrag::New(Actor,PressPawn,Panel,FJadeboundHotbarEntry::Skill(Skill),PressRevision);
        ActiveDrag=Op;return FReply::Handled().BeginDragDrop(Op);
    }
    virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&) override
    { if (Actor.IsValid()) Actor->MarkSkillHudGesture();PressRevision=-1;return FReply::Handled(); }
    virtual FReply OnMouseButtonDoubleClick(const FGeometry&,const FPointerEvent&) override
    { if (Actor.IsValid()) Actor->MarkSkillHudGesture();return FReply::Handled(); }
private:
    TWeakObjectPtr<AJadeboundProgressionActor> Actor;
    TWeakObjectPtr<APawn> PressPawn;
    TWeakPtr<SWidget> Panel;
    int32 Skill=INDEX_NONE;
    int64 PressRevision=-1;
    TWeakPtr<FJadeboundSkillDrag> ActiveDrag;
};
// Balm uses a noncapturing tile: child SButtons cannot steal DetectDrag.
// A normal click inspects once on release; dragging/cancelling never uses it.
class SJadeboundBalmDragTile final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundBalmDragTile) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AJadeboundProgressionActor>,ProgressionActor)
        SLATE_ARGUMENT(TWeakPtr<SWidget>,PanelToken)
        SLATE_ARGUMENT(int32,InventorySlot)
        SLATE_ARGUMENT(int64,InventoryRevision)
        SLATE_ARGUMENT(const FSlateBrush*,Background)
        SLATE_EVENT(FOnClicked,OnInspect)
        SLATE_DEFAULT_SLOT(FArguments,Content)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Actor=Args._ProgressionActor;Panel=Args._PanelToken;Slot=Args._InventorySlot;
        RenderedRevision=Args._InventoryRevision;Inspect=Args._OnInspect;SetCanTick(true);
        ChildSlot[SNew(SBorder).BorderImage(Args._Background).Padding(FMargin(6.f,5.f))[Args._Content.Widget]];
    }
    virtual bool SupportsKeyboardFocus() const override {return true;}
    virtual void Tick(const FGeometry& Geometry,double Now,float Delta) override
    {
        SCompoundWidget::Tick(Geometry,Now,Delta);
        if(const auto Op=ActiveDrag.Pin()) Op->ObserveContext();
        if(Pressed && !PressContext()) Pressed=false; // one-way loss for this press
    }
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& Event) override
    {
        if(Event.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
        Pressed=false;PressPawn.Reset();
        if(Actor.IsValid()) Actor->MarkSkillHudGesture();
        if(!PlainSkillPointer(Event) || !Actor.IsValid() || !Actor->CanConfigureSkillHotbar() ||
            !Panel.IsValid() || RenderedRevision<0 || Actor->GetRevision()!=RenderedRevision) return FReply::Handled();
        PressPawn=Actor->GetHealthPresentation().Pawn;
        PressHotbarRevision=Actor->GetSkillPresentation().HotbarRevision;
        Pressed=PressContext();
        return Pressed ? FReply::Handled().SetUserFocus(SharedThis(this),EFocusCause::Mouse).DetectDrag(SharedThis(this),EKeys::LeftMouseButton) : FReply::Handled();
    }
    virtual FReply OnDragDetected(const FGeometry&,const FPointerEvent& Event) override
    {
        if(!Pressed || !PressContext() || !PlainSkillPointer(Event) || !Event.IsMouseButtonDown(EKeys::LeftMouseButton))
        {Pressed=false;return FReply::Handled();}
        Actor->MarkSkillHudGesture();
        const auto Op=FJadeboundSkillDrag::New(Actor,PressPawn,Panel,FJadeboundHotbarEntry::Item(7),PressHotbarRevision,Slot,RenderedRevision);
        ActiveDrag=Op;Pressed=false;
        return FReply::Handled().BeginDragDrop(Op);
    }
    virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent& Event) override
    {
        if(Event.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
        const bool DoInspect=Pressed && PressContext() && PlainSkillPointer(Event);
        Pressed=false;if(Actor.IsValid())Actor->MarkSkillHudGesture();
        return DoInspect && Inspect.IsBound() ? Inspect.Execute() : FReply::Handled();
    }
    virtual FReply OnMouseButtonDoubleClick(const FGeometry&,const FPointerEvent& Event) override
    {
        if(Event.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
        Pressed=false;if(Actor.IsValid())Actor->MarkSkillHudGesture();return FReply::Handled();
    }
    virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent& Event) override
    {
        if(Event.GetKey()!=EKeys::Enter && Event.GetKey()!=EKeys::SpaceBar) return FReply::Unhandled();
        if(Actor.IsValid())Actor->MarkSkillHudGesture();
        return !Event.IsRepeat() && Inspect.IsBound() ? Inspect.Execute() : FReply::Handled();
    }
private:
    TWeakObjectPtr<AJadeboundProgressionActor> Actor;
    TWeakObjectPtr<APawn> PressPawn;
    TWeakPtr<SWidget> Panel;
    TWeakPtr<FJadeboundSkillDrag> ActiveDrag;
    FOnClicked Inspect;
    int32 Slot=INDEX_NONE;
    int64 RenderedRevision=-1,PressHotbarRevision=-1;
    bool Pressed=false;
    bool PressContext() const
    {
        if(!Actor.IsValid() || !Actor->CanConfigureSkillHotbar() || !Panel.IsValid() || !PressPawn.IsValid() ||
            Actor->GetHealthPresentation().Pawn!=PressPawn || Actor->GetRevision()!=RenderedRevision ||
            Actor->GetSkillPresentation().HotbarRevision!=PressHotbarRevision || !FSlateApplication::IsInitialized() ||
            !FSlateApplication::Get().IsActive()) return false;
        const auto& State=Actor->GetProgressionState();
        return State.ExactRevision==static_cast<uint64>(RenderedRevision) && State.Slots.IsValidIndex(Slot) &&
            State.Slots[Slot].ItemId==7 && State.Slots[Slot].Count>0;
    }
};

// One row-local display sample per engine frame. This is never an acceptance cache.
class FJadeboundAssignmentDisplaySnapshot final
{
public:
    explicit FJadeboundAssignmentDisplaySnapshot(TWeakObjectPtr<AJadeboundProgressionActor> Source)
        : Actor(Source) { Clear(); }
    const FJadeboundSkillPresentation& GetView() const { return View; }
    void Refresh()
    {
        const uint64 Frame=GFrameCounter;
        if (!bHasFrame || Frame!=LastFrame)
        {
            LastFrame=Frame;bHasFrame=true;bSampledThisFrame=false;bInvalidatedThisFrame=false;
        }
        AJadeboundProgressionActor* CurrentActor=Actor.Get();
        APlayerController* CurrentPlayer=CurrentActor ? Cast<APlayerController>(CurrentActor->GetOwner()) : nullptr;
        APawn* CurrentPawn=IsValid(CurrentPlayer) ? CurrentPlayer->GetPawn() : nullptr;
        const int64 Revision=CurrentActor ? CurrentActor->GetRevision() : INDEX_NONE;
        const bool ContextPresent=CurrentActor && IsValid(CurrentPlayer) && IsValid(CurrentPawn) &&
            CurrentActor->IsMenuOpen() && CurrentActor->IsProgressionReady();
        // A changed owner/pawn, committed revision, or missing menu/readiness clears
        // every cell's shared rendered token. Recovery waits for the next engine frame.
        if (!ContextPresent || (bSampledThisFrame &&
            (Player.Get()!=CurrentPlayer || Pawn.Get()!=CurrentPawn || ObservedRevision!=Revision)))
        {
            if (!bInvalidatedThisFrame) Clear();
            bInvalidatedThisFrame=true;return;
        }
        if (bInvalidatedThisFrame || bSampledThisFrame) return;
        Player=CurrentPlayer;Pawn=CurrentPawn;ObservedRevision=Revision;
        View=CurrentActor->GetSkillPresentation();
        bSampledThisFrame=true;
    }
private:
    void Clear()
    {
        View=FJadeboundSkillPresentation{};
        for (auto& Entry:View.SlotEntries) Entry={};
        View.HotbarRevision=INDEX_NONE;
    }
    TWeakObjectPtr<AJadeboundProgressionActor> Actor;
    TWeakObjectPtr<APlayerController> Player;
    TWeakObjectPtr<APawn> Pawn;
    FJadeboundSkillPresentation View;
    uint64 LastFrame=0;
    int64 ObservedRevision=INDEX_NONE;
    bool bHasFrame=false;
    bool bSampledThisFrame=false;
    bool bInvalidatedThisFrame=false;
};

// Original action glyphs and actual blue stamina. These leaves are paint-only;
// their attributes read the existing HUD or shared menu presentation snapshot.
class SJadeboundMartialGlyph final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundMartialGlyph) {}
        SLATE_ATTRIBUTE(int32, Identity)
        SLATE_ATTRIBUTE(double, Cooldown)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    { Identity=Args._Identity; Cooldown=Args._Cooldown; ArtResources=AcquireOriginalArt(); SetVisibility(EVisibility::HitTestInvisible); ForceVolatile(true); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(40.f,40.f); }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,
        FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const override
    {
        const int32 Id=Identity.Get(INDEX_NONE);
        if ((Id!=0 && Id!=1 && Id!=7) || !FSlateApplication::IsInitialized()) return L;
        const FVector2D Ext=G.GetLocalSize();
        if (Ext.X<8.0 || Ext.Y<8.0 || !FMath::IsFinite(Ext.X) || !FMath::IsFinite(Ext.Y)) return L;
        const FSlateBrush* Original = Id==7 ? ArtResources->Item(7) : ArtResources->Skill(Id);
        if (Original)
        {
            const float Side=FMath::Min(static_cast<float>(Ext.X),static_cast<float>(Ext.Y));
            const auto Icon=G.MakeChild(FVector2f(Side,Side),FSlateLayoutTransform(FVector2f(
                (static_cast<float>(Ext.X)-Side)*.5f,(static_cast<float>(Ext.Y)-Side)*.5f))).ToPaintGeometry();
            FSlateDrawElement::MakeBox(E,L,Icon,Original,Enabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
                Original->GetTint(S) * S.GetColorAndOpacityTint());
        }
        TArray<FSlateVertex> V; TArray<SlateIndex> I; V.Reserve(180); I.Reserve(270);
        auto Poly=[&](std::initializer_list<FVector2f> Points,FLinearColor C)
        {
            const int32 Start=V.Num();
            for (const auto& P:Points) V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(G.GetAccumulatedRenderTransform(),
                FVector2f(P.X*static_cast<float>(Ext.X),P.Y*static_cast<float>(Ext.Y)),FVector2f(.5f,.5f),(C*S.GetColorAndOpacityTint()).ToFColor(true)));
            for (int32 N=1;N+1<static_cast<int32>(Points.size());++N)
            { I.Add(static_cast<SlateIndex>(Start)); I.Add(static_cast<SlateIndex>(Start+N)); I.Add(static_cast<SlateIndex>(Start+N+1)); }
        };
        const FLinearColor Steel(.73f,.79f,.72f,1.f), Edge(.97f,.92f,.71f,1.f), Wood(.25f,.12f,.055f,1.f);
        if (!Original)
        {
        if (Id==0)
        {
            // A rooted glaive behind a protective angular arc, with real empty space.
            Poly({{.18f,.67f},{.23f,.38f},{.30f,.42f},{.24f,.67f}},Jade);
            Poly({{.23f,.38f},{.50f,.25f},{.50f,.33f},{.30f,.42f}},Jade);
            Poly({{.50f,.25f},{.77f,.38f},{.70f,.42f},{.50f,.33f}},Jade);
            Poly({{.70f,.42f},{.77f,.38f},{.82f,.67f},{.76f,.67f}},Jade);
            Poly({{.46f,.73f},{.52f,.73f},{.56f,.37f},{.50f,.36f}},Wood);
            Poly({{.49f,.42f},{.56f,.42f},{.69f,.28f},{.64f,.20f},{.54f,.25f}},Steel);
            Poly({{.54f,.25f},{.64f,.20f},{.62f,.24f}},Edge);
            Poly({{.64f,.20f},{.69f,.28f},{.66f,.31f},{.62f,.24f}},Edge);
            Poly({{.22f,.72f},{.78f,.72f},{.74f,.77f},{.26f,.77f}},Bronze);
        }
        else if (Id==1)
        {
            // Sweeping bronze haft and distinct steel edge through a water trace.
            const FLinearColor Water(.055f,.35f,.36f,1.f);
            Poly({{.13f,.66f},{.25f,.49f},{.30f,.51f},{.20f,.66f}},Water);
            Poly({{.25f,.49f},{.47f,.40f},{.48f,.46f},{.30f,.51f}},Water);
            Poly({{.47f,.40f},{.78f,.40f},{.64f,.45f},{.48f,.46f}},Water);
            Poly({{.19f,.74f},{.42f,.67f},{.49f,.70f},{.27f,.78f}},Jade);
            Poly({{.42f,.67f},{.66f,.54f},{.72f,.55f},{.49f,.70f}},Jade);
            Poly({{.66f,.54f},{.85f,.36f},{.72f,.55f},{.69f,.58f}},Jade);
            Poly({{.27f,.70f},{.31f,.73f},{.70f,.36f},{.66f,.33f}},Bronze);
            Poly({{.63f,.38f},{.72f,.36f},{.85f,.23f},{.82f,.16f},{.72f,.18f},{.65f,.28f}},Steel);
            Poly({{.72f,.18f},{.82f,.16f},{.79f,.21f}},Edge);
            Poly({{.82f,.16f},{.85f,.23f},{.81f,.27f},{.79f,.21f}},Edge);
        }
        else // Item7: existing original balm artwork, with a passive salve-pot fallback.
        {
            Poly({{.24f,.33f},{.76f,.33f},{.83f,.76f},{.70f,.90f},{.30f,.90f},{.17f,.76f}},FLinearColor(.035f,.16f,.12f,1.f));
            Poly({{.24f,.33f},{.51f,.33f},{.48f,.84f},{.30f,.84f},{.23f,.74f}},Jade);
            Poly({{.51f,.33f},{.76f,.33f},{.77f,.74f},{.70f,.84f},{.48f,.84f}},FLinearColor(.13f,.37f,.24f,1.f));
            Poly({{.21f,.20f},{.79f,.20f},{.79f,.34f},{.21f,.34f}},FLinearColor(.14f,.07f,.025f,1.f));
            Poly({{.21f,.20f},{.79f,.20f},{.73f,.25f},{.27f,.25f}},Bronze);
            Poly({{.30f,.44f},{.69f,.44f},{.69f,.70f},{.30f,.70f}},Edge);
            Poly({{.40f,.62f},{.43f,.65f},{.58f,.51f},{.55f,.48f}},FLinearColor(.035f,.16f,.12f,1.f));
            Poly({{.48f,.55f},{.47f,.58f},{.57f,.63f},{.59f,.60f}},FLinearColor(.035f,.16f,.12f,1.f));
        }
        }
        const double Remaining=Cooldown.Get(0.0);
        if (FMath::IsFinite(Remaining) && Remaining>0.0)
        {
            const float Ratio=FMath::Clamp(static_cast<float>(Remaining/(Id==0 ? 4.0 : 5.0)),0.f,1.f);
            Poly({{.10f,.18f},{.90f,.18f},{.90f,.18f+.61f*Ratio},{.10f,.18f+.61f*Ratio}},FLinearColor(.003f,.009f,.014f,.68f));
        }
        const int32 MaskLayer=Original ? L+1 : L;
        if (I.Num()>0)
        {
            const auto R=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
            FSlateDrawElement::MakeCustomVerts(E,MaskLayer,R,V,I,nullptr,0,0,Enabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect);
        }
        return I.Num()>0 ? MaskLayer : L;
    }
private:
    TSharedPtr<FJadeboundOriginalArt> ArtResources;
    TAttribute<int32> Identity; TAttribute<double> Cooldown;
};

class SJadeboundSkillAssignmentTarget final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundSkillAssignmentTarget) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AJadeboundProgressionActor>, ProgressionActor)
        SLATE_ARGUMENT(TWeakPtr<SWidget>, PanelToken)
        SLATE_ARGUMENT(int32, SlotIndex)
        SLATE_ARGUMENT(TSharedPtr<FJadeboundAssignmentDisplaySnapshot>, DisplaySnapshot)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Actor=Args._ProgressionActor;Panel=Args._PanelToken;Slot=Args._SlotIndex;
        Display=Args._DisplaySnapshot;
        if (!Display.IsValid()) Display=MakeShared<FJadeboundAssignmentDisplaySnapshot>(Actor);
        SetCanTick(true);Display->Refresh();
        ChildSlot[SNew(SBorder).BorderImage(&InsetBrush).Padding(3.f)
            .ToolTip(PackTooltip(LOCTEXT("AssignmentCellTooltip","Drop a skill or balm here to assign it. Right-click to clear. Dropping never uses an item or casts."),LOCTEXT("AssignmentCellTitle","F-KEY ASSIGNMENT"),0,210.f))
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [Text(FText::FromString(FString::Printf(TEXT("F%d"),Slot+1)),14.f,Gold,true)]
                + SVerticalBox::Slot().FillHeight(1.f)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).HAlign(HAlign_Center).VAlign(VAlign_Center)
                        [SNew(SJadeboundMartialGlyph).Cooldown(0.0)
                            .Identity_Lambda([this]() {
                                const FJadeboundHotbarEntry& Entry=Display->GetView().SlotEntries[Slot];
                                return Entry.Kind==EJadeboundHotbarKind::Item && Entry.Identity==7 ? 7 :
                                    Entry.Kind==EJadeboundHotbarKind::Skill ? Entry.Identity : INDEX_NONE;
                            })]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12.f)).ColorAndOpacity(Muted)
                        .Text_Lambda([this]() { return FText::FromString(Display->GetView().SlotEntries[Slot].Kind==EJadeboundHotbarKind::Empty ? TEXT("\u2014") : TEXT("")); })]]]];
    }
    virtual void Tick(const FGeometry& Geometry,double Now,float Delta) override
    { SCompoundWidget::Tick(Geometry,Now,Delta);Display->Refresh(); }
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& Event) override
    {
        if (Actor.IsValid()) Actor->MarkSkillHudGesture();
        if (Event.GetEffectingButton()==EKeys::RightMouseButton && PlainSkillPointer(Event) &&
            Actor.IsValid() && Actor->CanConfigureSkillHotbar())
            Actor->AssignSkillHotbarSlot(Slot,INDEX_NONE,Display->GetView().HotbarRevision);
        return FReply::Handled();
    }
    virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&) override
    { if(Actor.IsValid())Actor->MarkSkillHudGesture();return FReply::Handled(); }
    virtual FReply OnMouseButtonDoubleClick(const FGeometry&,const FPointerEvent&) override
    { if(Actor.IsValid())Actor->MarkSkillHudGesture();return FReply::Handled(); }
    virtual FReply OnDrop(const FGeometry&,const FDragDropEvent& Event) override
    {
        if(Actor.IsValid())Actor->MarkSkillHudGesture();
        const TSharedPtr<FJadeboundSkillDrag> Op=Event.GetOperationAs<FJadeboundSkillDrag>();
        if (Op.IsValid()) Op->ObserveContext();
        if (!Op.IsValid() || Op->bContextCancelled || !Actor.IsValid() || !Actor->CanConfigureSkillHotbar() || Op->Actor!=Actor ||
            !Panel.IsValid() || Op->Panel.Pin()!=Panel.Pin() || !Op->Pawn.IsValid() ||
            Actor->GetHealthPresentation().Pawn!=Op->Pawn || !PlainSkillPointer(Event) ||
            Event.GetEffectingButton()!=EKeys::LeftMouseButton || Display->GetView().HotbarRevision!=Op->Revision)
            return FReply::Handled();
        if(Op->Binding.Kind==EJadeboundHotbarKind::Item)
            Actor->AssignItemHotbarSlot(Slot,Op->InventorySlot,Op->Binding.Identity,Op->InventoryRevision,Op->Revision);
        else if(Op->Binding.Kind==EJadeboundHotbarKind::Skill)
            Actor->AssignSkillHotbarSlot(Slot,Op->Binding.Identity,Op->Revision); // only terminal drop may assign
        return FReply::Handled();
    }
private:
    TWeakObjectPtr<AJadeboundProgressionActor> Actor;
    TWeakPtr<SWidget> Panel;
    TSharedPtr<FJadeboundAssignmentDisplaySnapshot> Display;
    int32 Slot=0;
};

class SJadeboundStaminaWell final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundStaminaWell) {}
        SLATE_ATTRIBUTE(float, Stamina)
        SLATE_ATTRIBUTE(FText, ActiveLabel)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    { Stamina=Args._Stamina; ActiveLabel=Args._ActiveLabel; SetVisibility(EVisibility::HitTestInvisible); ForceVolatile(true); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(72.f,64.f); }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,
        FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const override
    {
        if (!FSlateApplication::IsInitialized()) return L;
        const FVector2D Ext=G.GetLocalSize();
        if (!FMath::IsFinite(Ext.X) || !FMath::IsFinite(Ext.Y) || Ext.X<60.0 || Ext.Y<60.0) return L;
        const float Actual=Stamina.Get(0.f);
        const bool Available=FMath::IsFinite(Actual);
        const float Ratio=Available ? FMath::Clamp(Actual/100.f,0.f,1.f) : 0.f;
        const FVector2f Center(static_cast<float>(Ext.X)*.5f,31.f);
        TArray<FSlateVertex> V; TArray<SlateIndex> I; V.Reserve(400); I.Reserve(1200);
        auto Add=[&](FVector2f P,FLinearColor C) { return FSlateVertex::Make<ESlateVertexRounding::Disabled>(
            G.GetAccumulatedRenderTransform(),P,FVector2f(.5f,.5f),(C*S.GetColorAndOpacityTint()).ToFColor(true)); };
        auto Disk=[&](float Radius,FLinearColor C,bool Fill)
        {
            TArray<FVector2f> Boundary; Boundary.Reserve(48);
            for(int32 N=0;N<48;++N) { const float A=2.f*PI*N/48.f; Boundary.Add(Center+FVector2f(FMath::Cos(A),FMath::Sin(A))*Radius); }
            TArray<FVector2f> Points; Points.Reserve(52);
            const float Cut=Center.Y+Radius*(1.f-2.f*Ratio);
            for(int32 N=0;N<Boundary.Num();++N)
            {
                const auto A=Boundary[N],B=Boundary[(N+1)%Boundary.Num()];
                const bool InA=!Fill || A.Y>=Cut,InB=!Fill || B.Y>=Cut;
                if(InA) Points.Add(A);
                if(InA!=InB) Points.Add(A+(B-A)*((Cut-A.Y)/(B.Y-A.Y)));
            }
            const int32 Start=V.Num();
            for(const auto& P:Points)
            {
                const float Depth=FMath::Clamp((P.Y-Center.Y+Radius)/(2.f*Radius),0.f,1.f);
                V.Add(Add(P,Fill ? FMath::Lerp(C,FLinearColor(.014f,.048f,.14f,1.f),Depth) : C));
            }
            for(int32 N=1;N+1<Points.Num();++N) { I.Add(static_cast<SlateIndex>(Start));I.Add(static_cast<SlateIndex>(Start+N));I.Add(static_cast<SlateIndex>(Start+N+1)); }
        };
        Disk(30.f,FLinearColor(.59f,.40f,.19f,1.f),false);
        Disk(28.3f,FLinearColor(.12f,.075f,.032f,1.f),false);
        Disk(26.7f,Gold*.64f,false); Disk(25.6f,WellDark,false);
        if(Available && Ratio>0.f) Disk(24.7f,FLinearColor(.09f,.33f,.65f,1.f),true);
        const auto R=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
        FSlateDrawElement::MakeCustomVerts(E,L,R,V,I,nullptr,0,0,Enabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect);
        const auto Font=FCoreStyle::GetDefaultFontStyle(TEXT("Medium"),11.f);
        const auto ValueFont=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),13.f);
        const FText Name=LOCTEXT("ActualStaminaLabel","Stamina");
        const FText Value=Available ? FText::FromString(FString::Printf(TEXT("%.0f"),Actual)) : LOCTEXT("StaminaUnavailable","--");
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        auto Label=[&](FText Text,const FSlateFontInfo& F,float Y)
        {
            const float X=FMath::Max(0.f,(static_cast<float>(Ext.X)-static_cast<float>(Measure->Measure(Text,F).X))*.5f);
            FSlateDrawElement::MakeText(E,L+1,G.MakeChild(FVector2f(static_cast<float>(Ext.X),18.f),FSlateLayoutTransform(FVector2f(X,Y))).ToPaintGeometry(),Text,F,
                Enabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,Ink*S.GetColorAndOpacityTint());
        };
        Label(Name,Font,11.f); Label(Value,ValueFont,29.f);
        const FText Effect=ActiveLabel.Get(FText::GetEmpty());
        if (!Effect.IsEmpty()) Label(Effect,Font,47.f);
        return L+1;
    }
private:
    TAttribute<float> Stamina;
    TAttribute<FText> ActiveLabel;
};

class SJadeboundSkillQuickbar final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundSkillQuickbar) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AJadeboundProgressionActor>, ProgressionActor)
        SLATE_ARGUMENT(TSharedPtr<FJadeboundHudDockLayout>, DockLayout)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Actor = Args._ProgressionActor;
        Dock = Args._DockLayout;
        SlotStyle.SetNormal(FSlateRoundedBoxBrush(WellDark,2.f,Bronze,1.f))
            .SetHovered(FSlateRoundedBoxBrush(SectionDark,2.f,Gold*.65f,1.f))
            .SetPressed(FSlateRoundedBoxBrush(PanelDark,2.f,Gold,1.f))
            .SetDisabled(FSlateRoundedBoxBrush(WellDark,2.f,BronzeDim,.8f))
            .SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
        SelectedStyle = SlotStyle;
        SelectedStyle.SetNormal(FSlateRoundedBoxBrush(WellDark,2.f,Good*.65f,1.5f))
            .SetHovered(FSlateRoundedBoxBrush(SectionDark,2.f,Good*.65f,1.5f));
        WalkStyle = SlotStyle;
        WalkStyle.SetNormal(FSlateRoundedBoxBrush(SectionDark,2.f,Bronze,1.f));
        SetCanTick(true);
        SetVisibility(TAttribute<EVisibility>::CreateLambda([Weak=Actor]()
        { return Weak.IsValid() && !Weak->IsMenuOpen() && !Weak->IsDevelopmentModel() && Weak->GetNetMode() == NM_Standalone ? EVisibility::Visible : EVisibility::Collapsed; }));
        if (Actor.IsValid()) View=Actor->GetSkillPresentation();
        Rebuild();
    }
    virtual void Tick(const FGeometry& Geometry, double Now, float Delta) override
    {
        SCompoundWidget::Tick(Geometry,Now,Delta);
        if (Actor.IsValid()) View = Actor->GetSkillPresentation();
        if (StyledSelection != View.SelectedSlot)
        {
            StyledSelection = View.SelectedSlot;
            for (int32 Slot=0; Slot<10; ++Slot)
                if (SlotButtons[Slot].IsValid()) SlotButtons[Slot]->SetButtonStyle(Slot == StyledSelection ? &SelectedStyle : &SlotStyle);
        }
        // Ten 46-unit cells plus padding; the resource/frame is measured separately.
        // Full descriptions/costs live in tooltips, not oversized slot labels.
    }
    FVector2D SlotGridDesiredSize() const { return SlotGrid.IsValid() ? SlotGrid->GetDesiredSize() : FVector2D::ZeroVector; }
    virtual FReply OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
    {
        if (Event.GetEffectingButton() == EKeys::LeftMouseButton || Event.GetEffectingButton() == EKeys::RightMouseButton)
            if (Actor.IsValid()) Actor->MarkSkillHudGesture();
        return FReply::Unhandled(); // allow the contained LMB selection button
    }
    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
    virtual FReply OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
private:
    TWeakObjectPtr<AJadeboundProgressionActor> Actor;
    FJadeboundSkillPresentation View;
    TSharedPtr<FJadeboundHudDockLayout> Dock;
    TSharedPtr<SUniformGridPanel> SlotGrid;
    FButtonStyle SlotStyle, SelectedStyle, WalkStyle; // persistent, never temporary style pointers
    TSharedPtr<SButton> SlotButtons[10];
    TSharedPtr<IToolTip> SlotTips[4];
    int32 StyledSelection = INDEX_NONE;
    // Fixed one-row layout matches the Conquer F1-F10 ordering.
    void Rebuild()
    {
        SlotTips[0]=PackTooltip(LOCTEXT("HotbarEmptyHelp","Drag a skill or balm from your pack onto this F-key."),LOCTEXT("HotbarEmptyTitle","UNBOUND"),0,210.f);
        SlotTips[1]=PackTooltip(LOCTEXT("HotbarGuardHelp","20 Stamina  /  4s cooldown\nFrontal protection for 0.6s.\nClick to select; RMB or this F-key casts. Requires a glaive."),LOCTEXT("HotbarGuardTitle","ROOTED GUARD"),0,210.f);
        SlotTips[2]=PackTooltip(LOCTEXT("HotbarCurrentHelp","25 Stamina  /  5s cooldown\nStep onto clear ground 30-180cm away.\nClick to select; RMB or this F-key casts. Requires a glaive."),LOCTEXT("HotbarCurrentTitle","PASSING CURRENT"),0,210.f);
        SlotTips[3]=PackTooltip(LOCTEXT("HotbarBalmHelp","This F-key uses one owned balm.\nRestores 25% maximum health. Full health preserves it.\nClick to inspect; drag from your pack to assign."),LOCTEXT("HotbarBalmTitle","MOONLEAF BALM"),7,210.f);
        TSharedRef<SUniformGridPanel> Grid = SAssignNew(SlotGrid,SUniformGridPanel).SlotPadding(FMargin(2.f));
        for (int32 Slot=0; Slot<10; ++Slot)
            Grid->AddSlot(Slot, 0)
            [SNew(SBox).WidthOverride(46.f).HeightOverride(60.f)
                [SAssignNew(SlotButtons[Slot],SButton).ButtonStyle(&SlotStyle).ContentPadding(FMargin(0.f)).IsFocusable(false).IsEnabled_Lambda([this,Slot]() { return View.SlotEntries[Slot].Kind!=EJadeboundHotbarKind::Empty; })
                    .ToolTip(TAttribute<TSharedPtr<IToolTip>>::CreateLambda([this,Slot]() { const auto Entry=View.SlotEntries[Slot];
                        return SlotTips[Entry.Kind==EJadeboundHotbarKind::Item ? 3 : Entry.Kind!=EJadeboundHotbarKind::Skill ? 0 : Entry.Identity==0 ? 1 : 2]; }))
                    .OnClicked_Lambda([Weak=Actor,Slot]() { if(Weak.IsValid()) Weak->SelectSkillSlot(Slot); return FReply::Handled(); })
                    [SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
                        + SVerticalBox::Slot().AutoHeight()
                        [SNew(SBox).HeightOverride(16.f).VAlign(VAlign_Center).Padding(FMargin(3.f,0.f))
                            [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14.f)).ColorAndOpacity(Ink)
                                .Text_Lambda([Slot]() { return FText::FromString(FString::Printf(TEXT("F%d"),Slot+1)); })]]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                        [SNew(SBox).WidthOverride(40.f).HeightOverride(40.f)
                            [SNew(SOverlay)
                                + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                                [SNew(SJadeboundMartialGlyph)
                                    .Identity_Lambda([this,Slot]() { const auto Entry=View.SlotEntries[Slot]; return Entry.Kind==EJadeboundHotbarKind::Skill ? Entry.Identity : INDEX_NONE; })
                                    .Cooldown_Lambda([this,Slot]() { const auto Entry=View.SlotEntries[Slot]; return Entry.Kind==EJadeboundHotbarKind::Skill ? Entry.Identity==0 ? View.GuardCooldown : View.StepCooldown : 0.0; })]
                                + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                                [SNew(SBox).WidthOverride(40.f).HeightOverride(40.f)
                                    .Visibility_Lambda([this,Slot]() { return View.SlotEntries[Slot].Kind==EJadeboundHotbarKind::Item ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                                    [Art(7,40.f)]]
                                + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(FMargin(0.f,0.f,1.f,1.f))
                                [SNew(SBorder).BorderImage(&InsetBrush).Padding(FMargin(2.f,0.f))
                                    .Visibility_Lambda([this,Slot]() { return View.SlotEntries[Slot].Kind==EJadeboundHotbarKind::Item ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                                    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),11.f)).ColorAndOpacity(Gold)
                                        .Text_Lambda([this,Slot]() { return View.SlotEntries[Slot].Kind==EJadeboundHotbarKind::Item ? FText::FromString(FString::Printf(TEXT("x%d"),View.BalmCount)) : FText::GetEmpty(); })]]
                                + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                                [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),11.f)).ColorAndOpacity(Ink)
                                    .Text_Lambda([this,Slot]() { const auto Entry=View.SlotEntries[Slot];
                                const double Cooldown=Entry.Kind==EJadeboundHotbarKind::Skill ? Entry.Identity==0 ? View.GuardCooldown : View.StepCooldown : 0.0;
                                return Cooldown > 0.0 ? FText::FromString(FString::Printf(TEXT("%.1fs"),Cooldown)) : FText::GetEmpty(); })]]]
                        + SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(4.f)]]]];
        ChildSlot
        [SNew(SJadeboundArtBorder).HudRail(true).Padding(FMargin(4.f))
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0.f,0.f,8.f,0.f)
                [SNew(SBox).WidthOverride(72.f)
                    [SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()[SNew(SJadeboundStaminaWell).Stamina_Lambda([this]() { return View.Stamina; })
                            .ActiveLabel_Lambda([this]() { return View.bGuarding ? LOCTEXT("ActiveGuardCue","GUARDING") : View.bStepping ? LOCTEXT("ActiveCurrentCue","CURRENT") : FText::GetEmpty(); })]
                        + SVerticalBox::Slot().AutoHeight()
                        [SNew(SButton).ButtonStyle(&WalkStyle).IsFocusable(false)
                            .ToolTip(PackTooltip(LOCTEXT("WalkRunDockHelp","Click to switch walking and running.\nAssign your F-keys in the pack's Skills page."),LOCTEXT("WalkRunTitle","PACE"),0,210.f))
                            .OnClicked_Lambda([Weak=Actor]() { if(Weak.IsValid()) Weak->ToggleWalkRunFromHud(); return FReply::Handled(); })
                            [SNew(STextBlock).ColorAndOpacity(Ink).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12.f)).Justification(ETextJustify::Center)
                                .Text_Lambda([this]() { return FText::FromString(View.bHotbarConfigIssue ? View.bRun ? TEXT("RUN !") : TEXT("WALK !") : View.bRun ? TEXT("RUN") : TEXT("WALK")); })]]]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
                [SNew(SVerticalBox)
                    // The sibling gear rail occupies this measured header. It
                    // remains passive, so only the original slots catch input.
                    + SVerticalBox::Slot().AutoHeight()
                    [SNew(SBox).HeightOverride(TAttribute<FOptionalSize>::CreateLambda([this]() { return FOptionalSize(Dock.IsValid() ? Dock->GearHeaderHeight : 28.f); }))]
                    + SVerticalBox::Slot().AutoHeight()[Grid]]]];
    }
};

// Slate-only view cache. A full snapshot is copied only when the cheap
// committed revision changes or the first ready state arrives, never per paint.
class SJadeboundGameplayHud final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundGameplayHud) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AJadeboundProgressionActor>, ProgressionActor)
        SLATE_ARGUMENT(TSharedPtr<FJadeboundHudDockLayout>, DockLayout)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Actor=Args._ProgressionActor;
        Dock=Args._DockLayout;
        SetCanTick(true);
        SetVisibility(TAttribute<EVisibility>::CreateLambda([Weak=Actor]()
            {return Weak.IsValid() && !Weak->IsMenuOpen() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;}));
        Refresh();
    }
    virtual void Tick(const FGeometry& Geometry,double Now,float Delta) override
    {
        SCompoundWidget::Tick(Geometry,Now,Delta);
        if(Now>=NextPoll){NextPoll=Now+.1;Refresh();}
    }
    FVector2D EquipmentDesiredSize() const
    {
        if (GearCard.IsValid()) return GearCard->GetDesiredSize();
        return FVector2D::ZeroVector;
    }
private:
    TWeakObjectPtr<AJadeboundProgressionActor> Actor;
    TSharedPtr<FJadeboundHudDockLayout> Dock;
    TSharedPtr<SBox> GearCard;
    int64 Revision=-1, Coins=-1;
    int32 Weapon=0, QuestState=-1;
    bool bReady=false, bDevelopment=false;
    bool bHasState=false;
    double NextPoll=0, RoutineFeedbackUntil=0;
    int64 ReceiptSequence=0;
    bool bFeedbackVisible=false, bRoutineSave=false, bFeedbackWarning=false;
    FText Quest, Feedback, WeaponName;
    FJadeboundPersistencePresentation Persistence;
    float FeedbackWidth() const
    {
        const float Width=Dock.IsValid() && FMath::IsFinite(Dock->GearWidth) && Dock->GearWidth>0.f
            ? Dock->GearWidth : 500.f;
        return FMath::Min(Width,500.f);
    }
    FMargin FeedbackPadding() const
    {
        const float Left=Dock.IsValid() && FMath::IsFinite(Dock->GearLeft) && Dock->GearLeft>=0.f
            ? Dock->GearLeft : 12.f;
        const float Base=Dock.IsValid() && FMath::IsFinite(Dock->GearBottom) && Dock->GearBottom>=0.f
            ? Dock->GearBottom : 144.f;
        const double Height=GearCard.IsValid() ? GearCard->GetDesiredSize().Y : 28.0;
        const double SafeHeight=FMath::IsFinite(Height) && Height>0.0 ? Height : 28.0;
        const double Raised=static_cast<double>(Base)+SafeHeight+8.0;
        const float Bottom=FMath::IsFinite(Raised) && Raised>=0.0 && Raised<=static_cast<double>(TNumericLimits<float>::Max())
            ? static_cast<float>(Raised) : 180.f;
        return FMargin(Left,0.f,0.f,Bottom);
    }
    void Refresh()
    {
        const AJadeboundProgressionActor* Model=Actor.Get();
        if(!Model)return;
        const bool Ready=Model->IsProgressionReady();
        const int64 NewRevision=Model->GetRevision(), NewCoins=Model->GetCoins();
        const FText NewQuest=Model->GetQuestText(), NewFeedback=Model->GetFeedback();
        const FJadeboundPersistencePresentation NewPersistence=Model->GetPersistencePresentation();
        const FJadeboundActionReceipt Receipt=Model->GetLastActionReceipt();
        const FString RawFeedback=NewFeedback.ToString();
        const bool RoutineSave=Ready && !Receipt.bHeldCandidate && !Receipt.bPersistenceBlocked
            && (Receipt.Sequence==0 || Receipt.bSucceeded)
            && RawFeedback==TEXT("Character saved and read back successfully.");
        if (RoutineSave && (!Feedback.EqualTo(NewFeedback) || ReceiptSequence!=Receipt.Sequence))
            RoutineFeedbackUntil=FPlatformTime::Seconds()+4.0;
        const bool InitialControls=Ready && RawFeedback==TEXT("LMB: walk / target attack   Ctrl+LMB: jump   F1-F10: assigned actions   RMB: loot / selected skill   I: bag   Progress saves automatically");
        const bool ShowFeedback=!NewFeedback.IsEmpty() && !InitialControls
            && (!RoutineSave || FPlatformTime::Seconds()<RoutineFeedbackUntil);
        const bool WarnFeedback=NewPersistence.FailureKind!=EJadeboundPersistenceFailureKind::None || !Ready || Receipt.bHeldCandidate || Receipt.bPersistenceBlocked
            || (Receipt.Sequence>0 && !Receipt.bSucceeded);
        const bool Changed=!bHasState || Revision!=NewRevision || Coins!=NewCoins || bReady!=Ready
            || !Quest.EqualTo(NewQuest) || !Feedback.EqualTo(NewFeedback) || bFeedbackVisible!=ShowFeedback
            || Persistence.FailureKind!=NewPersistence.FailureKind || !Persistence.Message.EqualTo(NewPersistence.Message)
            || bFeedbackWarning!=WarnFeedback || ReceiptSequence!=Receipt.Sequence;
        if(!Changed)return;
        if(!bHasState || Revision!=NewRevision || (!bReady && Ready))
        {
            const FJadeboundProgressionState State=Model->GetProgressionState();
            Weapon=State.EquippedWeapon;
            QuestState=State.QuestState;
            WeaponName=LOCTEXT("HudUnarmed","Unarmed");
            for(const FJadeboundItemView& Item:Model->GetItemDefinitions())
                if(Item.ItemId==Weapon){WeaponName=Item.Name;break;}
        }
        bHasState=true;Revision=NewRevision;Coins=NewCoins;bReady=Ready;
        bDevelopment=Model->IsDevelopmentModel();Quest=NewQuest;Feedback=NewFeedback;
        Persistence=NewPersistence;
        ReceiptSequence=Receipt.Sequence;bFeedbackVisible=ShowFeedback;bRoutineSave=RoutineSave;bFeedbackWarning=WarnFeedback;
        // Full quest/reward text stays in the pack Quest tooltip. Action
        // feedback remains readable above the gear rail under its original gates.
        const FText Task=Model->GetQuestObjectiveText();
        const FText Gear=bReady?WeaponName:LOCTEXT("HudLoading","Preparing your equipment...");
        ChildSlot
        [SNew(SOverlay)
            + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(0.f,24.f,28.f,0.f))
            [SNew(SBox).WidthOverride(240.f)
                .Visibility_Lambda([Weak=Actor]() { return Weak.IsValid() && !Weak->GetRevivePresentation().bWaiting ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                [SNew(SJadeboundArtBorder).QuietTrim(true).Padding(FMargin(8.f,6.f))
                    [SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [Text(LOCTEXT("HudJourney","A Stolen Spark"),16.f,Ink,true)]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.f,4.f,0.f,0.f)
                        // 240-unit card minus two 8-unit insets: reserve every
                        // wrapped objective line during the first desired-size
                        // pass, before this AutoHeight card is arranged.
                        [SNew(STextBlock).Text(Task).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14.f))
                            .ColorAndOpacity(Ink).WrapTextAt(224.f)]]]]
            // A passive sibling keeps action results out of the quest card and
            // above the measured gear; it never enlarges the quickbar hit area.
            + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom)
                .Padding(TAttribute<FMargin>::CreateLambda([this]() { return FeedbackPadding(); }))
            [SNew(SBox).WidthOverride(TAttribute<FOptionalSize>::CreateLambda([this]() { return FOptionalSize(FeedbackWidth()); }))
                .Visibility_Lambda([this]() { return bFeedbackVisible && Actor.IsValid() &&
                    (!Actor->GetRevivePresentation().bWaiting || Persistence.FailureKind!=EJadeboundPersistenceFailureKind::None)
                    ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                [SNew(SBorder).BorderImage(&InsetBrush).Padding(FMargin(8.f,4.f))
                    [SNew(STextBlock)
                        .Text(bRoutineSave ? LOCTEXT("HudSavedShort","Progress saved") : PlayerFeedback(Feedback,bDevelopment))
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14.f))
                        .ColorAndOpacity(bFeedbackWarning ? Warning : Ink).AutoWrapText(true)]]]
            + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom)
                .Padding(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(Dock.IsValid() ? Dock->GearLeft : 12.f,0.f,0.f,Dock.IsValid() ? Dock->GearBottom : 144.f); }))
            [SAssignNew(GearCard,SBox).WidthOverride(TAttribute<FOptionalSize>::CreateLambda([this]() { return FOptionalSize(Dock.IsValid() ? Dock->GearWidth : 540.f); }))
                // One passive equipment strip. The measured height may still
                // grow for a full wrapped name; never clip it to a fixed row.
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(FMargin(6.f,3.f))
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)[Art(bReady?Weapon:0,20.f)]
                        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f,0.f,12.f,0.f)
                        [SNew(STextBlock).Text(Gear).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Medium"),15.f)).ColorAndOpacity(Ink).AutoWrapText(true)]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)[Art(-2,18.f)]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [Text(FText::AsNumber(Coins),15.f,Gold,true)]]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f,0.f,16.f,0.f)
                        [Text(LOCTEXT("HudCompactPackHint","I Pack"),12.f,Muted)]]]]];
    }
};

// Presentation-only layout. Each original panel keeps its own hit geometry;
// the full menu still consumes background input and never casts or trades.
class SJadeboundSpatialPackLayout final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundSpatialPackLayout) {}
        SLATE_ARGUMENT(TSharedPtr<SWidget>, LeftPanel)
        SLATE_ARGUMENT(TSharedPtr<SWidget>, RightPanel)
        SLATE_ARGUMENT(TSharedPtr<SWidget>, BottomTray)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        check(Args._LeftPanel.IsValid() && Args._RightPanel.IsValid() && Args._BottomTray.IsValid());
        SetCanTick(true);
        SetVisibility(EVisibility::SelfHitTestInvisible);
        SetClipping(EWidgetClipping::ClipToBounds);
        ChildSlot
        [SNew(SConstraintCanvas).Visibility(EVisibility::SelfHitTestInvisible)
            + SConstraintCanvas::Slot().Anchors(FAnchors(0.f,0.f)).Alignment(FVector2D(0.f,0.f))
                .Offset(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(16.f*Fit,16.f*Fit,252.f*Fit,380.f*Fit); }))
                [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                    [SNew(SBox).WidthOverride(252.f).HeightOverride(380.f)[Args._LeftPanel.ToSharedRef()]]]
            + SConstraintCanvas::Slot().Anchors(FAnchors(1.f,0.f)).Alignment(FVector2D(1.f,0.f))
                .Offset(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(-16.f*Fit,16.f*Fit,232.f*Fit,432.f*Fit); }))
                [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                    [SNew(SBox).WidthOverride(232.f).HeightOverride(432.f)[Args._RightPanel.ToSharedRef()]]]
            + SConstraintCanvas::Slot().Anchors(FAnchors(.5f,1.f)).Alignment(FVector2D(.5f,1.f))
                .Offset(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(0.f,-16.f*Fit,548.f*Fit,128.f*Fit); }))
                [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                    [SNew(SBox).WidthOverride(548.f).HeightOverride(128.f)[Args._BottomTray.ToSharedRef()]]]];
    }
    virtual void Tick(const FGeometry& Geometry,double Now,float Delta) override
    {
        SCompoundWidget::Tick(Geometry,Now,Delta);
        const FVector2D Size=Geometry.GetLocalSize();
        if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) || Size.X<=1.0 || Size.Y<=1.0) return;
        // 676 leaves a 160-unit world gap between side panels. 600 includes
        // the 432-unit bag, 128-unit tray, 8-unit gap and 16-unit margins.
        const float Next=FMath::Max(.01f,FMath::Min(1.f,FMath::Min(static_cast<float>(Size.X)/676.f,static_cast<float>(Size.Y)/600.f)));
        if (!FMath::IsNearlyEqual(Fit,Next,.0001f))
        { Fit=Next;Invalidate(EInvalidateWidgetReason::Layout); }
    }
private:
    float Fit=1.f;
};

// Layout-only owner. It uses its actual allotted viewport and prepassed child
// sizes; gesture handlers remain on the original quickbar, byte for byte.
class SJadeboundGameplayDock final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundGameplayDock) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AJadeboundProgressionActor>, ProgressionActor)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Dock=MakeShared<FJadeboundHudDockLayout>();
        SetCanTick(true);
        SetVisibility(EVisibility::SelfHitTestInvisible);
        ChildSlot
        [SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom)
                .Padding(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(Dock->QuickbarLeft,0.f,0.f,Dock->QuickbarBottom); }))
                [SAssignNew(Quickbar,SJadeboundSkillQuickbar).ProgressionActor(Args._ProgressionActor).DockLayout(Dock)]
            + SOverlay::Slot()[SAssignNew(Gameplay,SJadeboundGameplayHud).ProgressionActor(Args._ProgressionActor).DockLayout(Dock)]
            + SOverlay::Slot()[SNew(SJadeboundHealthHud).ProgressionActor(Args._ProgressionActor).DockLayout(Dock)]];
    }
    virtual void Tick(const FGeometry& Geometry,double Now,float Delta) override
    {
        SCompoundWidget::Tick(Geometry,Now,Delta);
        const FVector2D Local=Geometry.GetLocalSize();
        if (!FMath::IsFinite(Local.X) || !FMath::IsFinite(Local.Y) || Local.X<320.0 || Local.Y<260.0 ||
            !Gameplay.IsValid() || !Quickbar.IsValid()) return;
        const float Width=static_cast<float>(Local.X), Height=static_cast<float>(Local.Y);
        const float GearWidth=FMath::Min(500.f,FMath::Max(1.f,Width-24.f));
        const FVector2D Gear=Gameplay->EquipmentDesiredSize();
        const FVector2D Skills=Quickbar->GetDesiredSize();
        const FVector2D Grid=Quickbar->SlotGridDesiredSize();
        if (!FMath::IsFinite(Gear.X) || !FMath::IsFinite(Gear.Y) || !FMath::IsFinite(Skills.X) || !FMath::IsFinite(Skills.Y) ||
            !FMath::IsFinite(Grid.X) || !FMath::IsFinite(Grid.Y) || Gear.X<=0.0 || Gear.Y<=0.0 ||
            Skills.X<=0.0 || Skills.Y<=0.0 || Grid.X<=0.0 || Grid.Y<=0.0) return;
        const float GearActualHeight=static_cast<float>(Gear.Y);
        const float SkillWidth=static_cast<float>(Skills.X);
        // Actual compact player chrome ends at104. Horizontal fit still uses
        // actual child desired sizes, with12-unit viewport/12-unit group gaps.
        const bool bBesideHealth=Width-12.f-SkillWidth>=104.f+12.f;
        const float QuickbarLeft=FMath::Max(0.f,bBesideHealth ? 116.f : (Width-SkillWidth)*.5f);
        const float QuickbarBottom=bBesideHealth ? 12.f : 112.f;
        const float GearLeft=QuickbarLeft+4.f+72.f+8.f;
        const float GearBottom=QuickbarBottom+4.f+static_cast<float>(Grid.Y)+4.f;
        const float GearHeaderHeight=GearActualHeight+4.f;
        const float HealthCenterY=Height-66.f;
        const bool Changed=!FMath::IsNearlyEqual(Dock->GearWidth,GearWidth,.25f) ||
            !FMath::IsNearlyEqual(Dock->GearLeft,GearLeft,.25f) || !FMath::IsNearlyEqual(Dock->QuickbarLeft,QuickbarLeft,.25f) ||
            !FMath::IsNearlyEqual(Dock->GearBottom,GearBottom,.25f) || !FMath::IsNearlyEqual(Dock->HealthCenterY,HealthCenterY,.25f) ||
            !FMath::IsNearlyEqual(Dock->GearHeaderHeight,GearHeaderHeight,.25f) || !FMath::IsNearlyEqual(Dock->QuickbarBottom,QuickbarBottom,.25f);
        if (!Changed) return;
        Dock->GearWidth=GearWidth; Dock->GearLeft=GearLeft; Dock->GearBottom=GearBottom;
        Dock->GearHeaderHeight=GearHeaderHeight;Dock->QuickbarBottom=QuickbarBottom;
        Dock->QuickbarLeft=QuickbarLeft; Dock->HealthCenterY=HealthCenterY;
        // The measured passive header may grow for a long equipped name.
        // No font shrink, token change or input callback is used to make it fit.
        Invalidate(EInvalidateWidgetReason::Layout);
    }
private:
    TSharedPtr<FJadeboundHudDockLayout> Dock;
    TSharedPtr<SJadeboundGameplayHud> Gameplay;
    TSharedPtr<SJadeboundSkillQuickbar> Quickbar;
};
}

void SJadeboundProgressionPanel::Construct(const FArguments& InArgs)
{
    ProgressionActor = InArgs._ProgressionActor;
    ActionStyle.SetNormal(FSlateRoundedBoxBrush(Jade,2.f,Good,.8f))
        .SetHovered(FSlateRoundedBoxBrush(SelectedJade,2.f,Good,1.f))
        .SetPressed(FSlateRoundedBoxBrush(SectionDark,2.f,Gold,1.f))
        .SetDisabled(FSlateRoundedBoxBrush(SectionDark,2.f,BronzeDim,1.f))
        .SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
    SecondaryStyle.SetNormal(FSlateRoundedBoxBrush(SectionDark,2.f,Bronze,1.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.06f,.047f,.029f,1.f),2.f,Gold,1.f))
        .SetPressed(FSlateRoundedBoxBrush(PanelDark,2.f,Bronze,1.f))
        .SetDisabled(FSlateRoundedBoxBrush(SectionDark,2.f,BronzeDim,1.f))
        .SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
    const TSharedRef<FJadeboundOriginalArt> Art = AcquireOriginalArt();
    if (Art->MerchantButton(0))
    {
        MerchantArtResources = Art;
        MerchantStyle = ActionStyle;
        MerchantStyle.SetNormal(*Art->MerchantButton(0)).SetHovered(*Art->MerchantButton(1))
            .SetPressed(*Art->MerchantButton(2)).SetDisabled(*Art->MerchantButton(3));
    }
    SlotStyle.SetNormal(FSlateRoundedBoxBrush(SectionDark,2.f,BronzeDim,1.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.052f,.067f,.060f,1.f),2.f,Gold,1.f))
        .SetPressed(FSlateRoundedBoxBrush(Jade,2.f,Good,1.f))
        .SetDisabled(FSlateRoundedBoxBrush(SectionDark,2.f,BronzeDim,1.f))
        .SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
    EmptySlotStyle = SlotStyle;
    EmptySlotStyle.SetNormal(FSlateRoundedBoxBrush(WellDark,2.f,BronzeDim,.8f));
    SelectedSlotStyle = SlotStyle;
    SelectedSlotStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.037f,.12f,.081f,1.f),2.f,Good,2.f));
    SetCanTick(true);
    if (ProgressionActor.IsValid() && ProgressionActor->IsMerchantInRange()) ContextPage=EContextPage::Merchant;
    RefreshFromActor(true);
}

FReply SJadeboundProgressionPanel::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape || Event.GetKey() == EKeys::I)
    {
        if (AJadeboundProgressionActor* Actor = ProgressionActor.Get()) Actor->CloseMenu();
        return FReply::Handled();
    }
    return SCompoundWidget::OnPreviewKeyDown(MyGeometry, Event);
}

FReply SJadeboundProgressionPanel::OnMouseButtonDown(const FGeometry&, const FPointerEvent&)
{
    return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SJadeboundProgressionPanel::OnMouseButtonUp(const FGeometry&, const FPointerEvent&)
{
    return FReply::Handled();
}

FReply SJadeboundProgressionPanel::OnMouseWheel(const FGeometry&, const FPointerEvent&)
{
    return FReply::Handled();
}

void SJadeboundProgressionPanel::Tick(const FGeometry& AllottedGeometry,
    double InCurrentTime, float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
    // Poll cheap accessors at 10 Hz. Copy the snapshot and rebuild the bounded
    // view only when committed state, range, readiness or feedback changes.
    if (InCurrentTime >= NextPollTime)
    {
        NextPollTime = InCurrentTime + 0.1;
        RefreshFromActor(false);
    }
}

void SJadeboundProgressionPanel::RefreshFromActor(bool bForce)
{
    const AJadeboundProgressionActor* Actor = ProgressionActor.Get();
    if (!Actor)
    {
        if (!bHasSnapshot || bActorWasValid || bForce)
        {
            DisplayState = FJadeboundProgressionState();
            Definitions.Reset();
            bReady = false;
            bCanSave = false;
            bMerchantInRange = false;
            QuestText = LOCTEXT("UnavailableQuest", "Your pack is unavailable.");
            FeedbackText = LOCTEXT("UnavailableActor", "Close this panel and return to the game.");
            PersistencePresentation = FJadeboundPersistencePresentation();
            bHasSnapshot = true;
            bActorWasValid = false;
            bLastReportedReady = false;
            RebuildContents();
        }
        return;
    }

    const int64 LatestRevision = Actor->GetRevision();
    const int64 LatestCoins = Actor->GetCoins();
    const FText LatestFeedback = Actor->GetFeedback();
    const FJadeboundPersistencePresentation LatestPersistence = Actor->GetPersistencePresentation();
    const bool bLatestReady = Actor->IsProgressionReady();
    const bool bLatestCanSave = Actor->CanSaveProgress();
    const bool bLatestRange = Actor->IsMerchantInRange();
    const FJadeboundActionReceipt Receipt = Actor->GetLastActionReceipt();
    const bool bAcknowledged = bAwaitingAction && Receipt.Sequence > PendingReceiptSequence
        && Receipt.Action == PendingAction && Receipt.ObservedRevision <= LatestRevision;
    const bool bWaitLong = bAwaitingAction && FPlatformTime::Seconds() - PendingSince >= 8.0;
    const bool bPendingChanged = bAcknowledged || bWaitLong != bPendingLong;
    if (bAcknowledged) bAwaitingAction = false;
    bPendingLong = bWaitLong && bAwaitingAction;
    // Health changes do not advance the inventory revision. Refresh only when
    // the selected consumable's restriction changes, never for every HP value.
    const FJadeboundInventorySlot* Selected = GetSelectedSlot();
    const FJadeboundItemView* SelectedItem = Selected ? FindDefinition(Selected->ItemId) : nullptr;
    const FText LatestUseRestriction = SelectedItem && Selected->Count > 0 && SelectedItem->HealingPercent > 0
        ? UseRestrictionForSlot(SelectedSlot) : FText::GetEmpty();
    const bool bUseRestrictionChanged = !LatestUseRestriction.EqualTo(RenderedSelectedUseRestriction)
        && !(FSlateApplication::IsInitialized() && FSlateApplication::Get().IsDragDropping());
    const FText LatestSplitRestriction = SplitRestrictionForSlot(SelectedSlot);
    const bool bSplitRestrictionChanged = !LatestSplitRestriction.EqualTo(RenderedSelectedSplitRestriction)
        && !(FSlateApplication::IsInitialized() && FSlateApplication::Get().IsDragDropping());
    const bool bChanged = bUseRestrictionChanged || bSplitRestrictionChanged || bPendingChanged || Receipt.Sequence != LastReceiptSequence || !bHasSnapshot || !bActorWasValid || LatestRevision != DisplayState.Revision
        || LatestCoins != DisplayState.Coins
        || !LatestFeedback.EqualTo(FeedbackText) || bLatestReady != bLastReportedReady
        || LatestPersistence.FailureKind != PersistencePresentation.FailureKind
        || !LatestPersistence.Message.EqualTo(PersistencePresentation.Message)
        || bLatestCanSave != bCanSave
        || bLatestRange != bMerchantInRange;
    if (bForce || bChanged)
    {
        const FJadeboundProgressionState NewState = Actor->GetProgressionState();
        if (SelectedSlot != INDEX_NONE && (!DisplayState.Slots.IsValidIndex(SelectedSlot)
            || !NewState.Slots.IsValidIndex(SelectedSlot)
            || DisplayState.Slots[SelectedSlot].ItemId != NewState.Slots[SelectedSlot].ItemId))
            SelectedSlot = INDEX_NONE;
        LastReceiptSequence = Receipt.Sequence;
        DisplayState = NewState;
        Definitions = Actor->GetItemDefinitions();
        if (Definitions.Num() > MaximumCatalogItems)
        {
            Definitions.SetNum(MaximumCatalogItems);
        }
        QuestText = Actor->GetQuestText();
        FeedbackText = LatestFeedback;
        PersistencePresentation = LatestPersistence;
        bReady = bLatestReady && DisplayState.Slots.Num() == DisplaySlotCount && DisplayState.Coins >= 0;
        bCanSave = bLatestCanSave;
        bMerchantInRange = bLatestRange;
        bHasSnapshot = true;
        bLastReportedReady = bLatestReady;
        RebuildContents();
    }
    bActorWasValid = true;
}

const FJadeboundItemView* SJadeboundProgressionPanel::FindDefinition(int32 ItemId) const
{
    return Definitions.FindByPredicate([ItemId](const FJadeboundItemView& Item) { return Item.ItemId == ItemId; });
}

const FJadeboundInventorySlot* SJadeboundProgressionPanel::GetSelectedSlot() const
{
    return DisplayState.Slots.IsValidIndex(SelectedSlot) ? &DisplayState.Slots[SelectedSlot] : nullptr;
}

bool SJadeboundProgressionPanel::HasCapacityForOne(int32 ItemId, int32 IgnoredSlot) const
{
    const FJadeboundItemView* Item = FindDefinition(ItemId);
    if (!Item || Item->StackLimit <= 0 || DisplayState.Slots.Num() != DisplaySlotCount)
    {
        return false;
    }
    for (int32 Index = 0; Index < DisplaySlotCount; ++Index)
    {
        const FJadeboundInventorySlot& Slot = DisplayState.Slots[Index];
        if (Index == IgnoredSlot || (Slot.ItemId == 0 && Slot.Count == 0)
            || (Slot.ItemId == ItemId && Slot.Count > 0 && Slot.Count < Item->StackLimit))
        {
            return true;
        }
    }
    return false;
}

FText SJadeboundProgressionPanel::ItemTooltip(const FJadeboundItemView& Item) const
{
    const FText Buy = Item.bPurchasable
        ? FText::Format(LOCTEXT("BuyTooltip", "Buy: {0} jade coins each"), FText::AsNumber(Item.BuyPrice))
        : LOCTEXT("CannotBuy", "Cannot be purchased");
    const FText Sell = Item.bSellable
        ? FText::Format(LOCTEXT("SellTooltip", "Sell: {0} jade coins each"), FText::AsNumber(Item.SellPrice))
        : LOCTEXT("CannotSell", "Cannot be sold");
    return FText::Format(LOCTEXT("ItemDetails", "{0}\n{1}\n\nStack limit: {2}\n{3}\n{4}\n\n{5}"),
        Item.Name, ItemLore(Item.ItemId), FText::AsNumber(Item.StackLimit), Buy, Sell,
        Item.HealingPercent > 0 ? LOCTEXT("ConsumableType", "Consumable - right-click in your pack to use one")
        : Item.bWeapon ? LOCTEXT("WeaponType", "Weapon - can be equipped") : LOCTEXT("OtherType", "Inventory item - cannot be equipped"));
}

FText SJadeboundProgressionPanel::BuyRestriction(const FJadeboundItemView& Item) const
{
    if (!bReady) return LOCTEXT("NotReady", "Your pack is not ready. Read the note below.");
    if (!bMerchantInRange) return LOCTEXT("OutOfRange", "Move closer to the merchant to trade.");
    if (!Item.bPurchasable || Item.BuyPrice < 0) return LOCTEXT("NotForSale", "This item is not for sale.");
    const AJadeboundProgressionActor* Actor = ProgressionActor.Get();
    if (Item.ItemId == 7 && (!Actor || (Actor->IsDevelopmentModel() ? !Actor->IsNativeConsumableContextAvailable() :
        (Actor->GetNetMode() != NM_Standalone || !Actor->HasAuthority()))))
        return LOCTEXT("BuyBalmReadyHero", "Balm purchase requires your living, ready hero.");
    if (DisplayState.Coins < Item.BuyPrice) return LOCTEXT("InsufficientCoins", "Not enough jade coins.");
    if (!HasCapacityForOne(Item.ItemId)) return LOCTEXT("BagFull", "No inventory space for this item.");
    return FText::GetEmpty();
}

FText SJadeboundProgressionPanel::SellRestriction(int32 Quantity) const
{
    if (!bReady) return LOCTEXT("NotReady", "Your pack is not ready. Read the note below.");
    const FJadeboundInventorySlot* Slot = GetSelectedSlot();
    const FJadeboundItemView* Item = Slot ? FindDefinition(Slot->ItemId) : nullptr;
    if (!Item || !Slot || Slot->Count <= 0) return LOCTEXT("SelectItem", "Select an occupied inventory slot.");
    if (!Item->bSellable || Item->SellPrice < 0) return LOCTEXT("CannotSellItem", "This item cannot be sold.");
    if (!bMerchantInRange) return LOCTEXT("OutOfRange", "Move closer to the merchant to trade.");
    if (Quantity <= 0 || Quantity > Slot->Count) return LOCTEXT("SaleQuantityChanged", "The selected stack no longer contains that many items.");
    int64 Total = 0;
    if (!Jadebound::Presentation::TryStackSaleTotal(Quantity, Item->SellPrice, DisplayState.Coins, Total))
        return LOCTEXT("CurrencyLimit", "This sale would exceed the coin limit.");
    return FText::GetEmpty();
}

FText SJadeboundProgressionPanel::EquipRestriction() const
{
    return EquipRestrictionForSlot(SelectedSlot);
}

FText SJadeboundProgressionPanel::EquipRestrictionForSlot(int32 SlotIndex) const
{
    if (!bReady) return LOCTEXT("NotReady", "Your pack is not ready. Read the note below.");
    const FJadeboundInventorySlot* Slot = DisplayState.Slots.IsValidIndex(SlotIndex) ? &DisplayState.Slots[SlotIndex] : nullptr;
    const FJadeboundItemView* Item = Slot ? FindDefinition(Slot->ItemId) : nullptr;
    if (!Item || !Slot || Slot->Count <= 0) return LOCTEXT("SelectItem", "Select an occupied inventory slot.");
    if (!Item->bWeapon) return LOCTEXT("SelectWeapon", "Select a weapon to equip.");
    if (Slot->ItemId == DisplayState.EquippedWeapon) return LOCTEXT("AlreadyEquipped", "This weapon is already equipped.");
    if (DisplayState.EquippedWeapon != 0 && !HasCapacityForOne(DisplayState.EquippedWeapon,
        Slot->Count == 1 ? SlotIndex : INDEX_NONE))
    {
        return LOCTEXT("SwapNeedsSpace", "Make room for the currently equipped weapon.");
    }
    return FText::GetEmpty();
}

FText SJadeboundProgressionPanel::UnequipRestriction() const
{
    if (!bReady) return LOCTEXT("NotReady", "Your pack is not ready. Read the note below.");
    if (DisplayState.EquippedWeapon == 0) return LOCTEXT("NothingEquipped", "No weapon is equipped.");
    if (!HasCapacityForOne(DisplayState.EquippedWeapon)) return LOCTEXT("UnequipNeedsSpace", "Make inventory space before unequipping.");
    return FText::GetEmpty();
}

FText SJadeboundProgressionPanel::UseRestrictionForSlot(int32 SlotIndex) const
{
    const AJadeboundProgressionActor* Actor = ProgressionActor.Get();
    if (!Actor || (Actor->IsDevelopmentModel() ? !Actor->IsNativeConsumableContextAvailable() :
        (Actor->GetNetMode() != NM_Standalone || !Actor->HasAuthority())))
        return LOCTEXT("UseReadyHero", "Balm use requires your living, ready hero.");
    if (!bReady || !Actor->IsProgressionReady()) return LOCTEXT("UseNotReady", "Your pack is not ready. Read the note below.");
    const FJadeboundInventorySlot* Slot = DisplayState.Slots.IsValidIndex(SlotIndex) ? &DisplayState.Slots[SlotIndex] : nullptr;
    const FJadeboundItemView* Item = Slot ? FindDefinition(Slot->ItemId) : nullptr;
    if (!Item || !Slot || Slot->Count <= 0 || Item->HealingPercent <= 0)
        return LOCTEXT("UseNotConsumable", "Select balm in your pack to use one.");
    const FJadeboundHealthPresentation Health = Actor->GetHealthPresentation();
    if (!Health.PlayerHealth.bAvailable || Health.PlayerHealth.Current <= 0.0)
        return LOCTEXT("UseLivingHero", "You must be alive to use balm.");
    if (Health.PlayerHealth.Current >= Health.PlayerHealth.Maximum)
        return LOCTEXT("UseFullHealth", "Your health is full. Balm is preserved.");
    return FText::GetEmpty();
}

FText SJadeboundProgressionPanel::SplitRestrictionForSlot(int32 SlotIndex) const
{
    const AJadeboundProgressionActor* Actor = ProgressionActor.Get();
    if (!Actor || !bReady || !Actor->IsProgressionReady())
        return LOCTEXT("SplitNotReady", "Your pack is not ready. Read the note below.");
    if (Actor->GetRevivePresentation().bWaiting)
        return LOCTEXT("SplitLivingHero", "Revive before splitting items.");
    if (DisplayState.Revision < 0 || DisplayState.ExactRevision >= static_cast<uint64>(MAX_int64) ||
        DisplayState.ExactRevision != static_cast<uint64>(DisplayState.Revision))
        return LOCTEXT("SplitRevisionLimit", "Your pack cannot accept another item action. Items are preserved.");
    const FJadeboundInventorySlot* Slot = DisplayState.Slots.IsValidIndex(SlotIndex) ? &DisplayState.Slots[SlotIndex] : nullptr;
    const FJadeboundItemView* Item = Slot ? FindDefinition(Slot->ItemId) : nullptr;
    if (!Item || !Slot || Slot->Count <= 0)
        return LOCTEXT("SplitSelectStack", "Select an occupied stack in your pack.");
    if (Item->StackLimit <= 1 || Slot->Count <= 1 || Slot->Count > Item->StackLimit)
        return LOCTEXT("SplitNeedsTwo", "Select a stack of two or more stackable items.");
    if (DisplayState.Slots.Num() != DisplaySlotCount ||
        !DisplayState.Slots.ContainsByPredicate([](const FJadeboundInventorySlot& Cell) { return Cell.ItemId == 0 && Cell.Count == 0; }))
        return LOCTEXT("SplitNeedsSpace", "Make one empty pack slot before splitting.");
    return FText::GetEmpty();
}

FReply SJadeboundProgressionPanel::ExecuteAction(EAction Action, int32 Argument, int64 ExpectedRevision,
    TSharedPtr<const uint8> RenderToken, uint64 ExpectedLifeGeneration)
{
    return ExecuteActionWithQuantity(Action, Argument, ExpectedRevision, 1, 0, RenderToken, ExpectedLifeGeneration);
}

TSharedRef<SWidget> SJadeboundProgressionPanel::ActionButton(const FText& Label,
    const FText& Tooltip, bool bEnabled, EAction Action, int32 Argument, int32 Quantity, int32 ExpectedItemId)
{
    const int64 ExpectedRevision = DisplayState.Revision;
    const uint64 ExpectedLifeGeneration = ProgressionActor.IsValid() ? ProgressionActor->GetItemUseLifeGeneration() : 0;
    const bool bCanClick = bEnabled && ProgressionActor.IsValid()
        && (Action == EAction::Close || !bAwaitingAction);
    const bool bPrimary = Action == EAction::Buy || Action == EAction::Equip || Action == EAction::Save || Action == EAction::Use;
    const FButtonStyle* Style = (Action == EAction::Buy || Action == EAction::Sell) && MerchantArtResources.IsValid()
        ? &MerchantStyle : bPrimary ? &ActionStyle : &SecondaryStyle;
    const FJadeboundInventorySlot* Selection = GetSelectedSlot();
    const int32 TooltipItem = Action == EAction::Buy ? Argument
        : (Action == EAction::Equip || Action == EAction::Sell || Action == EAction::Use || Action == EAction::Split) && Selection ? Selection->ItemId
        : Action == EAction::Unequip ? DisplayState.EquippedWeapon : 0;
    TSharedRef<SButton> Button = SNew(SButton)
        .Tag(FName(*FString::Printf(TEXT("JadeboundAction%d_%d_%d"), static_cast<int32>(Action), Argument, Quantity)))
        .ButtonStyle(Style)
        .IsEnabled(bCanClick).ContentPadding(FMargin(12.f,Action == EAction::Split ? 3.f : 8.f)).HAlign(HAlign_Center)
        .ToolTip(PackTooltip(Tooltip,Label,TooltipItem))
        .OnClicked(this, &SJadeboundProgressionPanel::ExecuteActionWithQuantity, Action, Argument, ExpectedRevision, Quantity, ExpectedItemId, TradeRenderToken, ExpectedLifeGeneration)
        [Text(Label,14.f,bCanClick ? Ink : FLinearColor(.65f,.69f,.61f,1.f),true)];
    FocusTargets.Add(FName(*FString::Printf(TEXT("Action-%d-%d-%d"),static_cast<int32>(Action),Argument,Quantity)),Button);
    return WithFocusRing(Button);
}

FReply SJadeboundProgressionPanel::SelectSlot(int32 Index)
{
    if (Index >= 0 && Index < DisplaySlotCount) { SelectedSlot = Index; RefreshFromActor(true); }
    return FReply::Handled();
}

FReply SJadeboundProgressionPanel::HandleInventoryMouseDown(const FGeometry&, const FPointerEvent& Event,
    int32 SlotIndex, int32 RenderedItemId, int64 RenderedRevision, TSharedPtr<const uint8> RenderToken, uint64 RenderedLifeGeneration)
{
    if (Event.GetEffectingButton() != EKeys::RightMouseButton) return FReply::Unhandled();
    // Consume the gesture even when it cannot transact; it never reaches the world.
    if (Event.IsTouchEvent() || Event.IsControlDown() || Event.IsShiftDown() || Event.IsAltDown() || Event.IsCommandDown())
        return FReply::Handled();
    AJadeboundProgressionActor* Actor = ProgressionActor.Get();
    if (!Actor || !Actor->IsMenuOpen() || !bReady || !Actor->IsProgressionReady() || bAwaitingAction)
        return FReply::Handled();
    // Preserve the widget's original intent. Refreshing may not retarget a later item.
    if (!RenderToken || RenderToken != TradeRenderToken || RenderedRevision < 0 || DisplayState.Revision != RenderedRevision ||
        DisplayState.ExactRevision != static_cast<uint64>(RenderedRevision) ||
        Actor->GetRevision() != RenderedRevision || Actor->GetProgressionState().ExactRevision != static_cast<uint64>(RenderedRevision) ||
        !DisplayState.Slots.IsValidIndex(SlotIndex) || DisplayState.Slots[SlotIndex].ItemId != RenderedItemId)
    { RefreshFromActor(true); return FReply::Handled(); }
    const FJadeboundItemView* Item = FindDefinition(RenderedItemId);
    if (Item && Item->HealingPercent > 0)
    {
        if (!UseRestrictionForSlot(SlotIndex).IsEmpty()) return SelectSlot(SlotIndex);
        SelectedSlot = SlotIndex;
        return ExecuteAction(EAction::Use, SlotIndex, RenderedRevision, RenderToken, RenderedLifeGeneration);
    }
    // Materials, parcel, empty or unavailable items remain inspection only.
    if (!Item || !Item->bWeapon || !EquipRestrictionForSlot(SlotIndex).IsEmpty())
        return SelectSlot(SlotIndex);
    SelectedSlot = SlotIndex;
    return ExecuteAction(EAction::Equip, SlotIndex, RenderedRevision, RenderToken);
}

FReply SJadeboundProgressionPanel::HandleMerchantMouseDown(const FGeometry&, const FPointerEvent& Event,
    int32 RenderedItemId, int64 RenderedRevision, int64 RenderedBuyPrice, TSharedPtr<const uint8> RenderToken)
{
    if (Event.GetEffectingButton() != EKeys::RightMouseButton) return FReply::Unhandled();
    // One shelf gesture sends one intent; invalid shortcuts never reach the world.
    if (Event.IsTouchEvent() || Event.IsControlDown() || Event.IsShiftDown() || Event.IsAltDown() || Event.IsCommandDown())
        return FReply::Handled();
    AJadeboundProgressionActor* Actor = ProgressionActor.Get();
    // Remote shelf gestures remain unqualified. Keep this shortcut local while
    // the explicit Buy button carries a captured revision through the server RPC.
    if (!Actor || Actor->IsActorBeingDestroyed() || Actor->IsDevelopmentModel() ||
        Actor->GetNetMode() != NM_Standalone || !Actor->HasAuthority() ||
        !Actor->IsMenuOpen() || !bReady || !Actor->IsProgressionReady() || bAwaitingAction ||
        !bMerchantInRange || !Actor->IsMerchantInRange())
        return FReply::Handled();
    const FJadeboundProgressionState LiveState = Actor->GetProgressionState();
    if (!RenderToken || RenderToken != TradeRenderToken || RenderedRevision < 0 || DisplayState.Revision != RenderedRevision ||
        DisplayState.ExactRevision != static_cast<uint64>(RenderedRevision) ||
        LiveState.Revision != RenderedRevision || LiveState.ExactRevision != static_cast<uint64>(RenderedRevision))
    { RefreshFromActor(true); return FReply::Handled(); }
    const FJadeboundItemView* Item = FindDefinition(RenderedItemId);
    const TArray<FJadeboundItemView> LiveDefinitions = Actor->GetItemDefinitions();
    const FJadeboundItemView* LiveItem = LiveDefinitions.FindByPredicate(
        [RenderedItemId](const FJadeboundItemView& Candidate) { return Candidate.ItemId == RenderedItemId; });
    if (!Item || !LiveItem || RenderedItemId <= 0 || RenderedBuyPrice < 0 ||
        !Item->bPurchasable || !LiveItem->bPurchasable || Item->BuyPrice != RenderedBuyPrice ||
        LiveItem->BuyPrice != RenderedBuyPrice || LiveItem->StackLimit != Item->StackLimit)
    { RefreshFromActor(true); return FReply::Handled(); }
    if (!BuyRestriction(*Item).IsEmpty()) return FReply::Handled();
    // BuyItem revalidates authority, living possession, range and server catalog,
    // then atomically checks cost/capacity and saves. The UI never grants items.
    return ExecuteAction(EAction::Buy, RenderedItemId, RenderedRevision, RenderToken);
}

FReply SJadeboundProgressionPanel::HandleMerchantDoubleClick(const FGeometry&, const FPointerEvent& Event)
{
    // A recognized second RMB click is consumed even after a successful rebuild.
    return Event.GetEffectingButton() == EKeys::RightMouseButton ? FReply::Handled() : FReply::Unhandled();
}

FReply SJadeboundProgressionPanel::HandleInventoryDoubleClick(const FGeometry&, const FPointerEvent& Event)
{
    // The second RMB event must not equip the previous weapon back after a swap.
    return Event.GetEffectingButton() == EKeys::RightMouseButton ? FReply::Handled() : FReply::Unhandled();
}

FReply SJadeboundProgressionPanel::HandleEquippedDoubleClick(const FGeometry&, const FPointerEvent& Event,
    int32 RenderedWeapon, int64 RenderedRevision, TSharedPtr<const uint8> RenderToken)
{
    if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
    if (Event.IsTouchEvent() || Event.IsControlDown() || Event.IsShiftDown() || Event.IsAltDown() || Event.IsCommandDown())
        return FReply::Handled();
    AJadeboundProgressionActor* Actor = ProgressionActor.Get();
    // Remote double-click gestures remain unqualified. Keep this shortcut local;
    // the explicit Unequip button carries its captured revision to the server.
    if (!Actor || Actor->IsDevelopmentModel() || Actor->GetNetMode() != NM_Standalone || !Actor->HasAuthority() ||
        !Actor->IsMenuOpen() || !bReady || !Actor->IsProgressionReady() || bAwaitingAction)
        return FReply::Handled();
    if (!RenderToken || RenderToken != TradeRenderToken || RenderedRevision < 0 || RenderedWeapon == 0 || DisplayState.Revision != RenderedRevision ||
        DisplayState.ExactRevision != static_cast<uint64>(RenderedRevision) ||
        Actor->GetRevision() != RenderedRevision || Actor->GetProgressionState().ExactRevision != static_cast<uint64>(RenderedRevision) ||
        DisplayState.EquippedWeapon != RenderedWeapon || Actor->GetProgressionState().EquippedWeapon != RenderedWeapon)
    { RefreshFromActor(true); return FReply::Handled(); }
    if (!UnequipRestriction().IsEmpty()) return FReply::Handled();
    return ExecuteAction(EAction::Unequip, INDEX_NONE, RenderedRevision, RenderToken);
}

TSharedRef<SWidget> SJadeboundProgressionPanel::BuildEquipment()
{
    const FJadeboundItemView* Item = FindDefinition(DisplayState.EquippedWeapon);
    const bool bEquipped = Item && DisplayState.EquippedWeapon != 0;
    const FJadeboundInventorySlot* SelectedPackSlot = GetSelectedSlot();
    const FJadeboundItemView* SelectedWeapon = SelectedPackSlot ? FindDefinition(SelectedPackSlot->ItemId) : nullptr;
    const bool bSelectedWeapon = SelectedPackSlot && SelectedWeapon && SelectedWeapon->bWeapon && SelectedPackSlot->Count > 0;
    // Inspection uses this rendered pack snapshot only; it never equips or returns a weapon.
    // 252-wide Gear column minus two 8+8 paddings leaves 220 Slate units.
    TSharedRef<SWidget> SelectedInspection = SNew(STextBlock)
        .Text(LOCTEXT("InspectPackWeaponHint","Select a pack weapon to inspect."))
        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),13.f)).ColorAndOpacity(Muted)
        .WrapTextAt(220.f);
    if (bSelectedWeapon)
    {
        SelectedInspection = SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0.f)
            .ToolTip(PackTooltip(ItemTooltip(*SelectedWeapon),SelectedWeapon->Name,SelectedWeapon->ItemId))
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,2.f)
                [Text(LOCTEXT("SelectedPackWeaponHeading","SELECTED IN PACK"),10.f,Gold,true)]
                + SVerticalBox::Slot().AutoHeight()
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,8.f,0.f)
                    [Art(SelectedWeapon->ItemId,56.f)]
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                    [SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [SNew(STextBlock).Text(SelectedWeapon->Name)
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Medium"),13.f)).ColorAndOpacity(Ink).AutoWrapText(true)]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.f,2.f,0.f,0.f)
                        [Text(FText::Format(LOCTEXT("SelectedPackWeaponStack","Stack {0} / {1}"),
                            FText::AsNumber(SelectedPackSlot->Count),FText::AsNumber(SelectedWeapon->StackLimit)),11.f,Muted)]]]];
    }
    const FText Why = UnequipRestriction();
    const AJadeboundProgressionActor* Actor = ProgressionActor.Get();
    const bool bLocalShortcut = Actor && !Actor->IsDevelopmentModel() && Actor->GetNetMode() == NM_Standalone && Actor->HasAuthority();
    const FText ShortcutHelp = bLocalShortcut
        ? LOCTEXT("EquipmentDoubleClickHelp", "Double-click to return this weapon to your pack.")
        : LOCTEXT("EquipmentButtonHelp", "Use Return to pack to unequip this weapon.");
    return SNew(SJadeboundArtBorder).Frame(false).Fallback(&CardBrush).Padding(8.f)
    [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Text(LOCTEXT("EquipmentHeading","EQUIPPED WEAPON"),11.f,Gold,true,80)]
        + SVerticalBox::Slot().FillHeight(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SBox).WidthOverride(96.f).HeightOverride(96.f)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0.f)
                .ToolTip(PackTooltip(WithRestriction(ShortcutHelp,Why),bEquipped ? Item->Name : LOCTEXT("NoWeapon","Unarmed"),DisplayState.EquippedWeapon))
                .OnMouseDoubleClick(this,&SJadeboundProgressionPanel::HandleEquippedDoubleClick,DisplayState.EquippedWeapon,DisplayState.Revision,TradeRenderToken)
                [SNew(SJadeboundArtBorder).Frame(false).QuietTrim(true).Padding(0.f)[Art(bEquipped ? Item->ItemId : 0,96.f)]]]]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,3.f)
        [SNew(STextBlock).Text(bEquipped ? Item->Name : LOCTEXT("NoWeapon","Unarmed"))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Medium"),18.f)).ColorAndOpacity(Ink).AutoWrapText(true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,2.f,0.f,6.f)
        [SelectedInspection]
        + SVerticalBox::Slot().AutoHeight()
        [ActionButton(LOCTEXT("Unequip","Return to pack"),WithRestriction(LOCTEXT("UnequipHelp","Unequip your weapon and return it to the pack."),Why),Why.IsEmpty(),EAction::Unequip)]];
}

TSharedRef<SWidget> SJadeboundProgressionPanel::BuildInventory()
{
    TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(2.f);
    int32 Occupied = 0;
    for (int32 Index = 0; Index < DisplaySlotCount; ++Index)
    {
        const FJadeboundInventorySlot* Slot = DisplayState.Slots.IsValidIndex(Index) ? &DisplayState.Slots[Index] : nullptr;
        const FJadeboundItemView* Item = Slot ? FindDefinition(Slot->ItemId) : nullptr;
        const bool bOccupied = Slot && Slot->ItemId != 0 && Slot->Count > 0;
        Occupied += bOccupied ? 1 : 0;
        const FText Details = Item ? ItemTooltip(*Item) : bOccupied
            ? LOCTEXT("UnknownItem","This item is unavailable. It cannot be traded or equipped.")
            : LOCTEXT("EmptySlot","Empty pack slot. This space is available for another item.");
        const FText MouseHint = !bOccupied ? FText::GetEmpty() : Item && Item->bWeapon
            ? LOCTEXT("PackWeaponMouseHelp","Left-click to inspect. Right-click to equip.")
            : Item && Item->HealingPercent > 0
                ? (ProgressionActor.IsValid() && ProgressionActor->IsDevelopmentModel()
                    ? LOCTEXT("NativePackBalmMouseHelp","Left-click to inspect. Right-click to use one.")
                    : LOCTEXT("PackBalmMouseHelpCompact","Click to inspect. Drag onto an F-key below to assign."))
                : LOCTEXT("PackItemMouseHelp","Click to inspect. This item has no use action.");
        const TSharedRef<SWidget> Content = SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)[Text(FText::FromString(FString::Printf(TEXT("%02d"),Index+1)),10.f,SelectedSlot == Index ? Gold : bOccupied ? Muted : QuietIndex)]
                + SHorizontalBox::Slot().AutoWidth()[Text(bOccupied ? FText::Format(LOCTEXT("SlotQuantity","x{0}"),FText::AsNumber(Slot->Count)) : FText::GetEmpty(),12.f,bOccupied ? Ink : Muted,true)]]
            + SVerticalBox::Slot().FillHeight(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
            [Art(bOccupied ? Slot->ItemId : 0,32.f)]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f,1.f,0.f,0.f)
            [SNew(SBox).HeightOverride(1.f)
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).Padding(0.f)
                    .BorderBackgroundColor(bOccupied ? ItemAccent(Slot->ItemId) : FLinearColor::Transparent)]];
        const FText SlotHeading=Item ? Item->Name : FText::Format(LOCTEXT("PackSlotHeading","Pack slot {0}"),FText::AsNumber(Index+1));
        const FText SlotDetails=bOccupied ? FText::Format(LOCTEXT("PackSlotCompleteDetails","{0}\n{1}\nPack slot {2}"),Details,MouseHint,FText::AsNumber(Index+1)) : Details;
        const auto Tooltip=PackTooltip(SlotDetails,SlotHeading,bOccupied ? Slot->ItemId : 0);
        TSharedPtr<SWidget> Button;
        if(bOccupied && Item && Item->ItemId==7 && Item->HealingPercent==25 &&
            ProgressionActor.IsValid() && !ProgressionActor->IsDevelopmentModel())
        {
            Button=SNew(SJadeboundBalmDragTile).ProgressionActor(ProgressionActor).PanelToken(SharedThis(this))
                .InventorySlot(Index).InventoryRevision(DisplayState.Revision)
                .Background(SelectedSlot==Index ? &SelectedSlotStyle.Normal : &SlotStyle.Normal)
                .ToolTip(Tooltip).OnInspect(this,&SJadeboundProgressionPanel::SelectSlot,Index)[Content];
        }
        else
        {
            Button=SNew(SButton).ButtonStyle(SelectedSlot==Index ? &SelectedSlotStyle : bOccupied ? &SlotStyle : &EmptySlotStyle)
                .ContentPadding(FMargin(4.f,3.f)).ToolTip(Tooltip)
                .OnClicked(this,&SJadeboundProgressionPanel::SelectSlot,Index)[Content];
        }
        Button->SetTag(FName(*FString::Printf(TEXT("JadeboundSlot%d"), Index)));
        FocusTargets.Add(FName(*FString::Printf(TEXT("Slot-%d"),Index)),Button);
        Grid->AddSlot(Index % 3,Index / 3)[SNew(SBox).WidthOverride(60.f).HeightOverride(58.f)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0.f)
                .OnMouseButtonDown(this,&SJadeboundProgressionPanel::HandleInventoryMouseDown,Index,Slot ? Slot->ItemId : 0,DisplayState.Revision,TradeRenderToken,
                    ProgressionActor.IsValid() ? ProgressionActor->GetItemUseLifeGeneration() : uint64(0))
                .OnMouseDoubleClick(this,&SJadeboundProgressionPanel::HandleInventoryDoubleClick)
                [WithFocusRing(Button.ToSharedRef())]]];
    }
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(3.f,0.f,3.f,4.f)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f)[Text(LOCTEXT("InventoryTitle","Your inventory"),14.f,Ink,true)]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [Badge(FText::Format(LOCTEXT("PackCapacity","{0} / 12"),FText::AsNumber(Occupied)),Gold)]]
        + SVerticalBox::Slot().FillHeight(1.f)
        [SAssignNew(InventoryScroll,SScrollBox).ScrollBarThickness(FVector2f(5.f,5.f)).ConsumeMouseWheel(EConsumeMouseWheel::Always)
            .ScrollWhenFocusChanges(EScrollWhenFocusChanges::NoScroll)
            + SScrollBox::Slot()[Grid]];
}

TSharedRef<SWidget> SJadeboundProgressionPanel::BuildHotbarAssignmentRow()
{
    TSharedRef<SUniformGridPanel> Grid=SNew(SUniformGridPanel).SlotPadding(2.f);
    const TWeakPtr<SWidget> Token=SharedThis(this);
    const TSharedRef<FJadeboundAssignmentDisplaySnapshot> Display=MakeShared<FJadeboundAssignmentDisplaySnapshot>(ProgressionActor);
    for(int32 Slot=0;Slot<10;++Slot)
        Grid->AddSlot(Slot,0)[SNew(SBox).WidthOverride(48.f).HeightOverride(48.f)
            [SNew(SJadeboundSkillAssignmentTarget).ProgressionActor(ProgressionActor).PanelToken(Token).SlotIndex(Slot).DisplaySnapshot(Display)]];
    return Grid;
}

FReply SJadeboundProgressionPanel::ShowPackPage(bool Skills)
{
    if (ProgressionActor.IsValid()) ProgressionActor->MarkSkillHudGesture();
    if (FSlateApplication::IsInitialized() && FSlateApplication::Get().IsDragDropping()) return FReply::Handled();
    bSkillsPage=Skills;RebuildContents();return FReply::Handled();
}
FReply SJadeboundProgressionPanel::ShowContextPage(EContextPage Page)
{
    if (ProgressionActor.IsValid()) ProgressionActor->MarkSkillHudGesture();
    if (FSlateApplication::IsInitialized() && FSlateApplication::Get().IsDragDropping()) return FReply::Handled();
    ContextPage=Page;bSkillsPage=false;RebuildContents();return FReply::Handled();
}

TSharedRef<SWidget> SJadeboundProgressionPanel::BuildSkills()
{
    AJadeboundProgressionActor* Actor=ProgressionActor.Get();
    if (!Actor || Actor->IsDevelopmentModel() || Actor->GetNetMode()!=NM_Standalone || !Actor->HasAuthority())
        return Text(LOCTEXT("LocalSkillsOnly","Skill assignment is available for this local standalone character."),16.f,Muted);
    const TWeakPtr<SWidget> Token=SharedThis(this);
    return SNew(SScrollBox).ScrollBarThickness(FVector2f(5.f,5.f)).ConsumeMouseWheel(EConsumeMouseWheel::Always)
        + SScrollBox::Slot()
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(3.f,0.f,3.f,5.f)[Text(LOCTEXT("SkillsTitle","Your glaive skills"),15.f,Ink,true)]
            + SVerticalBox::Slot().AutoHeight().Padding(3.f,0.f,3.f,8.f)
            [SNew(STextBlock).Text(LOCTEXT("SkillsInstructionsCompact","Drag to F1-F10; drop never casts.\nRMB clears a slot."))
                .ToolTip(PackTooltip(LOCTEXT("SkillsInstructions","Drag a skill onto F1-F10 below. This moves its assignment. Right-click a slot to clear; dropping never casts."),LOCTEXT("SkillGuideTitle","ASSIGN SKILLS"),0,240.f))
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),13.f)).ColorAndOpacity(Muted).WrapTextAt(210.f).AutoWrapText(false)]
            + SVerticalBox::Slot().AutoHeight().Padding(3.f,0.f,3.f,8.f)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,6.f)
                [SNew(SJadeboundSkillPaletteTile).ProgressionActor(ProgressionActor).PanelToken(Token).SkillIdentity(0)]
                + SVerticalBox::Slot().AutoHeight()
                [SNew(SJadeboundSkillPaletteTile).ProgressionActor(ProgressionActor).PanelToken(Token).SkillIdentity(1)]]
            + SVerticalBox::Slot().AutoHeight().Padding(3.f,6.f)
            [SNew(STextBlock).Text_Lambda([Weak=ProgressionActor]() {return Weak.IsValid() ? Weak->GetSkillPresentation().HotbarFeedback : FText::GetEmpty();})
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14.f)).ColorAndOpacity_Lambda([Weak=ProgressionActor]()
                    {return Weak.IsValid() && Weak->GetSkillPresentation().bHotbarConfigIssue ? Warning : Muted;}).AutoWrapText(true)]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(3.f,0.f)
            [SNew(SButton).ButtonStyle(&SecondaryStyle).ContentPadding(FMargin(12.f,8.f))
                .IsEnabled_Lambda([Weak=ProgressionActor]() {return Weak.IsValid() && Weak->CanConfigureSkillHotbar();})
                .OnClicked_Lambda([Weak=ProgressionActor]() {if(Weak.IsValid()){Weak->MarkSkillHudGesture();Weak->ResetSkillHotbar(Weak->GetSkillPresentation().HotbarRevision);}return FReply::Handled();})
                [Text(LOCTEXT("ResetSkillDefaults","Reset F1 Guard / F2 Current"),14.f,Ink)]]
            + SVerticalBox::Slot().AutoHeight().Padding(3.f,8.f)
            [Text(LOCTEXT("LocalControlsNote","Controls save locally; your inventory and quest save is unchanged."),12.f,Muted)]];
}

TSharedRef<SWidget> SJadeboundProgressionPanel::BuildSelection()
{
    const FJadeboundInventorySlot* Slot = GetSelectedSlot();
    const FJadeboundItemView* Item = Slot ? FindDefinition(Slot->ItemId) : nullptr;
    const bool bOccupied = Item && Slot && Slot->Count > 0;
    const bool bUse = bOccupied && Item->HealingPercent > 0;
    const bool bPrimary = bOccupied && (bUse || Item->bWeapon);
    const bool bStackSale = bOccupied && Item->bSellable && Slot->Count > 1;
    RenderedSelectedUseRestriction = bUse ? UseRestrictionForSlot(SelectedSlot) : FText::GetEmpty();
    RenderedSelectedSplitRestriction = SplitRestrictionForSlot(SelectedSlot);
    const FText EquipWhy = bUse ? RenderedSelectedUseRestriction : EquipRestriction();
    const FText SellWhy = SellRestriction();
    const FText StackWhy = bStackSale ? SellRestriction(Slot->Count) : FText::GetEmpty();
    int64 Total = 0;
    const bool bQuoted = bOccupied && Item->bSellable &&
        Jadebound::Presentation::TryStackSaleTotal(Slot->Count, Item->SellPrice, DisplayState.Coins, Total);
    const FText Value = bQuoted ? bStackSale
        ? FText::Format(LOCTEXT("StackSaleValue", "{0} jade coins each / {1} jade coins for this stack"), FText::AsNumber(Item->SellPrice), FText::AsNumber(Total))
        : FText::Format(LOCTEXT("SingleSaleValue", "Sale value: {0} jade coins"), FText::AsNumber(Item->SellPrice))
        : bOccupied && !Item->bSellable ? LOCTEXT("NoSaleValue", "This item cannot be sold.") : FText::GetEmpty();
    const FText MainWhy = bPrimary ? EquipWhy : SellWhy;
    const FText ActionReason = MainWhy.IsEmpty() ? FText::GetEmpty() : bPrimary
        ? FText::Format(bUse ? LOCTEXT("UseReason", "Use: {0}") : LOCTEXT("EquipReason", "Equip: {0}"), MainWhy) : MainWhy;
    const TSharedRef<SWidget> Primary = bPrimary ? ActionButton(bUse ? LOCTEXT("UseBalm", "Use 1") : LOCTEXT("Equip", "Equip"),
        WithRestriction(bUse ? LOCTEXT("UseBalmHelp", "Recover 25% of maximum health, capped at full. One balm is consumed.")
            : LOCTEXT("EquipHelp", "Equip this weapon. Your current weapon returns to your pack."), EquipWhy),
        EquipWhy.IsEmpty(), bUse ? EAction::Use : EAction::Equip, SelectedSlot) : SNullWidget::NullWidget;
    const TSharedRef<SWidget> Bulk = bStackSale ? ActionButton(
        FText::Format(LOCTEXT("SellStackLabel", "Sell stack ({0})"), FText::AsNumber(Slot->Count)),
        WithRestriction(bQuoted ? FText::Format(LOCTEXT("SellStackHelp", "Sell all {0} displayed items for {1} jade coins in one saved transaction."), FText::AsNumber(Slot->Count), FText::AsNumber(Total)) : StackWhy, StackWhy),
        StackWhy.IsEmpty(), EAction::Sell, SelectedSlot, Slot->Count, Item->ItemId) : SNullWidget::NullWidget;
    return SNew(SJadeboundSectionBorder).Padding(FMargin(4.f,3.f))
        .ToolTip(PackTooltip(bOccupied ? ItemTooltip(*Item) : LOCTEXT("SelectDetails", "Select a filled slot to inspect or equip its item."),
            bOccupied ? Item->Name : LOCTEXT("SelectionHeading", "SELECTED ITEM"), bOccupied ? Item->ItemId : 0))
    [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [SNew(STextBlock).Text(bOccupied ? Item->Name : LOCTEXT("SelectHint", "Choose an item"))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Medium"),13.f)).ColorAndOpacity(Ink).AutoWrapText(true)]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f,0.f,4.f,0.f)
            [Text(bOccupied ? FText::Format(LOCTEXT("SelectedStack", "Stack {0} / {1}"), FText::AsNumber(Slot->Count), FText::AsNumber(Item->StackLimit))
                : LOCTEXT("CompactInspectHint", "Select a pack slot to inspect"),11.f,Muted)]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ActionButton(LOCTEXT("SplitOne", "Split 1"), WithRestriction(
                LOCTEXT("SplitOneHelp", "Move one item from this stack into the first empty pack slot. No items are created; at least one remains in this stack."),
                RenderedSelectedSplitRestriction), RenderedSelectedSplitRestriction.IsEmpty(), EAction::Split, SelectedSlot)]]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(STextBlock).Text(Value).Visibility(Value.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11.f)).ColorAndOpacity(Gold).AutoWrapText(true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,2.f,0.f,0.f)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(bPrimary ? 1.f : 0.f).Padding(0.f,0.f,bPrimary ? 4.f : 0.f,0.f)[Primary]
            + SHorizontalBox::Slot().FillWidth(1.f)
            [ActionButton(LOCTEXT("SellOne", "Sell 1"), WithRestriction(LOCTEXT("SellHelp", "Sell one selected item at the shown coin price."), SellWhy), SellWhy.IsEmpty(), EAction::Sell, SelectedSlot)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,bStackSale ? 2.f : 0.f,0.f,0.f)[Bulk]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,2.f,0.f,0.f)
        [SNew(STextBlock).Text(ActionReason).Visibility(ActionReason.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11.f)).ColorAndOpacity(Warning).AutoWrapText(true)]];
}

TSharedRef<SWidget> SJadeboundProgressionPanel::BuildMerchant()
{
    SAssignNew(CatalogScroll,SScrollBox).ScrollBarVisibility(EVisibility::Visible).ScrollBarThickness(FVector2f(5.f,5.f));
    TSharedRef<SVerticalBox> Shelf=SNew(SVerticalBox);
    for (const FJadeboundItemView& Item : Definitions)
    {
        if (!Item.bPurchasable) continue;
        const FText Why = BuyRestriction(Item);
        // Range/readiness is explained once above the shelf; item-specific
        // restrictions stay visible alongside the price and disabled action.
        const bool bShowPurchaseRestriction = bReady && bMerchantInRange && !Why.IsEmpty();
        const AJadeboundProgressionActor* Actor = ProgressionActor.Get();
        const bool bLocalShortcut = Actor && !Actor->IsDevelopmentModel() &&
            Actor->GetNetMode() == NM_Standalone && Actor->HasAuthority();
        const FText MouseHelp = bLocalShortcut
            ? LOCTEXT("MerchantRightClickHelp", "Right-click this item to buy one at the shown price.")
            : LOCTEXT("MerchantButtonHelp", "Use Buy 1 to purchase this item.");
        const FText ShelfTooltip = WithRestriction(FText::Format(
            LOCTEXT("MerchantShelfDetails", "{0}\n{1}"), ItemTooltip(Item), MouseHelp), Why);
        // Preserve the normal row minimum; restriction text may wrap and grow.
        Shelf->AddSlot().AutoHeight().Padding(0.f,0.f,0.f,3.f)
        [SNew(SBox).MinDesiredHeight(72.f)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0.f)
                .ToolTip(PackTooltip(ShelfTooltip,Item.Name,Item.ItemId))
                .OnMouseButtonDown(this,&SJadeboundProgressionPanel::HandleMerchantMouseDown,Item.ItemId,DisplayState.Revision,Item.BuyPrice,TradeRenderToken)
                .OnMouseDoubleClick(this,&SJadeboundProgressionPanel::HandleMerchantDoubleClick)
                [SNew(SJadeboundArtBorder).Frame(false).QuietTrim(true).Padding(FMargin(4.f,3.f))
                    [SNew(SVerticalBox)
                        + SVerticalBox::Slot().FillHeight(1.f)
                        [SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,4.f,0.f)[Art(Item.ItemId,36.f)]
                            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f,0.f,4.f,0.f)
                            [SNew(STextBlock).Text(Item.Name).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Medium"),11.f)).ColorAndOpacity(Ink).WrapTextAt(80.f).AutoWrapText(false)]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [ActionButton(LOCTEXT("BuyOne","Buy 1"),WithRestriction(ItemTooltip(Item),Why),Why.IsEmpty(),EAction::Buy,Item.ItemId)]]
                        + SVerticalBox::Slot().AutoHeight()
                        [Text(FText::Format(LOCTEXT("MerchantPrice","{0} jade coins"),FText::AsNumber(Item.BuyPrice)),11.f,Gold)]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.f,2.f,0.f,0.f)
                        [SNew(STextBlock).Text(Why)
                            .Visibility(bShowPurchaseRestriction ? EVisibility::Visible : EVisibility::Collapsed)
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12.f))
                            .ColorAndOpacity(Warning).AutoWrapText(true)]]]]];
    }
    CatalogScroll->AddSlot()[Shelf];
    const FText Range = !bReady ? LOCTEXT("ShopUnavailable","Your pack is not ready for trading.")
        : bMerchantInRange ? LOCTEXT("ShopOpen","Merchant nearby - trading open") : LOCTEXT("ShopRange","Out of range - approach the merchant");
    return SNew(SJadeboundSectionBorder).Padding(6.f)
    [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Text(LOCTEXT("MerchantTitle","Merchant's wares"),15.f,Ink,true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,2.f,0.f,5.f)
        [SNew(STextBlock).Text(Range).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12.f))
            .ColorAndOpacity(bReady && bMerchantInRange ? Good : Warning).AutoWrapText(true)]
        + SVerticalBox::Slot().FillHeight(1.f)[CatalogScroll.ToSharedRef()]];
}

TSharedRef<SWidget> SJadeboundProgressionPanel::BuildQuest()
{
    const AJadeboundProgressionActor* Actor = ProgressionActor.Get();
    const bool bComplete = DisplayState.QuestState == 4;
    return SNew(SJadeboundSectionBorder).Padding(10.f)
        .ToolTip(PackTooltip(QuestText,LOCTEXT("QuestTitle","A Stolen Spark")))
    [SNew(SScrollBox).ScrollBarThickness(FVector2f(5.f,5.f)).ConsumeMouseWheel(EConsumeMouseWheel::Always)
        + SScrollBox::Slot()
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)[Text(LOCTEXT("QuestEyebrow","QUEST"),11.f,Gold,true,60)]
                + SHorizontalBox::Slot().AutoWidth()[Text(bComplete ? LOCTEXT("CompleteBadge","COMPLETE") : DisplayState.QuestState == 0 ? LOCTEXT("OfferedBadge","AVAILABLE") : LOCTEXT("ActiveBadge","ACTIVE"),12.f,bComplete ? Good : Muted,true)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f,6.f,0.f,5.f)[Text(LOCTEXT("QuestTitle","A Stolen Spark"),19.f,Ink,true)]
            + SVerticalBox::Slot().AutoHeight()
            [SNew(STextBlock).Text(Actor ? Actor->GetQuestObjectiveText() : LOCTEXT("TaskUnavailable", "Your quest details are unavailable.")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14.f))
                .ColorAndOpacity(Muted).AutoWrapText(true)]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f,8.f,0.f,0.f)
            [SNew(STextBlock).Text(bComplete ? LOCTEXT("RewardReceived","Reward received - 5 coins + jade shard") : LOCTEXT("QuestReward","Reward - 5 coins + jade shard"))
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),13.f)).ColorAndOpacity(bComplete ? Good : Gold).AutoWrapText(true)]]];
}

void SJadeboundProgressionPanel::RebuildContents()
{
    // Retained buttons keep the previous token alive; no address reuse or counter wrap.
    TradeRenderToken = MakeShared<const uint8>(0);
    const bool bHadFocus = HasKeyboardFocus() || HasFocusedDescendants();
    FName FocusKey = NAME_None;
    if (bHadFocus && FSlateApplication::IsInitialized())
    {
        const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
        for (const auto& Pair : FocusTargets) if (Pair.Value.Pin() == Focused) { FocusKey = Pair.Key; break; }
    }
    const float Offset = CatalogScroll.IsValid() ? CatalogScroll->GetScrollOffset() : RetainedCatalogOffset;
    RetainedCatalogOffset=Offset;
    // Retain the visible pack row through selection and authoritative refreshes.
    const float InventoryOffset = InventoryScroll.IsValid() ? InventoryScroll->GetScrollOffset() : RetainedInventoryOffset;
    RetainedInventoryOffset=InventoryOffset;
    CatalogScroll.Reset();
    InventoryScroll.Reset();
    FocusTargets.Reset();
    const AJadeboundProgressionActor* Actor = ProgressionActor.Get();
    // Saving and retries belong to authority. A persistent warning takes
    // priority even while a gameplay acknowledgment is still arriving.
    const bool bPersistenceFailure = PersistencePresentation.FailureKind != EJadeboundPersistenceFailureKind::None;
    const FJadeboundActionReceipt Receipt = Actor ? Actor->GetLastActionReceipt() : FJadeboundActionReceipt();
    const bool bIssue = bPersistenceFailure || (!bAwaitingAction && Receipt.Sequence > 0 && !Receipt.bSucceeded);
    const bool bDevelopment = Actor && Actor->IsDevelopmentModel();
    const FText Status = bPersistenceFailure ? PersistencePresentation.Message : bAwaitingAction
        ? bPendingLong ? LOCTEXT("WaitingLong","Still waiting for confirmation. Close and reopen your pack if this continues.") : LOCTEXT("Waiting","Confirming your action...")
        : !bReady ? FeedbackText.IsEmpty() ? LOCTEXT("PackLoading","Your pack is loading. Items will be available when ready.") : PlayerFeedback(FeedbackText,bDevelopment)
        : FeedbackText.IsEmpty() ? LOCTEXT("IdleNote","Your items are saved after each successful change.") : PlayerFeedback(FeedbackText,bDevelopment);
    const FText ControlsHelp = LOCTEXT("PackControlsGuide",
        "LMB: walk / target attack\nCtrl+LMB: jump\nF1-F10: assigned actions\nRMB: loot / selected skill\nI / Esc: pack\nProgress saves automatically.\n\nIn the pack, right-click a weapon to equip or balm to use. Drag a skill or balm onto an F-key; right-click a slot to clear.");
    // Exact initial controls and routine-success notes are summarized; full Status/error feedback stays intact.
    const bool bInitialControlsNote = !bAwaitingAction && !bPersistenceFailure && FeedbackText.ToString() == TEXT("LMB: walk / target attack   Ctrl+LMB: jump   F1-F10: assigned actions   RMB: loot / selected skill   I: bag   Progress saves automatically");
    const bool bRestoredNote = bReady && !bAwaitingAction && !bPersistenceFailure
        && Status.ToString() == TEXT("Saved inventory, equipment, coins, quest and uncollected loot restored.");
    const bool bSavedNote = bReady && !bAwaitingAction && !bPersistenceFailure
        && Status.ToString() == TEXT("Character saved and read back successfully.");
    const FText DisplayStatus = bInitialControlsNote
        ? LOCTEXT("PackControlsSummary","LMB: move / attack. F-keys: actions.")
        : bRestoredNote ? LOCTEXT("PackRestoredSummary","Saved progress restored.")
        : bSavedNote ? LOCTEXT("PackSavedSummary","Progress saved and verified.") : Status;
    // Three spatial panels; center remains visible but menu input is consumed.
    TSharedRef<SWidget> Context = bSkillsPage ? BuildSkills()
        : ContextPage == EContextPage::Merchant ? BuildMerchant()
        : ContextPage == EContextPage::Journey ? BuildQuest() : BuildEquipment();
    TSharedRef<SWidget> LeftPanel = SNew(SJadeboundArtBorder).Padding(8.f)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,4.f)
            [Text(bSkillsPage ? LOCTEXT("SpatialSkillsTitle","SKILLS") : ContextPage==EContextPage::Merchant ? LOCTEXT("SpatialShopTitle","MERCHANT")
                : ContextPage==EContextPage::Journey ? LOCTEXT("SpatialQuestTitle","JOURNEY") : LOCTEXT("SpatialEquipmentTitle","TRAVELER"),16.f,Gold,true)]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,5.f)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f,0.f,3.f,0.f)
                [SNew(SButton).ButtonStyle(&SecondaryStyle).ContentPadding(FMargin(3.f,5.f))
                    .ToolTip(PackTooltip(LOCTEXT("EquipmentPageHelp","Inspect your equipped weapon."),LOCTEXT("EquipmentPage","Weapon"),0,210.f))
                    .Tag(FName(TEXT("JadeboundGearTab")))
                    .OnClicked(this,&SJadeboundProgressionPanel::ShowContextPage,EContextPage::Equipment)
                    [Text(LOCTEXT("EquipmentTabGear","Gear"),12.f,!bSkillsPage && ContextPage==EContextPage::Equipment ? Gold : Muted,true)]]
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f,0.f,3.f,0.f)
                [SNew(SButton).ButtonStyle(&SecondaryStyle).ContentPadding(FMargin(3.f,5.f))
                    .IsEnabled(bReady && bMerchantInRange)
                    .ToolTip(PackTooltip(bMerchantInRange ? LOCTEXT("ShopPageHelp","Browse the nearby merchant's wares.") : LOCTEXT("ShopPageRange","Approach the merchant to open the shop."),LOCTEXT("ShopPage","Shop"),0,210.f))
                    .Tag(FName(TEXT("JadeboundShopTab")))
                    .OnClicked(this,&SJadeboundProgressionPanel::ShowContextPage,EContextPage::Merchant)
                    [Text(LOCTEXT("ShopPage","Shop"),12.f,!bSkillsPage && ContextPage==EContextPage::Merchant ? Gold : Muted,true)]]
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f,0.f,3.f,0.f)
                [SNew(SButton).ButtonStyle(&SecondaryStyle).ContentPadding(FMargin(3.f,5.f))
                    .Tag(FName(TEXT("JadeboundQuestTab")))
                    .OnClicked(this,&SJadeboundProgressionPanel::ShowContextPage,EContextPage::Journey)
                    [Text(LOCTEXT("JourneyTabQuest","Quest"),12.f,!bSkillsPage && ContextPage==EContextPage::Journey ? Gold : Muted,true)]]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [SNew(SButton).ButtonStyle(&SecondaryStyle).ContentPadding(FMargin(3.f,5.f))
                    .OnClicked(this,&SJadeboundProgressionPanel::ShowPackPage,true)
                    [Text(LOCTEXT("SkillsPage","Skills"),12.f,bSkillsPage ? Gold : Muted,true)]]]
            + SVerticalBox::Slot().FillHeight(1.f)[Context]];
    TSharedPtr<SWidget> SelectionFooter;
    TSharedPtr<SBox> NoteArea;
    TSharedRef<SWidget> RightPanel = SNew(SJadeboundArtBorder).Padding(8.f)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,4.f)
            [SNew(SBox).HeightOverride(36.f)
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Text(LOCTEXT("SpatialPackTitle","PACK"),16.f,Gold,true)]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,4.f,0.f)[Art(-2,14.f)]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)[Text(FText::AsNumber(DisplayState.Coins),14.f,Gold,true)]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [ActionButton(LOCTEXT("SpatialClose","X"),LOCTEXT("CloseHelp","Close the pack and return to your journey."),true,EAction::Close)]]]
            + SVerticalBox::Slot().FillHeight(1.f)[BuildInventory()]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f,4.f,0.f,0.f)[SNew(SBox).MinDesiredHeight(88.f)[(SelectionFooter=BuildSelection()).ToSharedRef()]]];
    TSharedRef<SWidget> BottomTray = SNew(SJadeboundArtBorder).Padding(FMargin(8.f,6.f))
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)[Text(LOCTEXT("SpatialAssignmentsTitle","F1-F10 ASSIGNMENTS"),10.f,Gold,true,60)]
                + SHorizontalBox::Slot().AutoWidth()
                [SNew(STextBlock).Text(LOCTEXT("PackControlsLabel","Controls"))
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12.f)).ColorAndOpacity(Gold)
                    .ToolTip(PackTooltip(ControlsHelp,LOCTEXT("PackControlsTitle","PACK CONTROLS"),0,250.f))]]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0.f)
                .ToolTip(PackTooltip(LOCTEXT("SharedAssignmentHint","Drag a skill or balm onto F1-F10. Right-click clears. Dropping never uses or casts."),LOCTEXT("AssignmentHintTitle","F1-F10"),0,210.f))
                [BuildHotbarAssignmentRow()]]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f,2.f)[Rule()]
            + SVerticalBox::Slot().FillHeight(1.f)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,8.f,0.f)
                [SNew(SBox).WidthOverride(72.f)
                    .ToolTip(PackTooltip(bPersistenceFailure ? LOCTEXT("AttentionHeading","SAVING NEEDS ATTENTION") : bAwaitingAction ? LOCTEXT("PendingHeading","WAITING FOR CONFIRMATION") : bIssue ? LOCTEXT("IssueHeading","ACTION NEEDS ATTENTION") : LOCTEXT("NoteHeading","FIELD NOTE"),LOCTEXT("StatusTooltipTitle","FIELD NOTE"),0,240.f))
                    [Text(bPersistenceFailure ? LOCTEXT("PersistenceHeadingCompact","SAVING") : bAwaitingAction ? LOCTEXT("PendingHeadingCompact","WAITING") : bIssue ? LOCTEXT("IssueHeadingCompact","ATTENTION") : LOCTEXT("NoteHeadingCompact","NOTE"),10.f,bIssue ? Warning : Gold,true,60)]]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
                [SAssignNew(NoteArea,SBox).HeightOverride(44.f)
                    [SNew(SScrollBox).ScrollBarThickness(FVector2f(4.f,4.f)).ScrollBarAlwaysVisible(true).ConsumeMouseWheel(EConsumeMouseWheel::Always)
                        .ToolTip(PackTooltip(Status,LOCTEXT("StatusTooltipTitle","FIELD NOTE")))
                        + SScrollBox::Slot()
                        [SNew(STextBlock).Text(DisplayStatus).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),13.f))
                            .ToolTip(PackTooltip(Status,LOCTEXT("StatusTooltipTitle","FIELD NOTE"),0,320.f))
                            .ColorAndOpacity(bIssue ? Warning : Ink).WrapTextAt(300.f).AutoWrapText(false)]]]]];
    const TSharedRef<SWidget> MenuHealth=SNew(SJadeboundMenuPlayerHealth).ProgressionActor(ProgressionActor);
    TArray<TWeakPtr<SWidget>> TooltipExclusions;
    TooltipExclusions.Add(LeftPanel);TooltipExclusions.Add(RightPanel);TooltipExclusions.Add(SelectionFooter);
    TooltipExclusions.Add(BottomTray);TooltipExclusions.Add(NoteArea);TooltipExclusions.Add(MenuHealth);
    ChildSlot
    [SNew(SDPIScaler).Visibility(EVisibility::SelfHitTestInvisible)
        .DPIScale(TAttribute<float>::CreateLambda([Weak=ProgressionActor]() { return GameplayHudScaleFloor(Weak); }))
        [SNew(SJadeboundTooltipHost).ProtectedWidgets(TooltipExclusions).LeftPanel(LeftPanel).RightPanel(RightPanel)
            [SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
                + SOverlay::Slot()
                    [SNew(SJadeboundSpatialPackLayout).LeftPanel(LeftPanel).RightPanel(RightPanel).BottomTray(BottomTray)]
                + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(12.f)
                    [MenuHealth]]]];
    if (CatalogScroll.IsValid()) CatalogScroll->SetScrollOffset(Offset);
    if (InventoryScroll.IsValid()) InventoryScroll->SetScrollOffset(InventoryOffset);
    if (bHadFocus && FSlateApplication::IsInitialized())
    {
        const TWeakPtr<SWidget>* Target = FocusTargets.Find(FocusKey);
        const TSharedPtr<SWidget> Restore = Target ? Target->Pin() : nullptr;
        if (!Restore.IsValid() || !Restore->IsEnabled() || !FSlateApplication::Get().SetKeyboardFocus(Restore,EFocusCause::SetDirectly))
        {
            // An action can become disabled after a commit (for example the last
            // permitted split). Keep focus on its selected slot when it survives.
            const TWeakPtr<SWidget>* SelectedTarget = FocusKey != NAME_None && SelectedSlot != INDEX_NONE
                ? FocusTargets.Find(FName(*FString::Printf(TEXT("Slot-%d"),SelectedSlot))) : nullptr;
            const TSharedPtr<SWidget> SelectedRestore = SelectedTarget ? SelectedTarget->Pin() : nullptr;
            if (!SelectedRestore.IsValid() || !SelectedRestore->IsEnabled() || !FSlateApplication::Get().SetKeyboardFocus(SelectedRestore,EFocusCause::SetDirectly))
                FSlateApplication::Get().SetKeyboardFocus(SharedThis(this),EFocusCause::SetDirectly);
        }
    }
}

namespace
{
// The death modal is the only full-screen input surface in the gameplay HUD.
// Its token is the last rendered life, and the Actor rechecks that token and
// the authority clock even when input arrived before the disabled button.
class SJadeboundReviveOverlay final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SJadeboundReviveOverlay) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AJadeboundProgressionActor>, ProgressionActor)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Model=Args._ProgressionActor;
        SetVisibility(TAttribute<EVisibility>::CreateLambda([Weak=Model]() {
            const auto* Actor=Weak.Get();
            return IsValid(Actor) && Actor->GetRevivePresentation().bWaiting ? EVisibility::Visible : EVisibility::Collapsed;
        }));
        ChildSlot
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(.004f,.012f,.014f,.07f)).Padding(0.f)
            [SNew(SOverlay)
                + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(0.f,24.f,24.f,0.f))
                [SNew(SBox).WidthOverride(328.f)
                    [SNew(SJadeboundArtBorder).Frame(true).Padding(FMargin(20.f,16.f))
                        [SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [SNew(STextBlock).Text(LOCTEXT("SpiritUnbound","SPIRIT UNBOUND"))
                                .Justification(ETextJustify::Center).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),17.f)).ColorAndOpacity(Gold)]
                            + SVerticalBox::Slot().AutoHeight().Padding(0.f,9.f)[Rule(Bronze)]
                            + SVerticalBox::Slot().AutoHeight()
                            [SNew(STextBlock).Text_Lambda([this]() {
                                return Rendered.SecondsRemaining>0 ? FText::Format(LOCTEXT("ReviveCountdown","Revive in {0}"),FText::AsNumber(Rendered.SecondsRemaining)) : LOCTEXT("SpiritReady","Your spirit is ready");
                            }).Justification(ETextJustify::Center).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Medium"),25.f)).ColorAndOpacity(Ink)]
                            + SVerticalBox::Slot().AutoHeight().Padding(0.f,8.f,0.f,12.f)
                            [SNew(STextBlock).Text_Lambda([this]() { return Rendered.Message; })
                                .Justification(ETextJustify::Center).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14.f))
                                .ColorAndOpacity(Muted).WrapTextAt(286.f).AutoWrapText(false)]
                            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                            [SNew(SBox).WidthOverride(188.f).HeightOverride(42.f)
                                [SNew(SButton).IsFocusable(true).ContentPadding(FMargin(14.f,7.f))
                                    .ButtonColorAndOpacity(SelectedJade)
                                    .IsEnabled_Lambda([this]() {
                                        const auto* Actor=Model.Get();
                                        if (!IsValid(Actor)) return false;
                                        const auto Current=Actor->GetRevivePresentation();
                                        return Current.bCanRevive && Current.Generation==Rendered.Generation;
                                    }).OnClicked_Lambda([this]() { return Revive(); })
                                    [SNew(STextBlock).Text(LOCTEXT("ManualRevive","Revive"))
                                        .Justification(ETextJustify::Center).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),18.f)).ColorAndOpacity(Ink)]]]
                            + SVerticalBox::Slot().AutoHeight().Padding(0.f,9.f,0.f,0.f)
                            [SNew(STextBlock).Text(LOCTEXT("ReviveKeyboard","Enter or R to revive when ready"))
                                .Justification(ETextJustify::Center).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11.f)).ColorAndOpacity(Muted)]]]]]];
    }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual void Tick(const FGeometry& Geometry,double Time,float Delta) override
    {
        SCompoundWidget::Tick(Geometry,Time,Delta);
        const auto* Actor=Model.Get();
        Rendered=IsValid(Actor) ? Actor->GetRevivePresentation() : FJadeboundRevivePresentation{};
        if (Rendered.bWaiting && Rendered.Generation!=FocusedGeneration && FSlateApplication::IsInitialized())
        {
            if (FSlateApplication::Get().SetKeyboardFocus(SharedThis(this),EFocusCause::SetDirectly))
                FocusedGeneration=Rendered.Generation;
        }
    }
    virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent& Event) override
    {
        if (!Event.IsRepeat() && (Event.GetKey()==EKeys::Enter || Event.GetKey()==EKeys::R)) return Revive();
        return FReply::Handled();
    }
    virtual FReply OnKeyUp(const FGeometry&,const FKeyEvent&) override { return FReply::Handled(); }
    virtual FReply OnKeyChar(const FGeometry&,const FCharacterEvent&) override { return FReply::Handled(); }
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&) override
    { return FReply::Handled().SetUserFocus(SharedThis(this),EFocusCause::Mouse); }
    virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&) override { return FReply::Handled(); }
    virtual FReply OnMouseButtonDoubleClick(const FGeometry&,const FPointerEvent&) override { return FReply::Handled(); }
    virtual FReply OnMouseWheel(const FGeometry&,const FPointerEvent&) override { return FReply::Handled(); }
private:
    FReply Revive()
    {
        if (auto* Actor=Model.Get(); IsValid(Actor)) Actor->RequestRevive(Rendered.Generation);
        return FReply::Handled();
    }
    TWeakObjectPtr<AJadeboundProgressionActor> Model;
    FJadeboundRevivePresentation Rendered;
    uint64 FocusedGeneration=0;
};
}

TSharedRef<SWidget> SJadeboundProgressionPanel::BuildGameplayHud(TWeakObjectPtr<AJadeboundProgressionActor> Actor)
{
    return SNew(SDPIScaler).Visibility(EVisibility::SelfHitTestInvisible)
        .DPIScale(TAttribute<float>::CreateLambda([Weak=Actor]() { return GameplayHudScaleFloor(Weak); }))
        [SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            + SOverlay::Slot()[SNew(SJadeboundTooltipHost)[SNew(SJadeboundGameplayDock).ProgressionActor(Actor)]]
            + SOverlay::Slot()[SNew(SJadeboundReviveOverlay).ProgressionActor(Actor)]];
}

#undef LOCTEXT_NAMESPACE
