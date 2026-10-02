#pragma once

#include "app_version.h"

#include <wx/icon.h>
#include <wx/string.h>
#include <wx/toplevel.h>

namespace spellcross_app
{
    inline wxString Version()
    {
        return wxString::FromUTF8(SPELLCROSS_VERSION_STRING);
    }

    inline wxString VersionLabel()
    {
        return wxString::FromUTF8("v" SPELLCROSS_VERSION_STRING);
    }

    inline void ApplyWindowIcon(wxTopLevelWindow* window)
    {
#ifdef __WXMSW__
        if (!window)
            return;

        wxIcon icon;
        icon.LoadFile("IDI_ICON2", wxBITMAP_TYPE_ICO_RESOURCE);
        if (icon.IsOk())
            window->SetIcon(icon);
#else
        (void)window;
#endif
    }
}
