///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 3.10.1-0-g8feb16b3)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "form_loader.h"
#include "app_identity.h"
#include <wx/stdpaths.h>

#include <filesystem>
#include <vector>

#include "other.h"
#include "simpleini.h"

namespace
{
	std::filesystem::path ResolveConfiguredPath(const char* value,
		const std::filesystem::path& config_path,
		const std::filesystem::path& executable_dir)
	{
		namespace fs = std::filesystem;
		if (!value || !*value)
			return {};

		const fs::path configured = char2wstring(value);
		if (configured.is_absolute())
			return configured.lexically_normal();

		std::vector<fs::path> roots = {
			fs::current_path(),
			config_path.parent_path(),
			executable_dir
		};

		for (fs::path dir : { config_path.parent_path(), executable_dir })
		{
			for (;;)
			{
				roots.push_back(dir);
				const fs::path parent = dir.parent_path();
				if (parent.empty() || parent == dir)
					break;
				dir = parent;
			}
		}

		std::error_code ec;
		for (const auto& root : roots)
		{
			const fs::path candidate = (root / configured).lexically_normal();
			if (fs::exists(candidate, ec) && !ec)
				return candidate;
			ec.clear();
		}

		// Keep a deterministic path in diagnostics even when the item is missing.
		return (fs::current_path() / configured).lexically_normal();
	}
}


///////////////////////////////////////////////////////////////////////////

FormLoader::FormLoader(wxWindow* parent,SpellData *&spell_data, wstring config_path, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : wxDialog( parent, id, title, pos, size, style )
{
    spellcross_app::ApplyWindowIcon(this);
	// === AUTO GENERATER START ===
	
	this->SetSizeHints(wxDefaultSize,wxDefaultSize);

	wxBoxSizer* szrLoader;
	szrLoader = new wxBoxSizer(wxVERTICAL);

	m_staticText63 = new wxStaticText(this,wxID_ANY,wxT("Progress:"),wxDefaultPosition,wxDefaultSize,0);
	m_staticText63->Wrap(-1);
	szrLoader->Add(m_staticText63,0,wxTOP|wxRIGHT|wxLEFT,5);

	txtList = new wxTextCtrl(this,wxID_TXT_LIST,wxEmptyString,wxDefaultPosition,wxDefaultSize,wxTE_MULTILINE|wxTE_READONLY|wxTE_WORDWRAP);
	szrLoader->Add(txtList,1,wxEXPAND|wxBOTTOM|wxRIGHT|wxLEFT,5);

	m_staticText62 = new wxStaticText(this,wxID_ANY,wxT("Loading item:"),wxDefaultPosition,wxDefaultSize,0);
	m_staticText62->Wrap(-1);
	szrLoader->Add(m_staticText62,0,wxTOP|wxRIGHT|wxLEFT,5);

	txtItem = new wxTextCtrl(this,wxID_TXT_ITEM,wxEmptyString,wxDefaultPosition,wxDefaultSize,0);
	szrLoader->Add(txtItem,0,wxEXPAND|wxBOTTOM|wxRIGHT|wxLEFT,5);

	btnOK = new wxButton(this,wxID_BTN_OK,wxT("EXIT"),wxDefaultPosition,wxDefaultSize,0);
	szrLoader->Add(btnOK,0,wxALL|wxEXPAND,5);

	this->SetSizer(szrLoader);
	this->Layout();

	this->Centre(wxBOTH);
	
	// === AUTO GENERATER END ===

	Bind(wxEVT_TEXT,&FormLoader::OnRefreshItem,this,wxID_TXT_ITEM);
	Bind(wxEVT_TEXT,&FormLoader::OnRefreshList,this,wxID_TXT_LIST);
	Bind(wxEVT_COMMAND_BUTTON_CLICKED,&FormLoader::OnExitClick,this,wxID_BTN_OK);
	Bind(wxEVT_CLOSE_WINDOW,&FormLoader::OnClose,this);

	// by default hide exist button
	btnOK->Enable(false);

	// initiate loader in its own thread
	loader = new std::thread(&FormLoader::Loader,this,config_path,std::ref(spell_data));
	
}

FormLoader::~FormLoader()
{
	
}

void FormLoader::OnClose(wxCloseEvent& ev)
{	
	// cleanup worker thread
	loader->join();
	delete loader;

	// passed?
	bool ok = !ev.GetEventObject();

	// return state
	EndModal(ok);
}

// manual panel close click
void FormLoader::OnExitClick(wxCommandEvent &event)
{
	Close();
}


// -------------------------------------------------------------------------------------------------
// Data loader thread
// -------------------------------------------------------------------------------------------------
void FormLoader::Loader(std::wstring config_path,SpellData* &spell_data)
{
	spell_data = NULL;
	
	// try load config.ini
	CSimpleIniA ini;
	ini.SetUnicode();
	if(ini.LoadFile(config_path.c_str()) != SI_OK)
	{
		UpdateList("Loading INI filed faild!");
		LoaderExit(true);
		return;
	}

	const std::filesystem::path config_file = std::filesystem::absolute(config_path).lexically_normal();
	const std::filesystem::path exe_path =
		std::filesystem::path(::wxStandardPaths::Get().GetExecutablePath().ToStdWstring()).parent_path();

	// Legacy directory roots are kept as fallbacks for old configs and for
	// dynamic resources (videos, ad-hoc archive lookup). The actual core data
	// archives are now resolved individually from [FILES].
	wstring spelldata_path = ResolveConfiguredPath(ini.GetValue("SPELCROS", "spell_path", ""), config_file, exe_path).wstring();
	wstring spellcd_path = ResolveConfiguredPath(ini.GetValue("SPELCROS", "spellcd_path", ""), config_file, exe_path).wstring();
	wstring spec_folder = ResolveConfiguredPath(ini.GetValue("DATA", "spec_data_path", ""), config_file, exe_path).wstring();
	wstring units_aux_data_path = ResolveConfiguredPath(ini.GetValue("DATA", "units_aux_data_path", ""), config_file, exe_path).wstring();

	auto resolveSource = [&](const char* key, const std::wstring& legacyRoot, const wchar_t* legacyName, bool optional=false) -> std::wstring
	{
		const char* configured = ini.GetValue("FILES", key, "");
		if(configured && *configured)
			return ResolveConfiguredPath(configured, config_file, exe_path).wstring();
		if(optional)
			return {};
		if(!legacyRoot.empty())
			return (std::filesystem::path(legacyRoot) / legacyName).wstring();
		return {};
	};

	SpellDataFiles files;
	files.data_root = spelldata_path;
	files.cd_root = spellcd_path;
	files.common_fs = resolveSource("common_fs", spelldata_path, L"COMMON.FS");
	files.terrain_t11_fs = resolveSource("t11_fs", spelldata_path, L"T11.FS");
	files.terrain_pust_fs = resolveSource("pust_fs", spelldata_path, L"PUST.FS");
	files.terrain_devast_fs = resolveSource("devast_fs", spelldata_path, L"DEVAST.FS");
	files.units_fsu = resolveSource("units_fsu", spelldata_path, L"UNITS.FSU");
	files.texts_fs = resolveSource("texts_fs", spelldata_path, L"TEXTS.FS");
	files.info_fs = resolveSource("info_fs", spellcd_path, L"INFO.FS");
	files.samples_fs = resolveSource("samples_fs", spelldata_path, L"SAMPLES.FS", true);
	files.music_fs = resolveSource("music_fs", spelldata_path, L"MUSIC.FS", true);

	if(files.data_root.empty() && !files.common_fs.empty())
		files.data_root = std::filesystem::path(files.common_fs).parent_path().wstring();
	if(files.cd_root.empty() && !files.info_fs.empty())
		files.cd_root = std::filesystem::path(files.info_fs).parent_path().wstring();

	UpdateList("Resolved runtime sources:");
	UpdateList(string_format(" - config: %ls", config_file.wstring().c_str()));
	UpdateList(string_format(" - COMMON.FS: %ls", files.common_fs.c_str()));
	UpdateList(string_format(" - UNITS.FSU: %ls", files.units_fsu.c_str()));
	UpdateList(string_format(" - TEXTS.FS: %ls", files.texts_fs.c_str()));
	UpdateList(string_format(" - INFO.FS: %ls", files.info_fs.c_str()));
	UpdateList(string_format(" - SAMPLES.FS: %ls", files.samples_fs.empty() ? L"<disabled>" : files.samples_fs.c_str()));
	UpdateList(string_format(" - MUSIC.FS: %ls", files.music_fs.empty() ? L"<disabled>" : files.music_fs.c_str()));
	UpdateList(string_format(" - program data: %ls", spec_folder.c_str()));

	// try load spellcross data
	try{
		spell_data = new SpellData(files,spec_folder,bind(&FormLoader::UpdateList,this,placeholders::_1),bind(&FormLoader::UpdateItem,this,placeholders::_1));
	}catch(const runtime_error& error){
		UpdateList(std::string(error.what()));
		LoaderExit(true);
		return;
	}
	
	// try load units.fsu aux metadata (optional)
	UpdateList("Loading units aux data...");
	if (spell_data->units_fsu->LoadAuxData(units_aux_data_path))
	{
		// OPTIONAL: don't kill the whole loader, just continue
		UpdateList(string_format(" - missing/failed, continuing without aux data (''%ls'')", units_aux_data_path.c_str()));

		// pokud máš nějaký flag, nastav ho:
		// spell_data->units_fsu->has_aux_data = false;

		// a hlavně NEVOLAT:
		// delete spell_data;
		// spell_data = NULL;
		// LoaderExit(true);
		// return;
	}
	else
	{
		UpdateList(" - OK");
	}


	// for each terrain load tile context
	UpdateList("Loading terrain context data...");
	for(auto & terr : spell_data->terrain)
	{
		UpdateList(string_format(" - loading ''%s''...",terr->name.c_str()));

		// make INI section
		string sec_name = "TERRAIN::" + terr->name;

		// try to load context
		wstring cont_path = ResolveConfiguredPath(ini.GetValue(sec_name.c_str(), "context_path", ""), config_file, exe_path).wstring();
		if(terr->InitSpriteContext(cont_path))
		{
			UpdateList(string_format("   - context ''%ls'' not found...",cont_path.c_str()));
		}
		// try add special tools
		terr->AddSpecialTools();
	}			

	// exit
	LoaderExit();
}

// end loader
void FormLoader::LoaderExit(bool hold)
{	
	// show manual exit button
	btnOK->Enable(true);	
	SetWindowStyle(GetWindowStyle() | wxCLOSE_BOX);
	
	// optional auto-exit command?
	if(!hold)
	{
		wxCommandEvent* evt = new wxCommandEvent(wxEVT_CLOSE_WINDOW);
		wxQueueEvent(this,evt);
	}
}

// update actual item info
void FormLoader::UpdateItem(std::string text)
{
	wxCommandEvent* evt = new wxCommandEvent(wxEVT_TEXT);
	evt->SetId(wxID_TXT_ITEM);
	evt->SetClientData(new std::string(text));
	wxQueueEvent(this,evt);
}

// update progress list
void FormLoader::UpdateList(std::string text)
{
	wxCommandEvent* evt = new wxCommandEvent(wxEVT_TEXT);
	evt->SetId(wxID_TXT_LIST);
	evt->SetClientData(new std::string(text));
	wxQueueEvent(this,evt);
}


// on progress/item update
void FormLoader::OnRefreshItem(wxCommandEvent& event)
{
	auto *str = (std::string*)event.GetClientData();
	txtItem->ChangeValue(*str);
	delete str;
}
void FormLoader::OnRefreshList(wxCommandEvent& event)
{
	auto* str = (std::string*)event.GetClientData();
	txtList->ChangeValue(txtList->GetValue() + *str + "\n");
	txtList->ShowPosition(txtList->GetLastPosition());
	delete str;
}

