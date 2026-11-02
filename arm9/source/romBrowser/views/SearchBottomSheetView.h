#pragma once
#include "BottomSheetView.h"
#include "gui/views/Label2DView.h"
#include "gui/views/KeyboardView.h"
#include "IconButton2DView.h"

class IRomBrowserController;
class MaterialColorScheme;
class IFontRepository;
class VBlankTextureLoader;

class SearchBottomSheetView : public BottomSheetView
{
    SHARED_ONLY(SearchBottomSheetView)

public:
    void InitVram(const VramContext& vramContext) override;
    void Update() override;
    void Draw(GraphicsContext& graphicsContext) override;
    bool HandleInput(const InputProvider& inputProvider, FocusManager& focusManager) override;
    SharedPtr<View> MoveFocus(const SharedPtr<View>& currentFocus,
        FocusMoveDirection direction, View* source) override;

    void Focus(FocusManager& focusManager) override
    {
        focusManager.Focus(_keyboardView);
    }

    void SetGraphics(const IconButton2DView::VramToken& iconButtonVramToken);

protected:
    void Close() override;

private:
    static constexpr u32 MAX_TEXT_LENGTH = 32;

    IRomBrowserController* _controller;
    const MaterialColorScheme* _materialColorScheme;

    SharedPtr<Label2DView> _titleLabel;
    SharedPtr<Label2DView> _queryLabel;
    SharedPtr<Label2DView> _hintLabel;
    SharedPtr<KeyboardView> _keyboardView;
    SharedPtr<IconButton2DView> _subFolderToggle;

    char _text[MAX_TEXT_LENGTH] {};
    bool _includeSubFolders = false;

    SearchBottomSheetView(IRomBrowserController* controller,
        const MaterialColorScheme* materialColorScheme,
        const IFontRepository* fontRepository,
        VBlankTextureLoader* vblankTextureLoader, u32 keyboardTextureOffset);

    static void OnKeyPress(char key, void* context);
    void HandleKeyPress(char key);
    void UpdateQueryLabel();
    void UpdateSubFolderToggle();
    void ClearQuery();
    void ApplySearch();

    u32 LoadIcon(IVramManager& vramManager, const unsigned int* tiles, u32 tilesLength) const;
};
