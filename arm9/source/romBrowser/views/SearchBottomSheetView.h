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
public:
    SearchBottomSheetView(IRomBrowserController* controller,
        const MaterialColorScheme* materialColorScheme,
        const IFontRepository* fontRepository,
        VBlankTextureLoader* vblankTextureLoader);

    void InitVram(const VramContext& vramContext) override;
    void Update() override;
    void Draw(GraphicsContext& graphicsContext) override;
    void VBlank() override;
    bool HandleInput(const InputProvider& inputProvider, FocusManager& focusManager) override;
    View* MoveFocus(View* currentFocus, FocusMoveDirection direction, View* source) override;

    void Focus(FocusManager& focusManager) override
    {
        focusManager.Focus(&_keyboardView);
    }

    void SetGraphics(const IconButton2DView::VramToken& iconButtonVramToken);

private:
    static constexpr u32 MAX_TEXT_LENGTH = 32;

    IRomBrowserController* _controller;
    const MaterialColorScheme* _materialColorScheme;

    Label2DView _titleLabel;
    Label2DView _queryLabel;
    KeyboardView _keyboardView;
    IconButton2DView _subFolderToggle;

    char _text[MAX_TEXT_LENGTH] {};
    bool _includeSubFolders = false;

    static void OnKeyPress(char key, void* context);
    void HandleKeyPress(char key);
    void UpdateQueryLabel();
    void ApplySearch();

    u32 LoadIcon(IVramManager& vramManager, const unsigned int* tiles, u32 tilesLength) const;
};
