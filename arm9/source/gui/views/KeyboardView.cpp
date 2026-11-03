#include "common.h"
#include <string.h>
#include "gui/IVramManager.h"
#include "gui/OamBuilder.h"
#include "gui/palette/GradientPalette.h"
#include "gui/VramContext.h"
#include "gui/GraphicsContext.h"
#include "gui/input/InputProvider.h"
#include "KeyboardView.h"

// QWERTY layout with special keys
// Row 0: 1234567890 (10 keys)
// Row 1: QWERTYUIOP (10 keys)
// Row 2: ASDFGHJKL- (10 keys)
// Row 3: ZXCVBNM_< (9 keys: 7 letters + space + backspace)

static const char sRow0[] = "1234567890";
static const char sRow1[] = "QWERTYUIOP";
static const char sRow2[] = "ASDFGHJKL-";
static const char sRow3[] = "ZXCVBNM \b";

const char* const KeyboardView::sRows[ROW_COUNT] = { sRow0, sRow1, sRow2, sRow3 };
const int KeyboardView::sRowLengths[ROW_COUNT] = { 10, 10, 10, 9 };
const int KeyboardView::sRowOffsets[ROW_COUNT] = { 0, 0, 10, 20 };

KeyboardView::KeyboardView(const nft2_header_t* font)
    : _font(font)
{
    _tileBufferSize = (BUFFER_WIDTH * BUFFER_HEIGHT) >> 1;
    _tileBuffer = std::make_unique_for_overwrite<u8[]>(_tileBufferSize);
}

char KeyboardView::GetKeyAt(int row, int col) const
{
    if (row < 0 || row >= ROW_COUNT)
        return 0;
    if (col < 0 || col >= sRowLengths[row])
        return 0;
    return sRows[row][col];
}

void KeyboardView::ClampCol()
{
    if (_focusCol >= sRowLengths[_focusRow])
        _focusCol = sRowLengths[_focusRow] - 1;
}

void KeyboardView::RenderKeyboard()
{
    memset(_tileBuffer.get(), 0, _tileBufferSize);

    char16_t charBuf[2] = { 0, 0 };
    nft2_string_render_params_t renderParams;
    renderParams.a5i3 = false;
    renderParams.onlyRenderWholeGlyphs = true;

    for (int row = 0; row < ROW_COUNT; row++)
    {
        int xStart = sRowOffsets[row];
        for (int col = 0; col < sRowLengths[row]; col++)
        {
            char key = sRows[row][col];
            if (key == '\b')
            {
                charBuf[0] = u'<';
            }
            else if (key == ' ')
            {
                charBuf[0] = u'_';
            }
            else
            {
                charBuf[0] = (char16_t)key;
            }

            int keyX = xStart + col * KEY_WIDTH;
            int keyY = row * KEY_HEIGHT;

            // Center character in key cell
            u32 charWidth, charHeight;
            nft2_measureString(_font, charBuf, charWidth, charHeight);
            int cx = keyX + (KEY_WIDTH - (int)charWidth) / 2;
            int cy = keyY + (KEY_HEIGHT - (int)charHeight) / 2;

            renderParams.x = cx;
            renderParams.y = cy;
            renderParams.width = KEYBOARD_WIDTH;
            renderParams.height = KEYBOARD_HEIGHT;
            nft2_renderString(_font, charBuf, _tileBuffer.get(), BUFFER_WIDTH / 4, &renderParams);
        }
    }

    _tileBufferUpdated = true;
}

void KeyboardView::InitVram(const VramContext& vramContext)
{
    const auto objVramManager = vramContext.GetObjVramManager();
    if (objVramManager)
    {
        _vramOffset = objVramManager->Alloc(_tileBufferSize);
        _vramAddress = objVramManager->GetVramAddress(_vramOffset);
        _highlightVramOffset = objVramManager->Alloc(512);
        u8 highlightTiles[512] {};
        for (u32 y = 0; y < KEY_HEIGHT; y++)
        {
            for (u32 x = 0; x < KEY_WIDTH; x++)
            {
                u32 pixel = ((y >> 3) * 4 + (x >> 3)) * 64 + (y & 7) * 8 + (x & 7);
                highlightTiles[pixel >> 1] |= 15 << ((pixel & 1) * 4);
            }
        }
        memcpy((void*)objVramManager->GetVramAddress(_highlightVramOffset), highlightTiles, sizeof(highlightTiles));
    }
    RenderKeyboard();
}

void KeyboardView::Update()
{
}

void KeyboardView::Draw(GraphicsContext& graphicsContext)
{
    if (!graphicsContext.IsVisible(GetBounds()) || !_vramAddress)
        return;

    // OAM allocation runs backwards, so allocate the highlight before the text.
    if (_isFocused)
    {
        int keyX = _position.x + sRowOffsets[_focusRow] + _focusCol * KEY_WIDTH;
        int keyY = _position.y + _focusRow * KEY_HEIGHT;
        u32 paletteRow = graphicsContext.GetPaletteManager().AllocRow(
            GradientPalette(_highlightColor, _highlightColor), keyY, keyY + KEY_HEIGHT);
        auto oam = graphicsContext.GetOamManager().AllocOams(1);
        OamBuilder::OamWithSize<32, 32>(keyX, keyY, _highlightVramOffset >> 7)
            .WithPalette16(paletteRow)
            .WithPriority(graphicsContext.GetPriority())
            .Build(oam[0]);
    }

    constexpr u32 hCellCount = BUFFER_WIDTH / 32;
    constexpr u32 vCellCount = BUFFER_HEIGHT / 16;
    auto oams = graphicsContext.GetOamManager().AllocOams(hCellCount * vCellCount);
    u32 paletteRow = graphicsContext.GetPaletteManager().AllocRow(
        GradientPalette(_backgroundColor, _foregroundColor), _position.y, _position.y + KEYBOARD_HEIGHT);
    u32 i = 0;
    for (u32 y = 0; y < vCellCount; y++)
    {
        for (u32 x = 0; x < hCellCount; x++)
        {
            OamBuilder::OamWithSize<32, 16>(
                    _position.x + x * 32, _position.y + y * 16,
                    (_vramOffset + i * 256) >> 7)
                .WithPalette16(paletteRow)
                .WithPriority(graphicsContext.GetPriority())
                .Build(oams[i]);
            i++;
        }
    }
}

void KeyboardView::VBlank()
{
    if (_tileBufferUpdated && _vramAddress)
    {
        memcpy((void*)_vramAddress, _tileBuffer.get(), _tileBufferSize);
        _tileBufferUpdated = false;
    }
}

bool KeyboardView::HandleInput(const InputProvider& inputProvider, FocusManager& focusManager)
{
    if (inputProvider.Triggered(InputKey::A))
    {
        PressKey(_focusRow, _focusCol);
        return true;
    }
    return View::HandleInput(inputProvider, focusManager);
}

void KeyboardView::PressKey(int row, int col)
{
    char key = GetKeyAt(row, col);
    if (key && _onKeyPress)
    {
        _onKeyPress(key, _onKeyPressContext);
    }
}

bool KeyboardView::HitTest(const Point& point, int& row, int& col) const
{
    if (!GetBounds().Contains(point))
        return false;

    row = (point.y - _position.y) / KEY_HEIGHT;
    if (row < 0 || row >= ROW_COUNT)
        return false;

    int x = point.x - _position.x - sRowOffsets[row];
    if (x < 0)
        return false;

    col = x / KEY_WIDTH;
    return col < sRowLengths[row];
}

void KeyboardView::HandlePenDown(const Point& touchPoint, FocusManager& focusManager)
{
    if (!HitTest(touchPoint, _penDownRow, _penDownCol))
    {
        _penDownRow = -1;
        _penDownCol = -1;
    }
}

void KeyboardView::HandlePenMove(const Point& touchPoint, FocusManager& focusManager)
{
    // Sliding off the key cancels the press
    int row, col;
    if (!HitTest(touchPoint, row, col) || row != _penDownRow || col != _penDownCol)
    {
        _penDownRow = -1;
        _penDownCol = -1;
    }
}

void KeyboardView::HandlePenUp(const Point& lastTouchPoint, FocusManager& focusManager)
{
    int row, col;
    if (_penDownRow >= 0 && HitTest(lastTouchPoint, row, col) &&
        row == _penDownRow && col == _penDownCol)
    {
        _focusRow = row;
        _focusCol = col;
        focusManager.Focus(SharedFromThis());
        PressKey(row, col);
    }

    _penDownRow = -1;
    _penDownCol = -1;
}

SharedPtr<View> KeyboardView::MoveFocus(const SharedPtr<View>& currentFocus,
    FocusMoveDirection direction, View* source)
{
    if (currentFocus.GetPointer() != this)
    {
        if (source == GetParent())
        {
            return SharedFromThis();
        }
        return nullptr;
    }

    switch (direction)
    {
        case FocusMoveDirection::Left:
            _focusCol--;
            if (_focusCol < 0)
                _focusCol = sRowLengths[_focusRow] - 1;
            return SharedFromThis();
        case FocusMoveDirection::Right:
            _focusCol++;
            if (_focusCol >= sRowLengths[_focusRow])
                _focusCol = 0;
            return SharedFromThis();
        case FocusMoveDirection::Up:
            if (_focusRow > 0)
            {
                _focusRow--;
                ClampCol();
                return SharedFromThis();
            }
            // Propagate up to parent for navigation out of keyboard
            if (GetParent() && GetParent() != source)
                return GetParent()->MoveFocus(currentFocus, direction, this);
            return nullptr;
        case FocusMoveDirection::Down:
            if (_focusRow < ROW_COUNT - 1)
            {
                _focusRow++;
                ClampCol();
                return SharedFromThis();
            }
            if (GetParent() && GetParent() != source)
                return GetParent()->MoveFocus(currentFocus, direction, this);
            return nullptr;
    }
    return nullptr;
}
