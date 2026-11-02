#include "common.h"
#include <string.h>
#include <bit>
#include "gui/IVramManager.h"
#include "gui/Gx.h"
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

KeyboardView::KeyboardView(const nft2_header_t* font, VBlankTextureLoader* vblankTextureLoader)
    : _font(font), _vblankTextureLoader(vblankTextureLoader)
{
    u32 texWidth = std::bit_ceil((u32)KEYBOARD_WIDTH);
    u32 texHeight = KEYBOARD_HEIGHT;
    _tileBufferSize = texWidth * texHeight;
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
    u32 texWidth = std::bit_ceil((u32)KEYBOARD_WIDTH);
    memset(_tileBuffer.get(), 0, _tileBufferSize);

    char16_t charBuf[2] = { 0, 0 };
    nft2_string_render_params_t renderParams;
    renderParams.a5i3 = true;
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
            nft2_renderString(_font, charBuf, _tileBuffer.get(), texWidth, &renderParams);
        }
    }

    _vblankTextureLoader->CancelLoad(_textureLoadRequest);
    _textureLoadRequest = VBlankTextureLoadRequest(_tileBuffer.get(), _tileBufferSize,
        _texVramOffset, nullptr, 0, 0, nullptr, nullptr);
    _vblankTextureLoader->RequestLoad(_textureLoadRequest);
}

void KeyboardView::InitVram(const VramContext& vramContext)
{
    const auto texVramManager = vramContext.GetTexVramManager();
    if (texVramManager)
    {
        _texVramOffset = texVramManager->Alloc(_tileBufferSize);
    }
    RenderKeyboard();
}

void KeyboardView::Update()
{
}

void KeyboardView::Draw(GraphicsContext& graphicsContext)
{
    if (!graphicsContext.IsVisible(GetBounds()))
        return;

    u32 texWidth = std::bit_ceil((u32)KEYBOARD_WIDTH);

    // Draw focus highlight as a solid colored 3D quad behind the focused key
    if (_isFocused)
    {
        int keyX = _position.x + sRowOffsets[_focusRow] + _focusCol * KEY_WIDTH;
        int keyY = _position.y + _focusRow * KEY_HEIGHT;

        Gx::MtxIdentity();
        Gx::PolygonAttr(GX_LIGHTMASK_NONE, GX_POLYGON_MODE_DECAL, GX_DISPLAY_MODE_FRONT,
            false, false, false, GX_DEPTH_FUNC_LESS, false, 31, graphicsContext.GetPolygonId());
        Gx::Color(Rgb<5, 5, 5>(_highlightColor));
        Gx::Begin(GX_PRIMITIVE_QUAD);
        REG_GX_VTX_16 = GX_VTX_PACK(keyX << 6, keyY << 3);
        REG_GX_VTX_16 = (201) << 6;
        REG_GX_VTX_16 = GX_VTX_PACK(keyX << 6, (keyY + KEY_HEIGHT) << 3);
        REG_GX_VTX_16 = (201) << 6;
        REG_GX_VTX_16 = GX_VTX_PACK((keyX + KEY_WIDTH) << 6, (keyY + KEY_HEIGHT) << 3);
        REG_GX_VTX_16 = (201) << 6;
        REG_GX_VTX_16 = GX_VTX_PACK((keyX + KEY_WIDTH) << 6, keyY << 3);
        REG_GX_VTX_16 = (201) << 6;
        Gx::End();
    }

    // Draw keyboard text as 3D textured quad
    {
        Gx::MtxIdentity();
        Gx::PolygonAttr(GX_LIGHTMASK_NONE, GX_POLYGON_MODE_MODULATE, GX_DISPLAY_MODE_FRONT,
            false, false, false, GX_DEPTH_FUNC_LESS, false, 31, graphicsContext.GetPolygonId());
        Gx::TexImageParam(_texVramOffset >> 3, false, false, false, false,
            (GxTexSize)(std::bit_width(texWidth) - 4),
            GX_TEXSIZE_1024, GX_TEXFMT_A5I3, false, GX_TEXGEN_NONE);
        graphicsContext.GetRgb6Palette()->ApplyColor(Rgb<6, 6, 6>(_foregroundColor));
        Gx::Begin(GX_PRIMITIVE_QUAD);
        Gx::TexCoord(0, 0);
        REG_GX_VTX_16 = GX_VTX_PACK(_position.x << 6, _position.y << 3);
        REG_GX_VTX_16 = (200) << 6;
        Gx::TexCoord(0, (int)KEYBOARD_HEIGHT);
        REG_GX_VTX_16 = GX_VTX_PACK(_position.x << 6, (_position.y + KEYBOARD_HEIGHT) << 3);
        REG_GX_VTX_16 = (200) << 6;
        Gx::TexCoord((int)KEYBOARD_WIDTH, (int)KEYBOARD_HEIGHT);
        REG_GX_VTX_16 = GX_VTX_PACK((_position.x + KEYBOARD_WIDTH) << 6, (_position.y + KEYBOARD_HEIGHT) << 3);
        REG_GX_VTX_16 = (200) << 6;
        Gx::TexCoord((int)KEYBOARD_WIDTH, 0);
        REG_GX_VTX_16 = GX_VTX_PACK((_position.x + KEYBOARD_WIDTH) << 6, _position.y << 3);
        REG_GX_VTX_16 = (200) << 6;
        Gx::End();
    }
}

void KeyboardView::VBlank()
{
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
