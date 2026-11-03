#pragma once
#include "View.h"
#include "gui/font/nitroFont2.h"
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

    void HandlePenDown(const Point& touchPoint, FocusManager& focusManager) override;
    void HandlePenMove(const Point& touchPoint, FocusManager& focusManager) override;
    void HandlePenUp(const Point& lastTouchPoint, FocusManager& focusManager) override;

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

    /// @brief Gets whether the focused key is the last key of its row.
    bool IsFocusAtRowEnd() const { return _focusCol == sRowLengths[_focusRow] - 1; }

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

    key_callback_t _onKeyPress = nullptr;
    void* _onKeyPressContext = nullptr;

    Rgb<8, 8, 8> _backgroundColor;
    Rgb<8, 8, 8> _foregroundColor;
    Rgb<8, 8, 8> _highlightColor;

    int _focusRow = 0;
    int _focusCol = 0;

    /// @brief Key the pen went down on, or -1 if none.
    int _penDownRow = -1;
    int _penDownCol = -1;

    static constexpr u32 BUFFER_WIDTH = (KEYBOARD_WIDTH + 31) & ~31;
    static constexpr u32 BUFFER_HEIGHT = (KEYBOARD_HEIGHT + 15) & ~15;
    u32 _vramOffset = 0;
    u32 _highlightVramOffset = 0;
    vu16* _vramAddress = nullptr;
    bool _tileBufferUpdated = false;
    u32 _tileBufferSize = 0;
    std::unique_ptr<u8[]> _tileBuffer;
    explicit KeyboardView(const nft2_header_t* font);

    void RenderKeyboard();
    char GetKeyAt(int row, int col) const;
    bool HitTest(const Point& point, int& row, int& col) const;
    void PressKey(int row, int col);
    void ClampCol();
};
