#include "form_video_box.h"
#include "fs_archive.h"
#include "other.h"

#include <wx/rawbmp.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/log.h>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include <cctype>

#ifdef _WIN32
#ifndef WINVER
#define WINVER 0x0601
#endif
#include <windows.h>
#include <mfplay.h>
#include <mferror.h>
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace
{
std::string UpperAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

std::filesystem::path FindBundledMovieFallback(SpellData* spell_data, const std::string& stem)
{
    namespace fs = std::filesystem;
    const std::string fallback_name = UpperAscii(stem) + ".mp4";
    std::vector<fs::path> roots;

    const fs::path exe_dir = fs::path(wxStandardPaths::Get().GetExecutablePath().ToStdWstring()).parent_path();
    roots.push_back(exe_dir / "data" / "movie_fallback");
    roots.push_back(fs::current_path() / "data" / "movie_fallback");
    roots.push_back(fs::current_path() / ".." / "data" / "movie_fallback");

    if (spell_data)
    {
        if (!spell_data->data_path.empty())
            roots.push_back(fs::path(spell_data->data_path) / "movie_fallback");
        if (!spell_data->cd_data_path.empty())
            roots.push_back(fs::path(spell_data->cd_data_path) / "movie_fallback");
    }

    for (const auto& root : roots)
    {
        std::error_code ec;
        const fs::path candidate = root / fallback_name;
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
            return candidate;
    }
    return {};
}

bool TryLoadLegacyVideo(SpellData* spell_data, const std::string& entry_name, SpellVideo*& out_video, std::string& error)
{
    namespace fs = std::filesystem;
    if (!spell_data)
    {
        error = "SpellData not available";
        return false;
    }

    const fs::path requested(entry_name);
    const std::string ext = UpperAscii(requested.extension().string());
    const std::string stem = UpperAscii(requested.stem().string());

    std::vector<std::wstring> archive_candidates;
    if (ext == ".DP2")
    {
        archive_candidates.push_back((fs::path(spell_data->cd_data_path) / "SPEAKER.FS").wstring());
        archive_candidates.push_back((fs::path(spell_data->data_path) / "SPEAKER.FS").wstring());
        archive_candidates.push_back((fs::path("temp") / "SPEAKER.FS").wstring());
    }
    else
    {
        archive_candidates.push_back((fs::path(spell_data->cd_data_path) / "MOVIE.FS").wstring());
        archive_candidates.push_back((fs::path(spell_data->data_path) / "MOVIE.FS").wstring());
        archive_candidates.push_back((fs::path("temp") / "MOVIE.FS").wstring());
        archive_candidates.push_back((fs::path("temp") / "MOVIE" / "MOVIE.FS").wstring());
        archive_candidates.push_back(fs::path("MOVIE.FS").wstring());
    }

    std::wstring archive_path;
    for (const auto& c : archive_candidates)
    {
        std::error_code ec;
        if (fs::exists(c, ec))
        {
            archive_path = c;
            break;
        }
    }
    if (archive_path.empty())
    {
        error = "source MOVIE.FS/SPEAKER.FS archive not found";
        return false;
    }

    std::vector<std::string> entries;
    if (ext == ".CAN" || ext == ".DPK" || ext == ".DP2")
        entries.push_back(requested.filename().string());
    else if (ext == ".SMK")
    {
        // EN-style level definitions may name Maslan's SMK conversion.  If the
        // fallback pack is absent, try the corresponding original CZ entry.
        entries.push_back(stem + ".CAN");
        entries.push_back(stem + ".DPK");
    }

    if (entries.empty())
    {
        error = "unsupported legacy video extension";
        return false;
    }

    try
    {
        FSarchive fs(archive_path);
        for (const auto& entry : entries)
        {
            try
            {
                out_video = new SpellVideo(&fs, entry);
                wxLogDebug("[VIDEO] original archive: %s (requested %s)", entry.c_str(), entry_name.c_str());
                return true;
            }
            catch (const std::exception& ex)
            {
                delete out_video;
                out_video = nullptr;
                error = ex.what();
            }
        }
    }
    catch (const std::exception& ex)
    {
        error = ex.what();
    }
    return false;
}
}

#ifdef _WIN32
class FormVideoMfCallback final : public IMFPMediaPlayerCallback
{
public:
    explicit FormVideoMfCallback(FormVideoBox* owner) : m_owner(owner) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == __uuidof(IMFPMediaPlayerCallback))
        {
            *ppv = static_cast<IMFPMediaPlayerCallback*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return (ULONG)InterlockedIncrement(&m_ref);
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG ref = (ULONG)InterlockedDecrement(&m_ref);
        if (!ref)
            delete this;
        return ref;
    }

    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event) override
    {
        FormVideoBox* owner = m_owner;
        if (!owner || !event)
            return;

        if (FAILED(event->hrEvent))
        {
            owner->NotifyMfPlaybackDone((long)event->hrEvent);
            return;
        }
        if (event->eEventType == MFP_EVENT_TYPE_PLAYBACK_ENDED)
            owner->NotifyMfPlaybackDone((long)S_OK);
    }

    void Detach() { m_owner = nullptr; }

private:
    ~FormVideoMfCallback() = default;
    LONG m_ref = 1;
    FormVideoBox* m_owner = nullptr;
};
#endif

// load video and make form
FormVideoBox::FormVideoBox(wxPanel* parent,wxWindowID win_id,SpellData* spell_data,std::string name,int zoom)
{
    m_spelldata = spell_data;
    m_sound = nullptr;
    m_frame = nullptr;
    m_video = nullptr;
    form = nullptr;
    m_frame_id = -1;
    m_zoom = zoom;
    m_in_move = false;
    m_closing = false;
    m_done_sent = false;

    namespace fs = std::filesystem;
    const fs::path requested(name);
    const std::string entry_name = requested.filename().string();
    const std::string ext = UpperAscii(requested.extension().string());
    const std::string stem = requested.stem().string();
    const fs::path fallback_path = FindBundledMovieFallback(spell_data, stem);

    // The CZ CAN picture codec is the one format the restoration decoder does
    // not understand.  Maslan captured those sequences from the CZ game and
    // converted them to SMK2 for the EN engine.  This build ships modern MP4
    // transcodes of those exact conversions and prefers them for CAN/SMK.
    bool use_fallback = false;
    std::string legacy_error;
    if ((ext == ".CAN" || ext == ".SMK") && !fallback_path.empty())
    {
        use_fallback = true;
    }
    else
    {
        TryLoadLegacyVideo(spell_data, entry_name, m_video, legacy_error);
        if (!m_video && !fallback_path.empty())
            use_fallback = true;
    }

    // Last chance: if a requested CAN has no converted asset, retain the old
    // audio-only path instead of silently dropping the cutscene altogether.
    if (!m_video && !use_fallback && (ext == ".CAN" || ext == ".SMK"))
        TryLoadLegacyVideo(spell_data, entry_name, m_video, legacy_error);

#ifndef _WIN32
    // The game-mode target is Windows.  On other platforms keep the legacy
    // decoder behaviour; MP4 fallback playback uses the native Windows MFPlay.
    if (use_fallback)
        use_fallback = false;
#endif

    if (!m_video && !use_fallback)
    {
        throw runtime_error(string_format("Loading video file \"%s\" failed (%s)%s%s",
            entry_name.c_str(), legacy_error.c_str(),
            fallback_path.empty() ? "; no bundled fallback " : "; fallback exists but cannot be used on this platform: ",
            fallback_path.empty() ? (UpperAscii(stem) + ".mp4").c_str() : fallback_path.string().c_str()));
    }

    int frame_x = 320;
    int frame_y = 200;
    if (m_video)
        std::tie(frame_x, frame_y) = m_video->GetResolution();

    // frame border graphics
    auto corn = m_spelldata->gres.wm_frame_corner;
    auto horz = m_spelldata->gres.wm_frame_horz;
    auto vert = m_spelldata->gres.wm_frame_vert;

    const int x_size = frame_x*m_zoom + 2*corn->x_size;
    const int y_size = frame_y*m_zoom + 2*corn->y_size;

    // Render the original Spellcross frame chrome once.  The MF video host is
    // a child window placed inside this border; legacy video is painted into
    // the same central rectangle by OnPaintTab().
    vector<uint8_t> buf(x_size*y_size,0x00);
    int pos_x = corn->x_size;
    do {
        horz->Render(&buf[0],&buf[0] + x_size*y_size,x_size,pos_x,0);
        horz->Render(&buf[0],&buf[0] + x_size*y_size,x_size,pos_x,y_size - horz->y_size);
        pos_x += horz->x_size;
    } while(pos_x < x_size);
    int pos_y = 10;
    do {
        vert->Render(&buf[0],&buf[0] + x_size*y_size,x_size,0,pos_y);
        vert->Render(&buf[0],&buf[0] + x_size*y_size,x_size,x_size - corn->x_size,pos_y);
        pos_y += vert->y_size;
    } while(pos_y < y_size);
    corn->Render(&buf[0],&buf[0] + x_size*y_size,x_size,0,0);
    corn->Render(&buf[0],&buf[0] + x_size*y_size,x_size,x_size - corn->x_size,0);
    corn->Render(&buf[0],&buf[0] + x_size*y_size,x_size,0,y_size - corn->y_size);
    corn->Render(&buf[0],&buf[0] + x_size*y_size,x_size,x_size - corn->x_size,y_size - corn->y_size);

    m_frame = new wxBitmap(x_size,y_size,24);
    uint8_t* ptr = &buf[0];
    uint8_t (*pal)[3] = horz->pal;
    wxNativePixelData pdata(*m_frame);
    wxNativePixelData::Iterator p(pdata);
    for(int y = 0; y < y_size; y++)
    {
        uint8_t* scan = p.m_ptr;
        for(int x = 0; x < x_size; x++)
        {
            *scan++ = pal[*ptr][2];
            *scan++ = pal[*ptr][1];
            *scan++ = pal[*ptr][0];
            ptr++;
        }
        p.OffsetY(pdata,1);
    }

    const wxPoint pos = {(parent->GetSize().x - x_size)/2, (parent->GetSize().y - y_size)/2};
    const wxSize size ={x_size, y_size};
    form = new wxWindow(parent,win_id,pos,size,wxBORDER_NONE|wxSTAY_ON_TOP);
    form->SetBackgroundStyle(wxBG_STYLE_PAINT);
    form->SetDoubleBuffered(true);

    form->Bind(wxEVT_CLOSE_WINDOW,&FormVideoBox::OnClose,this,win_id);
    form->Bind(wxEVT_PAINT,&FormVideoBox::OnPaintTab,this,win_id);
    form->Bind(wxEVT_KEY_UP,&FormVideoBox::OnKeyPress,this,win_id);
    form->Bind(wxEVT_LEAVE_WINDOW,&FormVideoBox::OnLeaveWin,this,win_id);
    form->Bind(wxEVT_LEFT_DOWN,&FormVideoBox::OnWinClick,this,win_id);
    form->Bind(wxEVT_LEFT_UP,&FormVideoBox::OnWinClick,this,win_id);
    form->Bind(wxEVT_MOTION,&FormVideoBox::OnWinMouseMove,this,win_id);
    form->Bind(wxEVT_THREAD,&FormVideoBox::OnNewAudioFrame,this);

#ifdef _WIN32
    if (use_fallback)
    {
        m_using_mf = true;
        m_media_host = new wxWindow(form, wxID_ANY,
            wxPoint(corn->x_size, corn->y_size),
            wxSize(frame_x*m_zoom, frame_y*m_zoom), wxBORDER_NONE);
        m_media_host->SetBackgroundColour(*wxBLACK);
        m_media_host->Bind(wxEVT_PAINT, &FormVideoBox::OnMediaHostPaint, this);
        m_media_host->Bind(wxEVT_SIZE, &FormVideoBox::OnMediaHostSize, this);
        m_media_host->Bind(wxEVT_KEY_UP, &FormVideoBox::OnKeyPress, this);

        // MFPlay is a COM-based Windows component.  wxWidgets applications
        // normally already have COM available, but do not rely on that.
        // S_OK/S_FALSE must be balanced by CoUninitialize; RPC_E_CHANGED_MODE
        // means COM already exists in another apartment and is still usable.
        const HRESULT co_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(co_hr))
            m_com_uninit = true;
        else if (co_hr != RPC_E_CHANGED_MODE)
        {
            form->Destroy();
            form = nullptr;
            delete m_frame;
            m_frame = nullptr;
            throw runtime_error(string_format("Cannot initialize COM for movie playback (HRESULT 0x%08lx).",
                (unsigned long)co_hr));
        }

        m_mf_callback = new FormVideoMfCallback(this);
        const HRESULT hr = MFPCreateMediaPlayer(
            fallback_path.wstring().c_str(),
            TRUE,                   // auto-start once the local MP4 is opened
            0,
            m_mf_callback,
            reinterpret_cast<HWND>(m_media_host->GetHandle()),
            &m_mf_player);

        if (FAILED(hr) || !m_mf_player)
        {
            wxLogMessage("[VIDEO] MFPlay failed for %s (HRESULT 0x%08lx)", fallback_path.string().c_str(), (unsigned long)hr);
            if (m_mf_callback)
                m_mf_callback->Detach();
            if (m_mf_player)
            {
                m_mf_player->Release();
                m_mf_player = nullptr;
            }
            if (m_mf_callback)
            {
                m_mf_callback->Release();
                m_mf_callback = nullptr;
            }
            if (m_media_host)
            {
                m_media_host->Destroy();
                m_media_host = nullptr;
            }
            if (m_com_uninit)
            {
                CoUninitialize();
                m_com_uninit = false;
            }
            form->Destroy();
            form = nullptr;
            delete m_frame;
            m_frame = nullptr;
            throw runtime_error(string_format("Cannot play bundled movie fallback %s (MFPlay HRESULT 0x%08lx).",
                fallback_path.string().c_str(), (unsigned long)hr));
        }
        wxLogDebug("[VIDEO] MP4 fallback: %s -> %s", entry_name.c_str(), fallback_path.string().c_str());
    }
#endif

    if (!use_fallback)
    {
        m_sound = new SpellSound(m_spelldata->sounds->channels, m_video->GetSound());
        m_sound->Play(false,false,bind(&FormVideoBox::cbNewAudioFrame,this),0.010);
    }

    form->SetFocus();
}

// cleanup
FormVideoBox::~FormVideoBox()
{           
    // optional kill narration sound playback
    StopPlayback();
    
    // loose video data
    delete m_video;
    m_video = NULL;
    
    // loose temp frame
    delete m_frame;
    m_frame = NULL;

    if (form)
    {
        form->Unbind(wxEVT_CLOSE_WINDOW, &FormVideoBox::OnClose, this);
        form->Unbind(wxEVT_THREAD, &FormVideoBox::OnNewAudioFrame, this);
    }

    if (form)
    {
        form->Destroy();
        form = NULL;
    }

}

// callback when new audio frame is submitted (time critical!), called from another thread, so no touchy to GUI here!
void FormVideoBox::cbNewAudioFrame(void)
{
    if (!form || m_closing)   // <- dleit, a to po Close u nesype eventy
        return;

    wxThreadEvent* evt = new wxThreadEvent();
    wxQueueEvent(form, evt);
}

// on new audio frame (GUI thread event)
void FormVideoBox::OnNewAudioFrame(wxThreadEvent& event)
{
#ifdef _WIN32
    if (m_using_mf)
    {
        if (!m_closing && form)
        {
            m_closing = true;
            form->Close();
        }
        return;
    }
#endif
    if(!m_sound || !m_video)
        return;

    // get playback time
    double time = m_sound->GetPlaybackTime();

    // update video frame
    int new_frame = m_video->GetFrameID(time);
    if(m_frame_id != new_frame)
    {
        m_frame_id = new_frame;
        form->Refresh();
    }

    if (m_sound && m_sound->isDone() && !m_closing)
    {
        m_closing = true;
        form->Close(); // vyvol OnClose prv jednou
    }

}

// stop audio playback
void FormVideoBox::StopPlayback()
{
#ifdef _WIN32
    if (m_mf_callback)
        m_mf_callback->Detach();
    if (m_mf_player)
    {
        m_mf_player->Stop();
        m_mf_player->Shutdown();
        m_mf_player->Release();
        m_mf_player = nullptr;
    }
    if (m_mf_callback)
    {
        m_mf_callback->Release();
        m_mf_callback = nullptr;
    }
    m_using_mf = false;
    if (m_com_uninit)
    {
        CoUninitialize();
        m_com_uninit = false;
    }
#endif
    if(m_sound)
    {
        // Stop is bounded.  If the audio backend does not acknowledge it, do
        // not free the callback owner out from under RtAudio; leaking this one
        // shutdown-path object is safer than a use-after-free and the process
        // is terminating anyway.
        if(m_sound->Stop(3.0) == 0)
            delete m_sound;
    }
    m_sound = NULL;
}

//// funkcni blok pro ukonceni formu (a posilani zpravy rodici)
//void FormVideoBox::OnClose(wxCloseEvent& ev)
//{       
//    // terminate (and send message to parent)
//    form->DeletePendingEvents();
//    wxQueueEvent(form->GetParent(),new wxCloseEvent(ev));    
//}

void FormVideoBox::OnClose(wxCloseEvent& ev)
{
    m_closing = true;

    // nechceme to poslat 2x (Destroy / dal close / cokoliv)
    if (m_done_sent)
    {
        ev.Skip(false);
        return;
    }
    m_done_sent = true;

    // okamit usekni audio + psun thread event
    StopPlayback();
    form->Unbind(wxEVT_THREAD, &FormVideoBox::OnNewAudioFrame, this);

    // signal parentovi "cutscene done"
    wxCloseEvent* done = new wxCloseEvent(wxEVT_CLOSE_WINDOW);
    done->SetId(form->GetId());     // ID_VIDEO_BOX_WIN
    done->SetEventObject(form);
    wxQueueEvent(form->GetParent(), done);

    // schovej okno; reln Destroy udl destruktor wrapperu
    form->Hide();

    // nedovol defaultn close, jinak se ti to me rozbt poadm destroy
    ev.Skip(false);
}


void FormVideoBox::OnPaintTab(wxPaintEvent& event)
{           
    // make local frame buffer
    int x_size = form->GetSize().x;
    int y_size = form->GetSize().y;
    
    // frame border graphics
    auto corn = m_spelldata->gres.wm_frame_corner;
    auto horz = m_spelldata->gres.wm_frame_horz;
    auto vert = m_spelldata->gres.wm_frame_vert;
    
    if(m_frame && m_video && m_video->GetFramesCount() && m_frame_id >= 0 && m_frame_id < m_video->GetFramesCount() && !m_video->isCAN())
    {
        // get frame size
        auto [frame_x,frame_y] = m_video->GetResolution();

        // render 24bit RGB data to raw bmp buffer		
        uint8_t *buf = m_video->GetFrame(m_frame_id);
        uint8_t *pal = m_video->GetPalette();
        wxNativePixelData pdata(*m_frame);
        wxNativePixelData::Iterator p(pdata);
        p.OffsetY(pdata,corn->y_size);
        int y_chunk = 0;
        for(int y = 0; y < frame_y*m_zoom; y++)
        {
            uint8_t* scan = &p.m_ptr[3*corn->x_size];
            int x_chunk = 0;
            for(int x = 0; x < frame_x*m_zoom; x++)
            {
                uint8_t pix = *buf;
                if(++x_chunk >= m_zoom)
                {
                    buf++;
                    x_chunk = 0;
                }
                *scan++ = pal[pix*3 + 2];
                *scan++ = pal[pix*3 + 1];
                *scan++ = pal[pix*3 + 0];
            }
            if(++y_chunk < m_zoom)
                buf -= frame_x;
            else
                y_chunk = 0;
            p.OffsetY(pdata,1);
        }

    }
   
    // blit image to screen
    wxPaintDC pdc(form);
    pdc.DrawBitmap(*m_frame,wxPoint(0,0));
}

#ifdef _WIN32
void FormVideoBox::NotifyMfPlaybackDone(long hr)
{
    if (FAILED((HRESULT)hr))
        wxLogMessage("[VIDEO] MFPlay playback error: HRESULT 0x%08lx", (unsigned long)hr);
    if (!form || m_closing)
        return;
    wxThreadEvent* evt = new wxThreadEvent();
    evt->SetInt((int)hr);
    wxQueueEvent(form, evt);
}

void FormVideoBox::OnMediaHostPaint(wxPaintEvent& event)
{
    wxPaintDC dc(m_media_host);
    if (m_mf_player)
        m_mf_player->UpdateVideo();
}

void FormVideoBox::OnMediaHostSize(wxSizeEvent& event)
{
    if (m_mf_player)
        m_mf_player->UpdateVideo();
    event.Skip();
}
#endif

void FormVideoBox::OnWinClick(wxMouseEvent& event)
{    
    if(event.LeftDown())
    {
        //form->CaptureMouse();
        m_click_pos = event.GetPosition() + form->GetPosition();
        m_in_move = true;
    }
    else if(event.LeftUp())
    {
        //form->ReleaseMouse();
        m_in_move = false;
    }    
}
void FormVideoBox::OnWinMouseMove(wxMouseEvent& event)
{
    if(m_in_move)
    {
        auto new_pos = event.GetPosition() + form->GetPosition();
        auto delta = new_pos - m_click_pos;
        form->SetPosition(form->GetPosition() + delta);
        m_click_pos = new_pos;
        form->Refresh();
    }
}

void FormVideoBox::OnKeyPress(wxKeyEvent& event)
{
    // terminate
    if(event.GetKeyCode() == WXK_ESCAPE)
        form->Close();
}

void FormVideoBox::OnLeaveWin(wxMouseEvent& event)
{
    // keep focus
    form->SetFocus();
}
