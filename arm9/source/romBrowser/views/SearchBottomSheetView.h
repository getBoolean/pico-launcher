#pragma once
#include "BottomSheetView.h"
#include "gui/views/Label2DView.h"
#include "gui/views/KeyboardView.h"

class IRomBrowserController;
class MaterialColorScheme;
class IFontRepository;

class SearchBottomSheetView : public BottomSheetView
{
    SHARED_ONLY(SearchBottomSheetView)

public:
    void Update() override;
    void Draw(GraphicsContext& graphicsContext) override;
    bool HandleInput(const InputProvider& inputProvider, FocusManager& focusManager) override;
    SharedPtr<View> MoveFocus(const SharedPtr<View>& currentFocus,
        FocusMoveDirection direction, View* source) override;

    void Focus(FocusManager& focusManager) override
    {
        focusManager.Focus(_keyboardView);
    }

protected:
    void Close() override;

private:
    static constexpr u32 MAX_TEXT_LENGTH = 32;

    IRomBrowserController* _controller;
    const MaterialColorScheme* _materialColorScheme;

    SharedPtr<Label2DView> _titleLabel;
    SharedPtr<Label2DView> _pathLabel;
    SharedPtr<Label2DView> _queryLabel;
    SharedPtr<Label2DView> _hintLabel;
    SharedPtr<Label2DView> _clearLabel;
    SharedPtr<Label2DView> _closeLabel;
    SharedPtr<KeyboardView> _keyboardView;

    char _text[MAX_TEXT_LENGTH] {};
    bool _includeSubFolders = false;

    SearchBottomSheetView(IRomBrowserController* controller,
        const MaterialColorScheme* materialColorScheme,
        const IFontRepository* fontRepository);

    static void OnKeyPress(char key, void* context);
    void HandleKeyPress(char key);
    void UpdateQueryLabel();
    void UpdateScopeHint();
    void ClearQuery();
    void ApplySearch();

};
