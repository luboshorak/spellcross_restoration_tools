#pragma once

#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/event.h>

#include <functional>
#include <vector>

#include "spellcross.h"
#include "map.h"

enum class FormMainMenuAction
{
    NewGame,
    Continue,
    LoadGame,
    Credits,
    Intro,
    Exit
};

class FormMainMenu
{
public:
    FormMainMenu(wxPanel* parent,
        wxWindowID win_id,
        SpellMap* spell_map,
        std::function<void(FormMainMenuAction)> action_cb);
    ~FormMainMenu();

private:
    struct MenuItem
    {
        wxString label;
        FormMainMenuAction action;
        wxRect rect;
    };

    SpellMap* m_spell_map;
    SpellData* m_spelldata;
    wxFrame* form;
    wxBitmap m_background;
    wxSize m_bg_size;
    std::vector<MenuItem> m_items;
    int m_hover_index;
    std::function<void(FormMainMenuAction)> m_action_cb;

    bool LoadBackground();
    void BuildMenuItems();
    void LayoutMenuItems(const wxSize& clientSize);
    void TriggerAction(int index);

    void OnClose(wxCloseEvent& ev);
    void OnPaint(wxPaintEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnMouseClick(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
};
