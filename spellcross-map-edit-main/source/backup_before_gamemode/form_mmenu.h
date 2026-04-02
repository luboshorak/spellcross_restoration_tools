#pragma once

#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/event.h>
#include <wx/gdicmn.h>

#include <array>

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
        SpellData* spell_data,
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

    wxBitmap m_panel;
    wxPoint m_panel_pos;
    wxSize m_panel_size;
    std::array<unsigned char, 256 * 3> m_pal256;
    bool m_pal256_ok;
    std::vector<MenuItem> m_items;
    int m_hover_index;
    std::function<void(FormMainMenuAction)> m_action_cb;

    bool LoadBackground();
    bool LoadPanel();
    void BuildMenuItems();
    void LayoutMenuItems(const wxSize& clientSize);
    void TriggerAction(int index);

    void OnClose(wxCloseEvent& ev);
    void OnPaint(wxPaintEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnMouseClick(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnOptionsAudio(wxCommandEvent& ev);

    static constexpr int ID_MMENU_OPTIONS_AUDIO  = 9901;
};
