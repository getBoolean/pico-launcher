#include "common.h"
#include <string.h>
#include "core/StringUtil.h"
#include "gui/GraphicsContext.h"
#include "gui/VramContext.h"
#include "gui/input/InputProvider.h"
#include "themes/material/MaterialColorScheme.h"
#include "themes/IFontRepository.h"
#include "TextInputBottomSheetView.h"

#define TITLE_LABEL_X       20
#define TITLE_LABEL_Y       12
#define QUERY_LABEL_X       20
#define QUERY_LABEL_Y       30
#define KEYBOARD_X          18
#define KEYBOARD_Y          50

TextInputBottomSheetView::TextInputBottomSheetView(
    const MaterialColorScheme* materialColorScheme,
    const IFontRepository* fontRepository,
    VBlankTextureLoader* vblankTextureLoader, u32 maxLength)
    : _materialColorScheme(materialColorScheme)
    , _titleLabel(128, 16, 25, fontRepository->GetFont(FontType::Medium11))
    , _queryLabel(216, 14, MAX_TEXT_LENGTH + 1, fontRepository->GetFont(FontType::Regular10))
    , _keyboardView(fontRepository->GetFont(FontType::Regular10), vblankTextureLoader)
    , _maxLength(maxLength < MAX_TEXT_LENGTH ? maxLength : MAX_TEXT_LENGTH - 1)
{
    _text[0] = 0;
    AddChildTail(&_titleLabel);
    AddChildTail(&_queryLabel);
    AddChildTail(&_keyboardView);

    _keyboardView.SetOnKeyPress(&TextInputBottomSheetView::OnKeyPress, this);

    _titleLabel.SetText(u"Search");
    UpdateQueryLabel();
}

void TextInputBottomSheetView::OnKeyPress(char key, void* context)
{
    auto self = static_cast<TextInputBottomSheetView*>(context);
    self->HandleKeyPress(key);
}

void TextInputBottomSheetView::HandleKeyPress(char key)
{
    if (key == '\b')
    {
        u32 len = strlen(_text);
        if (len > 0)
        {
            _text[len - 1] = 0;
        }
    }
    else
    {
        u32 len = strlen(_text);
        if (len < _maxLength)
        {
            _text[len] = key;
            _text[len + 1] = 0;
        }
    }

    UpdateQueryLabel();

    if (_onTextChanged)
        _onTextChanged(_text, _onTextChangedContext);
}

void TextInputBottomSheetView::UpdateQueryLabel()
{
    if (_text[0])
    {
        // Show text with cursor
        char display[MAX_TEXT_LENGTH + 2];
        u32 len = strlen(_text);
        memcpy(display, _text, len);
        display[len] = '_';
        display[len + 1] = 0;
        _queryLabel.SetText(display);
    }
    else
    {
        _queryLabel.SetText("_");
    }
}

void TextInputBottomSheetView::SetTitle(const char* title)
{
    _titleLabel.SetText(title);
}

void TextInputBottomSheetView::SetText(const char* text)
{
    StringUtil::Copy(_text, text, MAX_TEXT_LENGTH);
    UpdateQueryLabel();
}

void TextInputBottomSheetView::InitVram(const VramContext& vramContext)
{
    BottomSheetView::InitVram(vramContext);
}

void TextInputBottomSheetView::Update()
{
    BottomSheetView::Update();

    _titleLabel.SetPosition(TITLE_LABEL_X, _position.y + TITLE_LABEL_Y);
    _queryLabel.SetPosition(QUERY_LABEL_X, _position.y + QUERY_LABEL_Y);
    _keyboardView.SetPosition(KEYBOARD_X, _position.y + KEYBOARD_Y);
}

void TextInputBottomSheetView::Draw(GraphicsContext& graphicsContext)
{
    graphicsContext.SetClipArea(GetBounds());
    u32 oldPrio = graphicsContext.SetPriority(1);
    {
        _titleLabel.SetBackgroundColor(
            _materialColorScheme->GetColor(md::sys::color::surfaceContainerLow));
        _titleLabel.SetForegroundColor(_materialColorScheme->onSurface);
        _queryLabel.SetBackgroundColor(
            _materialColorScheme->GetColor(md::sys::color::surfaceContainerLow));
        _queryLabel.SetForegroundColor(_materialColorScheme->onSurface);
        _keyboardView.SetBackgroundColor(
            _materialColorScheme->GetColor(md::sys::color::surfaceContainerLow));
        _keyboardView.SetForegroundColor(_materialColorScheme->onSurface);
        _keyboardView.SetHighlightColor(
            _materialColorScheme->GetColor(md::sys::color::secondaryContainer));
        BottomSheetView::Draw(graphicsContext);
    }
    graphicsContext.SetPriority(oldPrio);
    graphicsContext.ResetClipArea();
}

void TextInputBottomSheetView::VBlank()
{
    BottomSheetView::VBlank();
}

bool TextInputBottomSheetView::HandleInput(
    const InputProvider& inputProvider, FocusManager& focusManager)
{
    if (inputProvider.Triggered(InputKey::B))
    {
        if (_onClose)
            _onClose(_onCloseContext);
        return true;
    }
    return false;
}

View* TextInputBottomSheetView::MoveFocus(
    View* currentFocus, FocusMoveDirection direction, View* source)
{
    if (currentFocus == &_keyboardView || source == &_keyboardView)
    {
        return _keyboardView.MoveFocus(currentFocus, direction, source);
    }
    return nullptr;
}
