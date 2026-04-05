#pragma once
#include "romBrowser/views/BottomSheetView.h"
#include "gui/views/Label2DView.h"
#include "gui/views/KeyboardView.h"

class MaterialColorScheme;
class IFontRepository;

class TextInputBottomSheetView : public BottomSheetView
{
public:
    typedef void (*text_changed_callback_t)(const char* text, void* context);
    typedef void (*close_callback_t)(void* context);

    TextInputBottomSheetView(const MaterialColorScheme* materialColorScheme,
        const IFontRepository* fontRepository, VBlankTextureLoader* vblankTextureLoader,
        u32 maxLength = 31);

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

    void SetOnTextChanged(text_changed_callback_t callback, void* context)
    {
        _onTextChanged = callback;
        _onTextChangedContext = context;
    }

    void SetOnClose(close_callback_t callback, void* context)
    {
        _onClose = callback;
        _onCloseContext = context;
    }

    void SetTitle(const char* title);
    const char* GetText() const { return _text; }
    void SetText(const char* text);

protected:
    KeyboardView& GetKeyboardView() { return _keyboardView; }

private:
    static constexpr u32 MAX_TEXT_LENGTH = 32;

    const MaterialColorScheme* _materialColorScheme;

    Label2DView _titleLabel;
    Label2DView _queryLabel;
    KeyboardView _keyboardView;

    char _text[MAX_TEXT_LENGTH] {};
    u32 _maxLength;

    text_changed_callback_t _onTextChanged = nullptr;
    void* _onTextChangedContext = nullptr;
    close_callback_t _onClose = nullptr;
    void* _onCloseContext = nullptr;

    static void OnKeyPress(char key, void* context);
    void HandleKeyPress(char key);
    void UpdateQueryLabel();
};
