#include "common.h"
#include <string.h>
#include "core/StringUtil.h"
#include "gui/GraphicsContext.h"
#include "gui/VramContext.h"
#include "gui/IVramManager.h"
#include "gui/input/InputProvider.h"
#include "themes/material/MaterialColorScheme.h"
#include "themes/IFontRepository.h"
#include "folderIcon.h"
#include "../IRomBrowserController.h"
#include "SearchBottomSheetView.h"

#define TITLE_LABEL_X       20
#define TITLE_LABEL_Y       12
#define QUERY_LABEL_X       20
#define QUERY_LABEL_Y       30
#define KEYBOARD_X          18
#define KEYBOARD_Y          50
#define TOGGLE_X            206
#define TOGGLE_Y_OFFSET     28

SearchBottomSheetView::SearchBottomSheetView(IRomBrowserController* controller,
    const MaterialColorScheme* materialColorScheme,
    const IFontRepository* fontRepository,
    VBlankTextureLoader* vblankTextureLoader)
    : _controller(controller)
    , _materialColorScheme(materialColorScheme)
    , _titleLabel(128, 16, 25, fontRepository->GetFont(FontType::Medium11))
    , _queryLabel(176, 14, MAX_TEXT_LENGTH + 1, fontRepository->GetFont(FontType::Regular10))
    , _keyboardView(fontRepository->GetFont(FontType::Regular10), vblankTextureLoader)
    , _subFolderToggle(
        IconButtonView::Type::Tonal,
        IconButtonView::State::ToggleUnselected,
        md::sys::color::surfaceContainerLow,
        materialColorScheme)
{
    _text[0] = 0;
    _titleLabel.SetText(u"Search");
    AddChildTail(&_titleLabel);
    AddChildTail(&_queryLabel);
    AddChildTail(&_keyboardView);
    AddChildTail(&_subFolderToggle);

    _keyboardView.SetOnKeyPress(&SearchBottomSheetView::OnKeyPress, this);

    _subFolderToggle.SetAction([] (IconButtonView* sender, void* arg)
    {
        auto self = static_cast<SearchBottomSheetView*>(arg);
        self->_includeSubFolders = !self->_includeSubFolders;
        self->_subFolderToggle.SetState(self->_includeSubFolders
            ? IconButtonView::State::ToggleSelected
            : IconButtonView::State::ToggleUnselected);
        self->ApplySearch();
    }, this);

    // Restore previous search query if any
    const char* prevQuery = controller->GetSearchQuery();
    if (prevQuery && prevQuery[0])
    {
        StringUtil::Copy(_text, prevQuery, MAX_TEXT_LENGTH);
    }

    UpdateQueryLabel();
}

void SearchBottomSheetView::OnKeyPress(char key, void* context)
{
    auto self = static_cast<SearchBottomSheetView*>(context);
    self->HandleKeyPress(key);
}

void SearchBottomSheetView::HandleKeyPress(char key)
{
    if (key == '\b')
    {
        u32 len = strlen(_text);
        if (len > 0)
            _text[len - 1] = 0;
    }
    else
    {
        u32 len = strlen(_text);
        if (len < MAX_TEXT_LENGTH - 1)
        {
            _text[len] = key;
            _text[len + 1] = 0;
        }
    }

    UpdateQueryLabel();
    ApplySearch();
}

void SearchBottomSheetView::UpdateQueryLabel()
{
    if (_text[0])
    {
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

void SearchBottomSheetView::ApplySearch()
{
    _controller->ApplySearch(_text, _includeSubFolders);
}

void SearchBottomSheetView::InitVram(const VramContext& vramContext)
{
    BottomSheetView::InitVram(vramContext);

    const auto objVramManager = vramContext.GetObjVramManager();
    if (objVramManager)
    {
        _subFolderToggle.SetIconVramOffset(
            LoadIcon(*objVramManager, folderIconTiles, folderIconTilesLen));
    }
}

void SearchBottomSheetView::Update()
{
    BottomSheetView::Update();

    _titleLabel.SetPosition(TITLE_LABEL_X, _position.y + TITLE_LABEL_Y);
    _queryLabel.SetPosition(QUERY_LABEL_X, _position.y + QUERY_LABEL_Y);
    _keyboardView.SetPosition(KEYBOARD_X, _position.y + KEYBOARD_Y);
    _subFolderToggle.SetPosition(TOGGLE_X, _position.y + TOGGLE_Y_OFFSET);
}

void SearchBottomSheetView::Draw(GraphicsContext& graphicsContext)
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

void SearchBottomSheetView::VBlank()
{
    BottomSheetView::VBlank();
}

bool SearchBottomSheetView::HandleInput(
    const InputProvider& inputProvider, FocusManager& focusManager)
{
    if (inputProvider.Triggered(InputKey::B))
    {
        _controller->HideSearch();
        return true;
    }
    return false;
}

View* SearchBottomSheetView::MoveFocus(
    View* currentFocus, FocusMoveDirection direction, View* source)
{
    // Navigate between keyboard and toggle
    if (currentFocus == &_subFolderToggle)
    {
        if (direction == FocusMoveDirection::Left || direction == FocusMoveDirection::Up)
            return &_keyboardView;
        return nullptr;
    }

    if (currentFocus == &_keyboardView)
    {
        // If keyboard wants to propagate right from the rightmost col, go to toggle
        if (direction == FocusMoveDirection::Right &&
            _keyboardView.GetFocusedCol() >= 9)
        {
            return &_subFolderToggle;
        }
        return _keyboardView.MoveFocus(currentFocus, direction, this);
    }

    if (source == &_keyboardView)
    {
        // Keyboard propagated focus movement to us
        if (direction == FocusMoveDirection::Right)
            return &_subFolderToggle;
        return nullptr;
    }

    return nullptr;
}

void SearchBottomSheetView::SetGraphics(
    const IconButton2DView::VramToken& iconButtonVramToken)
{
    _subFolderToggle.SetGraphics(iconButtonVramToken);
}

u32 SearchBottomSheetView::LoadIcon(IVramManager& vramManager,
    const unsigned int* tiles, u32 tilesLength) const
{
    u32 vramOffset = vramManager.Alloc(tilesLength);
    dma_ntrCopy32(3, tiles, vramManager.GetVramAddress(vramOffset), tilesLength);
    return vramOffset;
}
