#pragma once

#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/event.h>
#include <wx/gdicmn.h>

#include <functional>
#include <vector>
#include <array>

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
    // Palette used by MAINMENU + MAINM_* buttons (expanded to 256 RGB triples).
    std::array<unsigned char, 256 * 3> m_pal256{};
    bool m_pal_ok = false;
    // Menu panel overlay bitmap (MAINM_BG.LZ) drawn over MAINMENU.
    wxBitmap m_menuPanel;
    wxPoint m_menuPanelPos{ 0,0 };
    std::vector<MenuItem> m_items;
    int m_hover_index;
    std::function<void(FormMainMenuAction)> m_action_cb;

    bool LoadBackground();
    bool LoadMenuPanel();
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
