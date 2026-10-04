#pragma once

#include "spellcross.h"
#include "spell_video.h"
#include <wx/frame.h>
#include <wx/window.h>
#include <wx/panel.h>
#include <wx/bitmap.h>
#include <string>

#ifdef _WIN32
struct IMFPMediaPlayer;
class FormVideoMfCallback;
#endif

class FormVideoBox
{
public:

    FormVideoBox(wxPanel* parent,wxWindowID win_id,SpellData* spell_data,std::string name,int zoom=2);
    ~FormVideoBox();

private:
    SpellData* m_spelldata;
    SpellSound* m_sound;
    wxBitmap* m_frame;
    SpellVideo *m_video;
#ifdef _WIN32
    IMFPMediaPlayer* m_mf_player = nullptr;
    FormVideoMfCallback* m_mf_callback = nullptr;
    wxWindow* m_media_host = nullptr;
    bool m_using_mf = false;
    bool m_com_uninit = false;
    void NotifyMfPlaybackDone(long hr);
    void OnMediaHostPaint(wxPaintEvent& event);
    void OnMediaHostSize(wxSizeEvent& event);
    friend class FormVideoMfCallback;
#endif
    int m_frame_id;
    int m_zoom;
    
    wxWindow* form;
    
    wxPoint m_click_pos;
    int m_in_move;

    void StopPlayback();

    void OnClose(wxCloseEvent& ev);
    void OnPaintTab(wxPaintEvent& event);    
    void OnKeyPress(wxKeyEvent& event);
    void OnLeaveWin(wxMouseEvent& event);
    void OnWinClick(wxMouseEvent& event);    
    void OnWinMouseMove(wxMouseEvent& event);    

    void cbNewAudioFrame(void);
    void OnNewAudioFrame(wxThreadEvent& event);

    bool m_closing = false;       // aby se Close() nevolalo opakovan
    bool m_done_sent = false;     // aby se parentovi neposlal done event vckrt

};

