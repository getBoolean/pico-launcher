#pragma once
#include "View.h"
#include "gui/font/nitroFont2.h"
#include "gui/VBlankTextureLoader.h"
#include "core/math/Rgb.h"

class IVramManager;

class alignas(32) KeyboardView : public View
{
    SHARED_ONLY(KeyboardView)

public:
    typedef void (*key_callback_t)(char key, void* context);

    void InitVram(const VramContext& vramContext) override;
    void Update() override;
    void Draw(GraphicsContext& graphicsContext) override;
    void VBlank() override;

    bool HandleInput(const InputProvider& inputProvider, FocusManager& focusManager) override;
    SharedPtr<View> MoveFocus(const SharedPtr<View>& currentFocus,
        FocusMoveDirection direction, View* source) override;

    Rectangle GetBounds() const override
    {
        return Rectangle(_position.x, _position.y, KEYBOARD_WIDTH, KEYBOARD_HEIGHT);
    }

    void SetOnKeyPress(key_callback_t callback, void* context)
    {
        _onKeyPress = callback;
        _onKeyPressContext = context;
    }

    void SetBackgroundColor(const Rgb<8, 8, 8>& color) { _backgroundColor = color; }
    void SetForegroundColor(const Rgb<8, 8, 8>& color) { _foregroundColor = color; }
    void SetHighlightColor(const Rgb<8, 8, 8>& color) { _highlightColor = color; }

    int GetFocusedRow() const { return _focusRow; }
    int GetFocusedCol() const { return _focusCol; }

    static constexpr int KEYBOARD_WIDTH = 220;
    static constexpr int KEYBOARD_HEIGHT = 80;

private:
    static constexpr int KEY_WIDTH = 20;
    static constexpr int KEY_HEIGHT = 18;
    static constexpr int ROW_COUNT = 4;
    static constexpr int MAX_COLS = 11;

    static const char* const sRows[ROW_COUNT];
    static const int sRowLengths[ROW_COUNT];
    static const int sRowOffsets[ROW_COUNT];

    const nft2_header_t* _font;
    VBlankTextureLoader* _vblankTextureLoader;
    VBlankTextureLoadRequest _textureLoadRequest;

    key_callback_t _onKeyPress = nullptr;
    void* _onKeyPressContext = nullptr;

    Rgb<8, 8, 8> _backgroundColor;
    Rgb<8, 8, 8> _foregroundColor;
    Rgb<8, 8, 8> _highlightColor;

    int _focusRow = 0;
    int _focusCol = 0;

    u32 _texVramOffset = 0;
    u32 _tileBufferSize = 0;
    std::unique_ptr<u8[]> _tileBuffer;
    KeyboardView(const nft2_header_t* font, VBlankTextureLoader* vblankTextureLoader);

    void RenderKeyboard();
    char GetKeyAt(int row, int col) const;
    void ClampCol();
};
