/* gui_ext.cpp
This is a simple set of predefined GUI windows for SciTE,
built using the YAWL library.
Steve Donovan, 2007.
  */
#include <windows.h>
#include <string>
#include <vector>
#include <io.h>
#include <direct.h>
#include <memory>
#include "lua.hpp"
#include "utf.h"
#include "twl.h"
#include "twl_dialogs.h"
#include "twl_modal.h"
#include "twl_listview.h"
#include "twl_tab.h"
#include "twl_splitter.h"
#include "twl_treeview.h"

#define output(x) lua_pushstring(L, (x)); OutputMessage(L);

typedef std::wstring gui_string;
#define EQ(s1,s2) (wcscmp((s1),(s2))==0)

const char* WINDOW_CLASS = "WINDOW*";
const int BUFFER_SIZE = 2 * 0xFFFF;

static const char* LIB_VERSION = "0.1";
static TWin* s_parent = NULL;
static TWin* s_last_parent = NULL;
static HWND hSciTE = NULL, hContent = NULL, hCode;
static WNDPROC old_scite_proc, old_scintilla_proc, old_content_proc;
static lua_State* sL;
static TWin* code_window = NULL;
static TWin* extra_window = NULL;
static TWin* content_window = NULL;
static TWin* extra_window_splitter = NULL;
static bool forced_resize = false;
static Rect m, cwb, extra;

class PaletteWindow;

TWin* get_parent()
{
	return s_parent;
}

TWin* get_last_parent()
{
	return s_last_parent;
}

TWin* get_desktop_window()
{
	static TWin* desk = NULL;
	if (desk == NULL) {
		desk = new TWin(GetDesktopWindow());
	}
	return desk;
}

//  print version 
static int do_version(lua_State* L)
{
	lua_getglobal(L, "print");
	lua_pushstring(L, LIB_VERSION);
	lua_pcall(L, 1, 0, 0);
	return 0;
}

void destroy_windows()
{
	if (extra_window) {
		extra_window->hide();
		extra_window->set_parent(NULL);
		extra_window->close();
		delete extra_window;
		extra_window = nullptr;
	}
	if (extra_window_splitter) {
		extra_window_splitter->hide();
		extra_window_splitter->set_parent(NULL);
		extra_window_splitter->close();
		delete extra_window_splitter;
		extra_window_splitter = nullptr;
	}
	extra.bottom = extra.top = extra.left = extra.right = 0;
	//shake_scite_descendants();
}

// show a message on the SciTE output window
void OutputMessage(lua_State* L)
{
	if (lua_isstring(L, -1)) {
		std::string msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		lua_getglobal(L, "output");
		lua_getfield(L, -1, "AddText");
		lua_insert(L, -2);
		lua_pushstring(L, msg.append("\n").data());
		lua_pcall(L, 2, 0, 0);
	}
}

static int call_named_function(lua_State* L, const char* name, lua_Integer arg)
{
	int ret = 0;
	lua_getglobal(L, name);
	if (lua_isfunction(L, -1)) {
		lua_pushinteger(L, arg);
		if (lua_pcall(L, 1, 1, 0)) {
			OutputMessage(L);
		}
		else {
			ret = lua_gettop(L)?lua_toboolean(L, -1):0;
		}
	}
	lua_pop(L, 1);
	return 0;

}

static bool eq(const Rect& r1, const Rect& r2)
{
	return r1.left == r2.left && r1.top == r2.top && r1.right == r2.right && r1.bottom == r2.bottom;
}

static void force_contents_resize()
{
	// get the code pane's extents, and don't try to resize it again!
	code_window->get_rect(m, true);
	if (eq(cwb, m)) return;
	int oldw = m.width();
	int w = extra_window->width();
	int h = m.height();
	int sw = extra_window_splitter->width();
	extra = m;
	cwb = m;
	if (extra_window->align() == alLeft) {
		// on the left goes the extra pane, followed by the splitter
		extra.right = extra.left + w;
		extra_window->resize(m.left, m.top, w, h);
		extra_window_splitter->resize(m.left + w, m.top, sw, h);
		cwb.left += w + sw;
	}
	else {
		int margin = m.right - w;
		extra.left = margin;
		extra_window->resize(margin, m.top, w, h);
		extra_window_splitter->resize(margin - sw, m.top, sw, h);
		cwb.right -= w + sw;
	}
	// and then the code pane; note the hack necessary to prevent a nasty recursion here.
	forced_resize = true;
	code_window->resize(cwb);
	forced_resize = false;
}

//void set_visibility(bool);
static LRESULT SciTEWndProc(HWND hwnd, UINT iMessage, WPARAM wParam, LPARAM lParam)
{
	switch (iMessage)
	{
	case WM_ACTIVATEAPP:
		//PaletteWindow::set_visibility(wParam);
		//set_visibility(wParam);
		call_named_function(sL, "OnActivate", wParam);//!- disabled
		//floating toolbars may grab the focus, so restore it.
		if (wParam) code_window->set_focus();
		break;

	case WM_CLOSE:
		call_named_function(sL, "OnClosing", 0); //!- disabled
		break;

	case WM_COMMAND:
		if (call_named_function(sL, "OnCommand", LOWORD(wParam))) return TRUE;//!- disabled
		break;
	}
	return CallWindowProc(old_scite_proc, hwnd, iMessage, wParam, lParam);
}

static LRESULT ScintillaWndProc(HWND hwnd, UINT iMessage, WPARAM wParam, LPARAM lParam)
{
	if (iMessage == WM_SIZE) {
		if (extra_window) {
			if (!forced_resize) {
				force_contents_resize();
			}
		}
	}
	return CallWindowProc(old_scintilla_proc, hwnd, iMessage, wParam, lParam);
}

static LRESULT ContentWndProc(HWND hwnd, UINT iMessage, WPARAM wParam, LPARAM lParam)
{
	if (iMessage == WM_SETCURSOR) {
		Point ptCursor;
		GetCursorPos(&ptCursor);
		Point ptClient = ptCursor;
		ScreenToClient(hContent, &ptClient);
		if (extra.is_inside(ptClient)) {
			return DefWindowProc(hSciTE, iMessage, wParam, lParam);
		}
	}
	return CallWindowProc(old_content_proc, hwnd, iMessage, wParam, lParam);
}

static void subclass_scite_window()
{
	static bool subclassed = false;
	if (!subclassed) {  // to prevent a recursion
		old_scite_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(hSciTE, GWLP_WNDPROC, (LONG_PTR)SciTEWndProc));
		old_content_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(hContent, GWLP_WNDPROC, (LONG_PTR)ContentWndProc));
		old_scintilla_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(hCode, GWLP_WNDPROC, (LONG_PTR)ScintillaWndProc));
		subclassed = true;
	}
}

//////// dialogs functions
static inline unsigned int convert_colour_spec(const char* clrs)
{
	unsigned int r = 0, g = 0, b = 0;
	sscanf(clrs, "#%02x%02x%02x", &r, &g, &b);
	return RGB(r, g, b);
}

/** gui.colour_dlg(default_colour)
	@param default_colour  colour either in form '#RRGGBB" or as a 32-bit integer
	@return chosen colour, in same form as default_colour
*/
int do_colour_dlg(lua_State* L)
{
	bool in_rgb = lua_isstring(L, 1);
	unsigned int cval;
	if (in_rgb) {
		cval = convert_colour_spec(lua_tostring(L, 1));
	}
	else {
		cval = (UINT)luaL_optinteger(L, 1, 0);
	}
	TColourDialog dlg(get_parent(), cval);
	if (dlg.go()) {
		cval = dlg.result();
		if (in_rgb) {
			char buff[12];
			sprintf(buff, "#%02X%02X%02X", GetRValue(cval), GetGValue(cval), GetBValue(cval));
			lua_pushstring(L, buff);
		}
		else {
			lua_pushnumber(L, cval);
		}
	}
	else {
		lua_pushnil(L);
	}
	return 1;
}

/** gui.message(message_string, is_warning)
	@param message_string
	@param is_warning (0 for plain message, 1 for warning box)
	MSG_ERROR=2,MSG_WARNING=1, MSG_QUERY=3;
*/

int do_message(lua_State* L)
{
	auto msg = StringFromUTF8(luaL_checkstring(L, 1));
	auto kind = StringFromUTF8(luaL_optstring(L, 2, "message"));
	int type = 0;
	if (EQ(kind.c_str(), L"message")) type = 0; else //if (kind == L"message") type = 0; else
		if (EQ(kind.c_str(), L"warning")) type = 1; else
			if (EQ(kind.c_str(), L"error")) type = 2; else
				if (EQ(kind.c_str(), L"query")) type = 3;
	lua_pushboolean(L, get_parent()->message(msg.c_str(), type));
	return 1;
}

// gui.open_dlg([sCaption = "Open File"][,cFilter = "All (*.*)|*.*"])
int do_open_dlg(lua_State* L)
{
	auto caption = StringFromUTF8(luaL_optstring(L, 1, "Open File"));
	auto filter = StringFromUTF8(luaL_optstring(L, 2, "All (*.*)|*.*"));
	TOpenFile tof(get_parent(), caption.c_str(), filter.c_str());
	if (tof.go()) {
		lua_pushstring(L, UTF8FromString(tof.file_name()).c_str());
	}
	else {
		lua_pushnil(L);
	}
	return 1;
}

// gui.open_dlg([sCaption = "Save File"][, cFilter = "All (*.*)|*.*"])
int do_save_dlg(lua_State* L)
{
	auto caption = StringFromUTF8(luaL_optstring(L, 1, "Save File"));
	auto filter = StringFromUTF8(luaL_optstring(L, 2, "All (*.*)|*.*"));
	TSaveFile tof(get_parent(), caption.c_str(), filter.c_str());
	if (tof.go()) {
		lua_pushstring(L, UTF8FromString(tof.file_name()).c_str());
	}
	else {
		lua_pushnil(L);
	}
	return 1;
}

// gui.select_dir_dlg([sDescription = ""][, sInitialdir = ""])
int do_select_dir_dlg(lua_State* L)
{
	auto descr = StringFromUTF8(luaL_optstring(L, 1, ""));
	auto initdir = StringFromUTF8(luaL_optstring(L, 2, ""));
	TSelectDir dir(get_parent(), descr.c_str(), initdir.c_str());
	if (dir.go()) {
		lua_pushstring(L, UTF8FromString(dir.path()).c_str());
	}
	else {
		lua_pushnil(L);
	}
	return 1;
}

class PromptDlg : public TModalDlg {
public:
	wchar_t m_val[256];
	const wchar_t* m_field_name;

	PromptDlg(TEventWindow* parent, pchar field_name, const wchar_t* val)
		: TModalDlg(parent, L"Enter:"), m_field_name(field_name)
	{
		wcscpy_s(m_val, val);
	}

	void layout(Layout& lo)
	{
		lo << Field(m_field_name, m_val);
	}
};

// gui.prompt_value( sPrompt_string [, default_value = ""])
int do_prompt_value(lua_State* L)
{
	auto varname = StringFromUTF8(luaL_checkstring(L, 1));
	auto value = StringFromUTF8(luaL_optstring(L, 2, ""));
	PromptDlg dlg((TEventWindow*)get_desktop_window(), varname.c_str(), value.c_str());
	if (dlg.show_modal()) {
		lua_pushstring(L, UTF8FromString(dlg.m_val).c_str());
	}
	else {
		lua_pushnil(L);
	}
	return 1;
}
/// end dialos functions

/// others functions
static int append_file(lua_State* L, int idx, int attrib, bool look_for_dir, const wchar_t* value)
{
	if (((attrib & _A_SUBDIR) != 0) == look_for_dir) {
		if (look_for_dir && (EQ(value, L".") || EQ(value, L".."))) return idx;
		lua_pushinteger(L, idx);
		lua_pushstring(L, UTF8FromString(value).c_str());
		lua_settable(L, -3);
		return idx + 1;
	}
	return idx;
}

static bool optboolean(lua_State* L, int idx, bool res)
{
	if (lua_isnoneornil(L, idx)) {
		return res;
	}
	else {
		return lua_toboolean(L, idx);
	}
}

static int do_files(lua_State* L)
{
	struct _wfinddata_t c_file;
	auto mask = StringFromUTF8(luaL_checkstring(L, 1));
	bool look_for_dir = optboolean(L, 2, false);
	auto hFile = _wfindfirst(mask.c_str(), &c_file);
	int i = 1;
	if (hFile == -1L) { lua_pushboolean(L, 0); return 1; }
	lua_newtable(L);
	i = append_file(L, i, c_file.attrib, look_for_dir, c_file.name);
	while (_wfindnext(hFile, &c_file) == 0) {
		i = append_file(L, i, c_file.attrib, look_for_dir, c_file.name);
	}
	return 1;
}

static int do_chdir(lua_State* L)
{
	const char* dirname = luaL_checkstring(L, 1);
	int res = _chdir(dirname);
	lua_pushboolean(L, res == 0);
	return 1;
}

int do_run(lua_State* L)
{
	auto wsFile = StringFromUTF8(luaL_checkstring(L, 1));
	auto wsParameters = StringFromUTF8(lua_tostring(L, 2));
	auto wsDirectory = StringFromUTF8(lua_tostring(L, 3));
	lua_Integer res = (lua_Integer)ShellExecute(
		NULL,
		L"open",
		wsFile.c_str(),
		wsParameters.c_str(),
		wsDirectory.c_str(),
		SW_SHOWDEFAULT
	);
	if (res <= HINSTANCE_ERROR) {
		lua_pushboolean(L, 0);
		lua_pushinteger(L, res);
		return 2;
	}
	else {
		lua_pushinteger(L, res);
		return 1;
	}
}
/// end others functions

/// windows functions

////// This acts as the top-level frame window containing these controls; it supports
////// adding extra buttons and actions.

void dispatch_ref(lua_State* L, int idx, int ival)
{
	if (idx != 0) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, idx);
		lua_pushinteger(L, ival); // number->integer

		if (lua_pcall(L, 1, 0, 0)) {
			OutputMessage(L);
		}
	}
}

void dispatch_ref_bool(lua_State* L, int idx, bool ival)
{
	if (idx != 0) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, idx);
		lua_pushboolean(L, ival);

		if (lua_pcall(L, 1, 0, 0)) {
			OutputMessage(L);
		}
	}
}

int dispatch_rref(lua_State* L, int idx, int ival)
{
	if (idx != 0) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, idx);
		lua_pushinteger(L, ival);

		if (lua_pcall(L, 1, 1, 0)) {
			OutputMessage(L);
		}
		else {
			return lua_toboolean(L, -1);
		}
	}
	return 0;
}

bool function_ref(lua_State* L, int idx, int* pr)
{
	if (*pr != 0) {
		luaL_unref(L, LUA_REGISTRYINDEX, *pr);
	}
	lua_pushvalue(L, idx);
	*pr = luaL_ref(L, LUA_REGISTRYINDEX);
	return true;
}

class LuaWindow : public TEventWindow
{
protected:
	lua_State* L;
	int onclose_idx;
	int onfocus_idx;
	int onshow_idx;
public:
	LuaWindow(const wchar_t* caption, lua_State* l, TWin* parent, int stylex = 0, bool is_child = false, int style = -1)
		: TEventWindow(caption, parent, stylex, is_child, style), L(l), onclose_idx(0), onfocus_idx(0), onshow_idx(0)
	{}

	void handler(Item* item)
	{
		//wchar_t* name = (wchar_t*)item->data;
		//if (wcsncmp(name, L"IDM_", 4) == 0) {
		//	wchar_t buff[128];
		//	swprintf(buff, L"scite.MenuCommand(%s)", name);
		//	luaL_dostring(L, UTF8FromString(buff).c_str());
		//}
		//else
		//{
		//	lua_rawgeti(L, LUA_REGISTRYINDEX, (int)item->data);
		//	if (lua_pcall(L, 0, 0, 0)) {
		//		OutputMessage(L);
		//	}
		//}
		if (lua_rawgeti(L, LUA_REGISTRYINDEX, (lua_Integer)item->data) == LUA_TFUNCTION)	{
			if (lua_pcall(L, 0, 0, 0)) {
				OutputMessage(L);
			}
		} else {
			lua_Integer _data = luaL_checkinteger(L, -1);
			wchar_t buff[128];
			swprintf_s(buff, L"scite.MenuCommand(%lli)", _data);
			luaL_dostring(L, UTF8FromString(buff).c_str());
		}
	}

	void set_on_close(int iarg)
	{
		function_ref(L, iarg, &onclose_idx);
	}

	void on_close()
	{
		dispatch_ref(L, onclose_idx, 0);
	}

	void set_on_show(int iarg)
	{
		function_ref(L, iarg, &onshow_idx);
	}

	void on_showhide(bool show)
	{
		dispatch_ref_bool(L, onshow_idx, show);
	}
};

//std::list<PaletteWindow*> instances;
static PaletteWindow* instances[50];
static int n_instances = 0;

class PaletteWindow : public LuaWindow
{
protected:
	bool m_shown;
public:
	PaletteWindow(const wchar_t* caption, lua_State* l, int stylex = 0, int style = -1)
		: LuaWindow(caption, l, NULL, stylex, false, style), m_shown(false)
	{
		//instances.push_back(this);
		instances[n_instances++] = this;
		instances[n_instances] = NULL;
	}
	~PaletteWindow() {
		MessageBox(NULL, L"palette", L"destroy", 0);
	}
	void show(int how = SW_SHOW)
	{
		TEventWindow::show(how);
		m_shown = true;
	}

	void hide()
	{
		TEventWindow::hide();
		m_shown = false;
	}

	virtual bool query_close()
	{
		hide();
		return false;
	}

	void really_show() { TEventWindow::show(); }

	void really_hide() { TEventWindow::hide(); }

	bool is_shown() { return m_shown; }

	static void set_visibility(bool yesno)
	{
		for (int i = 0; instances[i]; i++) {
			PaletteWindow* w = instances[i];
			if (w->m_shown) {
				if (yesno) w->really_show();
				else w->really_hide();
			}
		}
	}
};

//static void set_visibility(bool yesno)
//{
//	for (auto w : instances)
//		if (w->is_shown()) {
//			if (yesno) w->really_show();
//			else w->really_hide();
//		}
//}

struct WinWrap {
	TWin* window;
	void* data;
};

static int wrap_window(lua_State* L, TWin* win)
{
	WinWrap* wrp = (WinWrap*)lua_newuserdata(L, sizeof(WinWrap));
	wrp->window = win;
	wrp->data = NULL;
	luaL_getmetatable(L, WINDOW_CLASS);
	lua_setmetatable(L, -2);
	return 1;
}

static void throw_error(lua_State* L, const wchar_t* msg)
{
	lua_pushstring(L, UTF8FromString(msg).c_str());
	lua_error(L);
}

static TWin* window_arg(lua_State* L, int idx = 1)
{
	WinWrap* wrp = (WinWrap*)lua_touserdata(L, idx);
	if (!wrp) throw_error(L, L"not a window");
	return (PaletteWindow*)wrp->window;
}

static void*& window_data(lua_State* L, int idx = 1)
{
	WinWrap* wrp = (WinWrap*)lua_touserdata(L, idx);
	if (!wrp) throw_error(L, L"not a window");
	return wrp->data;
}

class ContainerWindow : public PaletteWindow
{
public:
	TListView* listv;
	const wchar_t* select_name;
	const wchar_t* double_name;

	ContainerWindow(const wchar_t* caption, lua_State* l)
		: PaletteWindow(caption, l), listv(NULL), select_name(NULL), double_name(NULL)
	{
		set_icon_from_window(s_parent);
	}
	~ContainerWindow() {
		MessageBox(NULL, L"container", L"destroy", 0);
	};
	void dispatch(lua_Integer data, int ival)
	{
		if (data) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, data);
			lua_pushinteger(L, ival);
			if (lua_pcall(L, 1, 0, 0)) {
				OutputMessage(L);
			}
		}
	}

	void on_button(Item* item)
	{
		dispatch((lua_Integer)item->data, item->id);  //listv->selected_id()
	}

	void add_buttons(lua_State* l)
	{
		int nargs = lua_gettop(l);
		int i = 2;
		TEW* panel = new TEW(NULL, this, 0, true);
		panel->align(alBottom, 50);
		Layout layout(panel, this);
		while (i < nargs) {
			int itm_type = lua_getglobal(l, luaL_checkstring(l, i + 1));
			if (itm_type == LUA_TFUNCTION)
			{
				int data = luaL_ref(L, LUA_REGISTRYINDEX);
				layout << Button(StringFromUTF8(luaL_checkstring(l, i)).c_str(), (EH)&ContainerWindow::on_button, (void*)data);
			} else {
				lua_pop(L, 1);
				output("not a function:");
				output(lua_tostring(l, i + 1));
			}
			i += 2;
		}
		layout.release();
		add(panel);
		size();
	}
};

int window_show(lua_State* L)
{
	window_arg(L)->show();
	return 0;
}

int window_hide(lua_State* L)
{
	window_arg(L)->hide();
	return 0;
}

/** w:size()
	@param self
	@param width
	@param height
*/
int window_size(lua_State* L)
{
	int w = (int)luaL_optinteger(L, 2, 250);
	int h = (int)luaL_optinteger(L, 3, 200);
	window_arg(L)->resize(w, h);
	return 0;
}

/** w:position()
	@param self
	@param x
	@param y
*/
int window_position(lua_State* L)
{
	int x = (int)luaL_optinteger(L, 2, 10);
	int y = (int)luaL_optinteger(L, 3, 10);
	window_arg(L)->move(x, y);
	return 0;
}


int window_get_bounds(lua_State* L)
{
	TWin* win = (TWin*)window_arg(L);
	Rect rt;
	win->get_rect(rt);
	lua_pushboolean(L, win->visible());
	lua_pushinteger(L, rt.left);
	lua_pushinteger(L, rt.right);
	lua_pushinteger(L, rt.width());
	lua_pushinteger(L, rt.height());
	return 5;
}

int new_window(lua_State* L)
{
	static ContainerWindow* cw = NULL;
		auto caption = StringFromUTF8(luaL_checkstring(L, 1));
		cw = new ContainerWindow(caption.c_str(), L);
		s_last_parent = cw;
	return wrap_window(L, cw);
}


class PanelWindow : public LuaWindow
{
public:
	PanelWindow(lua_State* l)
		: LuaWindow(L"", l, get_parent(), 0, true)
	{}
};

int new_panel(lua_State* L)
{
	PanelWindow* pw = new PanelWindow(L);
	pw->align(alLeft, (int)luaL_checkinteger(L, 1));
	s_last_parent = pw;
	return wrap_window(L, pw);
}

class LuaControl
{
protected:
	lua_State* L;
	int select_idx;
	int double_idx;
	int onkey_idx;
	int rclick_idx;
	int onclose_idx;
	int onfocus_idx;
	Handle m_hpopup_menu;

public:
	LuaControl(lua_State* l)
		: L(l), select_idx(0), double_idx(0), onkey_idx(0), rclick_idx(0)
		, m_hpopup_menu(0), onclose_idx(0), onfocus_idx(0)
	{}

	void set_popup_menu(Handle menu)
	{
		m_hpopup_menu = menu;
	}

	virtual void set_select(int iarg)
	{
		function_ref(L, iarg, &select_idx);
	}

	virtual void set_double_click(int iarg)
	{
		function_ref(L, iarg, &double_idx);
	}

	virtual void set_onkey(int iarg)
	{
		function_ref(L, iarg, &onkey_idx);
	}

	virtual void set_rclick(int iarg)
	{
		function_ref(L, iarg, &rclick_idx);
	}

	virtual void set_on_close(int iarg)
	{
		function_ref(L, iarg, &onclose_idx);
	}

	virtual void set_on_focus(int iarg)
	{
		function_ref(L, iarg, &onfocus_idx);
	}

};

class TTabControlLua : public TTabControlB, public LuaControl
{
	int selection_changing_idx;
	TEventWindow* form;
public:

	TTabControlLua(TEventWindow* parent, lua_State* l)
		: TTabControlB(parent), LuaControl(l), selection_changing_idx(0), form(parent)
	{
	}

	void set_selection_changing(int iarg)
	{
		function_ref(L, iarg, &selection_changing_idx);
	}

	// implement
	virtual void handle_select(int id)
	{
		TWin* page = (TWin*)get_data(id);
		form->set_client(page);
		form->size();
		dispatch_ref(L, select_idx, id);
	}

	virtual int handle_selection_changing(int id)
	{
		if (selection_changing_idx) {
			dispatch_ref(L, selection_changing_idx, id);
			return 1;
		}
		else {
			return 0;
		}
	}

	TEventWindow* get_parent_win()	{ return form; }

	virtual int handle_rclick(int id)
	{
		if (m_hpopup_menu) {
			POINT p;
			GetCursorPos(&p);

			HWND hwnd = (HWND)(get_parent_win()->handle());
			TrackPopupMenu((HMENU)m_hpopup_menu, TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, hwnd, NULL);
			return 1;
		}

		return 0;
	}
};

int new_tabbar(lua_State* L)
{
	TEventWindow* form = (TEventWindow*)window_arg(L, 1);
	TTabControlLua* tab = new TTabControlLua(form, L);
	s_last_parent = form;
	tab->align(alTop);
	form->add(tab);
	return wrap_window(L, tab);
}

int tabbar_add(lua_State* L)
{
	TTabControl* tab = (TTabControl*)window_arg(L, 1);
	tab->add(StringFromUTF8(luaL_checkstring(L, 2)).c_str(), window_arg(L, 3));
	return 0;
}

int window_remove(lua_State* L)
{
	TEventWindow* form = (TEventWindow*)window_arg(L, 1);
	form->remove(window_arg(L, 2));
	TWin* split = (TWin*)window_data(L, 2);
	if (split) {
		form->remove(split);
	}
	return 0;
}

// wnd_parent:add( child_window[, sAligment = "client"][, iSize = 100][, bSplitted = true])
int window_add(lua_State* L)
{
	TEventWindow* cw = (TEventWindow*)window_arg(L, 1);
	TWin* child = window_arg(L, 2);
	auto align = StringFromUTF8(luaL_optstring(L, 3, "client"));
	int sz = luaL_optnumber(L, 4, 100);
	bool splitter = optboolean(L, 5, true);
	child->set_parent(cw);
	if (EQ(align.c_str(), L"top")) {
		child->align(alTop, sz);
	}
	else
		if (EQ(align.c_str(), L"bottom")) {
			child->align(alBottom, sz);
		}
		else
			if (EQ(align.c_str(), L"left")) {
				child->align(alLeft, sz);
			}
			else
				if (EQ(align.c_str(), L"right")) {
					child->align(alRight, sz);
				}
				else {
					child->align(alClient, sz);
				}
	cw->add(child);
	if (splitter && child->align() != alClient) {
		TSplitter* split = new TSplitter(cw, child);
		cw->add(split);
		window_data(L, 2) = split;
	}
	return 0;
}

class TMemoLua : public TMemo, public LuaControl
{
public:
	TMemoLua(TWin* parent, lua_State* l, int id, bool do_scroll = false, bool plain = false)
		:TMemo(parent, id, do_scroll, plain), LuaControl(l)
	{}

	virtual int handle_onkey(int id)
	{
		return dispatch_rref(L, onkey_idx, id);
	}
};

int new_memo(lua_State* L)
{
	TWin* p = get_last_parent();
	if (p) {
		TMemoLua* m = new TMemoLua(p, L, 1);
		return wrap_window(L, m);
	}
	else {
		luaL_error(L, "There is no parent panel to create 'gui.memo'");
		return 0;
	}
}

int memo_set(lua_State* L)
{
	TMemoLua* m = (TMemoLua*)window_arg(L, 1);
	m->set_text(StringFromUTF8(luaL_checkstring(L, 2)).c_str());
	return 0;
}

int memo_get(lua_State* L)
{
	wchar_t str[BUFSIZE];
	TMemoLua* m = (TMemoLua*)window_arg(L, 1);
	m->get_text(str, BUFSIZE);
	lua_pushstring(L, UTF8FromString(str).c_str());
	return 1;
}

int memo_set_colour(lua_State* L)
{
	TMemoLua* m = (TMemoLua*)window_arg(L, 1);
	m->set_text_colour(convert_colour_spec(luaL_checkstring(L, 2))); // Must be only ASCII
	m->set_background_colour(convert_colour_spec(luaL_checkstring(L, 3)));

	return 0;
}

static void shake_scite_descendants()
{
	Rect frt;
	s_parent->get_rect(frt, false);
	s_parent->send_msg(WM_SIZE, SIZE_RESTORED, (LPARAM)MAKELONG(frt.width(), frt.height()));
}

class SideSplitter : public TSplitterB
{
public:
	SideSplitter(TEventWindow* form, TWin* control)
		: TSplitterB(form, control, 5)
	{}

	void paint(TDC& dc)
	{
		Rect rt(this);
		dc.rectangle(rt);
	}


	void on_resize(const Rect& rt)
	{
		TSplitterB::on_resize(rt);
		shake_scite_descendants();
	}
};

static int do_set_panel(lua_State* L)
{
	if (content_window == NULL) {
		lua_pushstring(L, "Window subclassing was not successful");
		lua_error(L);
	}
	if (!lua_isuserdata(L, 1) && extra_window != NULL) {
		extra_window->hide();
		extra_window = NULL;
		extra_window_splitter->close();
		delete extra_window_splitter;
		extra_window_splitter = NULL;
		shake_scite_descendants();
	}
	else {
		extra_window = window_arg(L);
		auto align = StringFromUTF8(luaL_optstring(L, 2, "left"));
		if (EQ(align.c_str(), L"left")) {
			extra_window->align(alLeft);
		}
		else
			if (EQ(align.c_str(), L"right")) {
				extra_window->align(alRight);
			}
		extra_window->set_parent(content_window);
		extra_window->show();
		extra_window_splitter = new SideSplitter((TEventWindow*)content_window, extra_window);
		extra_window_splitter->show();
		force_contents_resize();
	}

	return 0;
}
/////////////////////////////////////////////////////////////////////////////////////////
/* do_day_of_year */
#include <algorithm>
#include <ctime>

int     days
(
	int     day,
	int     month,
	int     year
)
{
	int     a = (14 - month) / 12;
	int     y = year - a;
	int     m = month + 12 * a - 3;

	return      day
		+ (153 * m + 2) / 5
		+ 365 * y
		+ y / 4
		- y / 100
		+ y / 400;
}

int do_day_of_year(lua_State* L)
{
	if (lua_gettop(L) == 3)
	{
		int d = luaL_checkinteger(L, 1);
		std::clamp(d, 1, 31);
		int m = luaL_checkinteger(L, 2);
		std::clamp(m, 1, 12);
		int y = luaL_checkinteger(L, 3);
		lua_pushinteger(L, days(d, m, y) - days(0, 1, y));
	} else {
		time_t seconds = time(NULL);
		tm* timeinfo = localtime(&seconds);
		lua_pushinteger(L, timeinfo->tm_yday + 1);
	}
	return 1;
}

/////////////////////////////////////////////////////////////////////////////////////////

int do_test_function(lua_State* L)
{

	return 0;
}

//  this will allow us to hand keyboard focus over to editor
//  gui.pass_focus() переключаем фокус на редактор
static int do_pass_focus(lua_State* L)
{
	lua_getglobal(L, "editor");
	lua_pushboolean(L, 1);
	lua_setfield(L, -2, "Focus");
	lua_pop(L, 1);
	if (code_window) {
		code_window->set_focus();
	}
	return 0;
}

// parent_wnd:client(child_wnd)
int window_client(lua_State* L)
{
	TEventWindow* cw = (TEventWindow*)window_arg(L, 1);
	TWin* child = window_arg(L, 2);
	if (!child) throw_error(L, L"must provide a child window");
	child->set_parent(cw);
	cw->set_client(child);
	return 0;
}

class TListViewLua : public TListViewB, public LuaControl
{
public:
	TListViewLua(TWin* parent, lua_State* l, bool multiple_columns = false, bool single_select = true)
		: TListViewB(parent, false, multiple_columns, single_select),
		LuaControl(l)
	{
		if (!multiple_columns) {
			add_column(L"*", 200);
		}
	}

	// implement
	virtual void handle_select(int id)
	{
		dispatch_ref(L, select_idx, id);
	}

	virtual void handle_double_click(int id)
	{
		dispatch_ref(L, double_idx, id);
	}

	virtual void handle_onkey(int id)
	{
		dispatch_ref(L, onkey_idx, id);
	}

	virtual int handle_rclick(int id)
	{
		if (m_hpopup_menu) {
			POINT p;
			GetCursorPos(&p);

			HWND hwnd = (HWND)(get_parent_win()->handle());
			TrackPopupMenu((HMENU)m_hpopup_menu, TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, hwnd, NULL);
			return 1;
		}

		return 0;
	}

	virtual void handle_onfocus(bool yes)
	{
		dispatch_ref_bool(L, onfocus_idx, yes);
	}
};

class TTreeViewLua : public TTreeView, public LuaControl
{
public:
	TTreeViewLua(TEventWindow* parent, lua_State* l)
		: TTreeView(parent),
		LuaControl(l)
	{
	}

	// implement
	virtual void handle_select(int id)
	{
		dispatch_ref(L, select_idx, id);
	}

	virtual void handle_double_click(int id)
	{
		dispatch_ref(L, double_idx, id);
	}

	virtual void handle_onkey(int id)
	{
		dispatch_ref(L, onkey_idx, id);
	}

	virtual int handle_rclick(int id)
	{
		if (m_hpopup_menu) {
			POINT p;
			GetCursorPos(&p);

			HWND hwnd = (HWND)(get_parent_win()->handle());
			TrackPopupMenu((HMENU)m_hpopup_menu, TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, hwnd, NULL);
			return 1;
		}

		return 0;
	}

	virtual void handle_onfocus(bool yes)
	{
		dispatch_ref_bool(L, onfocus_idx, yes);
	}
};

TListViewLua* list_window_arg(lua_State* L)
{
	return (TListViewLua*)(window_arg(L));
}

int window_select_item(lua_State* L)
{
	list_window_arg(L)->select_item((int)luaL_checkinteger(L, 2));
	return 0;
}

//window_list:clear()
int window_clear(lua_State* L)
{
	list_window_arg(L)->clear();
	return 0;
}

// window_list:delete_item(index)
int window_delete_item(lua_State* L)
{
	list_window_arg(L)->delete_item((int)luaL_checkinteger(L, 2));
	return 0;
}

// vector of wide strings
typedef std::vector<std::wstring> vecws;

//static wchar_t** table_to_str_array(lua_State* L, int idx, int* psz = NULL)
vecws table_to_str_array(lua_State* L, int idx, int* psz = NULL)
{
	if (!lua_istable(L, idx)) {
		throw_error(L, L"table argument expected");
	}
	std::vector<std::wstring> res;
	lua_pushnil(L); // first key
	while (lua_next(L, idx) != 0) {
		res.emplace_back(StringFromUTF8(lua_tostring(L, -1)));
		lua_pop(L, 1);  /* removes `value'; keeps `key' for next iteration */
	}
	return res;	
}

void fill_menu(lua_State* L, Menu& mnu)
{
	vecws items = table_to_str_array(L, 2);
	for (auto& it : items) {
		size_t pos = it.find_first_of(L"|");
		if (pos == std::string::npos)
		{
			mnu.add_separator();
		}
		else
		{
			auto text = it.substr(0, pos);
			auto fun = it.substr(pos + 1);
			std::string fname = UTF8FromString(fun);
			lua_getglobal(L, fname.c_str());
			if (lua_isfunction(L, -1))
			{
				int data = luaL_ref(L, LUA_REGISTRYINDEX);
				mnu << Item(text.data(), (EH)&LuaWindow::handler, (void*)data);
			}
			else
			{
				output("can't find function for popup or contex menu:");
				output(fname.c_str());
				lua_pop(L, 1);
			}
		}
	}
}

int window_context_menu(lua_State* L)
{
	TWin* w = window_arg(L, 1);
	if (LuaWindow* cw = dynamic_cast<LuaWindow*> (w)) {
		ContextMenu mnu(cw);
		fill_menu(L, mnu);
	}
	else if (TTabControlLua* tc = dynamic_cast<TTabControlLua*>(w)) {
		TEventWindow* parent = tc->get_parent_win();
		HMENU hm = CreatePopupMenu();
		Popup mnu(hm);
		fill_menu(L, mnu);
		tc->set_popup_menu(hm);
		mnu.get_menu_handler()->set_form(parent);
		parent->add_handler(mnu.get_menu_handler());
	}
	else if (TListViewLua* lv = dynamic_cast<TListViewLua*>(w)) {
		TEventWindow* parent = lv->get_parent_win();
		HMENU hm = CreatePopupMenu();
		Popup mnu(hm);
		fill_menu(L, mnu);
		lv->set_popup_menu(hm);
		mnu.get_menu_handler()->set_form(parent);
		parent->add_handler(mnu.get_menu_handler());
	}
	return 0;
}

void window_aux_item(lua_State* L, bool at_index)
{
	TWin* w = window_arg(L);
	if (TListViewLua* lv = dynamic_cast<TListViewLua*>(w)) {
		int next_arg, ipos;
		void* data = NULL;
		if (at_index) {
			next_arg = 3;
			ipos = (int)luaL_checkinteger(L, 2);
		}
		else {
			next_arg = 2;
			ipos = lv->count();
		}
		if (!lua_isnoneornil(L, next_arg + 1)) {
			lua_pushvalue(L, next_arg + 1);
			data = (void*)luaL_ref(L,LUA_REGISTRYINDEX);
		}
		if (lua_isstring(L, next_arg)) {
			lv->add_item_at(ipos, StringFromUTF8(luaL_checkstring(L, next_arg)).c_str(), 0, data); // single column init with string
		}
		else {
			vecws items = table_to_str_array(L, next_arg);			
			const int _min = min(lv->columns(), items.size());
			int idx = lv->add_item_at(ipos, items.at(0).data(), 0, data); // init first column
			for (int i = 1; (i < _min) && items.at(i).size(); ++i) // init others
				lv->add_subitem(idx, items.at(i).data(), i);
		}
	}
	else if (TTreeViewLua* tv = dynamic_cast<TTreeViewLua*>(w)) {
		tv->add_item(StringFromUTF8(luaL_checkstring(L, 2)).c_str(), 0, NULL);
	}
}

// window_list:add_item(text, data)
// window_list:add_item({text1,text2}, data)
int window_add_item(lua_State* L)
{
	window_aux_item(L, false);
	return 0;
}

// window_list:insert_item(index, string)
int window_insert_item(lua_State* L)
{
	window_aux_item(L, true);
	return 0;
}

// window_list:count()
int window_count(lua_State* L)
{
	int sz = list_window_arg(L)->count();
	lua_pushinteger(L, sz);
	return 1;
}

// window_list:add_column( sTitle, iSize)
int window_add_column(lua_State* L)
{
	list_window_arg(L)->add_column(StringFromUTF8(luaL_checkstring(L, 2)).c_str(), (int)luaL_checkinteger(L, 3));
	return 0;
}

// window_list:get_item_text( index )
int window_get_item_text(lua_State* L)
{
	wchar_t* buff = new wchar_t[BUFFER_SIZE];
	list_window_arg(L)->get_item_text((int)luaL_checkinteger(L, 2), buff, BUFFER_SIZE);
	lua_pushstring(L, UTF8FromString(buff).c_str());
	delete[] buff;
	return 1;
}

// window_list:get_item_data( index )
int window_get_item_data(lua_State* L)
{
	void* data = list_window_arg(L)->get_item_data((int)luaL_checkinteger(L, 2));
	lua_rawgeti(L, LUA_REGISTRYINDEX, (lua_Integer)data);
	return 1;
}

// window_list:set_list_colour( sForeColor, sBackColor )
int window_set_colour(lua_State* L)
{
	TListViewLua* ls = list_window_arg(L);
	ls->set_foreground(convert_colour_spec(luaL_checkstring(L, 2)));
	ls->set_background(convert_colour_spec(luaL_checkstring(L, 3)));
	return 0;
}

int window_selected_item(lua_State* L)
{
	int idx = list_window_arg(L)->selected_id();
	lua_pushinteger(L, idx);
	return 1;
}

int window_selected_items(lua_State* L)
{
	TListViewLua* lv = list_window_arg(L);
	int i = -1;
	int idx = 0;
	lua_newtable(L);
	while (true) {
		i = lv->next_selected_id(i);
		if (i < 0) break;
		lua_pushinteger(L, ++idx);
		lua_pushinteger(L, i);
		lua_settable(L, -3);
	}

	return 1;
}

// list_window:selected_count()
int window_selected_count(lua_State* L)
{
	int count = list_window_arg(L)->selected_count();
	lua_pushinteger(L, count);
	return 1;
}

// gui.list([multiple_columns = false][, single_select = true])
int new_list_window(lua_State* L)
{
	TWin* p = get_last_parent();
	if (p) {
		bool multiple_columns = optboolean(L, 1, false);
		bool single_select = optboolean(L, 2, true);
		TListViewLua* lv = new TListViewLua(p, L, multiple_columns, single_select);
		return wrap_window(L, lv);
	}
	else {
		luaL_error(L, "There is no parent panel to create 'gui.list'");
		return 0;
	}
}

// list_window:autosize( index [, by_contents = false])
int window_autosize(lua_State* L)
{
	list_window_arg(L)->autosize_column((int)luaL_checkinteger(L, 2), optboolean(L, 3, false));
	return 0;
}

// generic methods

// wnd:on_select(function or <string name global function>)
int window_on_select(lua_State* L)
{
	LuaControl* lc = dynamic_cast<LuaControl*>(window_arg(L, 1));
	if (lc) lc->set_select(2);
	else luaL_error(L, "argument 1 is not a LuaControl");
	return 0;
}

int window_on_double_click(lua_State* L)
{
	list_window_arg(L)->set_double_click(2);
	return 0;
}

int window_on_close(lua_State* L)
{
	if (LuaWindow* cw = dynamic_cast<LuaWindow*>(window_arg(L)))
		cw->set_on_close(2);
	return 0;
}

int window_on_show(lua_State* L)
{
	if (LuaWindow* cw = dynamic_cast<LuaWindow*>(window_arg(L)))
		cw->set_on_show(2);
	return 0;
}

int window_on_focus(lua_State* L)
{
	LuaControl* lc = dynamic_cast<LuaControl*>(window_arg(L, 1));
	if (lc) lc->set_on_focus(2);
	else luaL_error(L, "argument 1 is not a LuaControl");
	return 0;
}

int window_on_key(lua_State* L)
{
	TWin* w = window_arg(L);
	if (TListViewLua* lv = dynamic_cast<TListViewLua*>(w)) {
		lv->set_onkey(2);
	}
	else if (TMemoLua* m = dynamic_cast<TMemoLua*>(w)) {
		m->set_onkey(2);
	}
	return 0;
}

int window_add_buttons(lua_State* L)
{
	ContainerWindow* lw = dynamic_cast<ContainerWindow*>(window_arg(L));
	if (lw) lw->add_buttons(L);
	else luaL_error(L, "argument 1 is not a ContainerWindow");
	return 0;
}
/// end windows functions


static const struct luaL_Reg gui[] = {
	// dialogs
	{"colour_dlg",do_colour_dlg},
	{"message",do_message},
	{"open_dlg",do_open_dlg},
	{"save_dlg",do_save_dlg},
	{"select_dir_dlg",do_select_dir_dlg},
	{"prompt_value",do_prompt_value},

	// others
	{"test_function",do_test_function},
	{"day_of_year",do_day_of_year},
	{"version",do_version},
	{"files",do_files},
	{"chdir",do_chdir},
	{"run",do_run},

	//windows 
	{"window",new_window},
	{"panel",new_panel},
	{"tabbar",new_tabbar},
	{"memo",new_memo},
	{"pass_focus",do_pass_focus},
	{"list",new_list_window},
	{"set_panel",do_set_panel},
	//{"tree",new_tree}, // not used
	//{"toolbar",new_toolbar}, // not used
	{NULL, NULL},
};

static const struct luaL_Reg window_methods[] = {
	// window
	{"show",window_show},
	{"hide",window_hide},
	{"size",window_size},
	{"position",window_position},
	{"bounds",window_get_bounds},
	{"client",window_client},
	{"add",window_add},
	{"remove",window_remove},
	{"context_menu",window_context_menu},

	{"on_close",window_on_close},
	{"on_show",window_on_show},
	{"on_focus",window_on_focus},
	{"on_key",window_on_key},
	//{"add_buttons",window_add_buttons},
	 
	//tabbar	
	{"add_tab",tabbar_add}, //win_tab:add_tab(sCaption, wndPanel)

	// list, tree
	{"clear",window_clear},
	{"set_selected_item",window_select_item},
	{"count",window_count},
	{"delete_item",window_delete_item},
	{"insert_item",window_insert_item},
	{"add_item",window_add_item},
	{"add_column",window_add_column},
	{"get_item_text",window_get_item_text},
	{"get_item_data",window_get_item_data},
	{"set_list_colour",window_set_colour},
	{"selected_count",window_selected_count},
	{"on_select",window_on_select},
	{"on_double_click",window_on_double_click},
	{"get_selected_item",window_selected_item},
	{"get_selected_items",window_selected_items},
	{"autosize",window_autosize},

	//memo
	{"set_text",memo_set},
	{"get_text",memo_get},
	{"set_memo_colour",memo_set_colour}, 
	{NULL, NULL},
};

extern "C" __declspec(dllexport)
int luaopen_gui(lua_State * L)
{
	// at this point, the SciTE window is available. Can't always assume
	// that it is the foreground window, so we hunt through all windows
	// associated with this thread (the main GUI thread) to find a window
	// matching the appropriate class name
 
	//EnumThreadWindows(GetCurrentThreadId(),CheckSciteWindow,(LPARAM)&hSciTE);
	hSciTE = FindWindow(L"SciTEWindow", NULL);
	s_parent = new TWin(hSciTE);
	sL = L;

	// Destroy window hierarchy created before.
	// And there are still memory and handle leaks.
	destroy_windows();

	// Its first child shold be the content pane (editor+output), 
	// but we check this anyway....	
	
	//EnumChildWindows(hSciTE,CheckContainerWindow,(LPARAM)&hContent);
	hContent = FindWindowEx(hSciTE, NULL, L"SciTEWindowContent", NULL);

	// the first child of the content pane is the editor pane.
	bool subclassed = false;
	if (hContent != NULL) {
		content_window = new TWin(hContent);
		hCode = GetWindow(hContent, GW_CHILD);
		if (hCode != NULL) {
			code_window = new TWin(hCode);
			subclass_scite_window();
			subclassed = true;
		}
	}
	if (!subclassed) {
		get_parent()->message(L"Cannot subclass SciTE Window", 2);
	}
#if LUA_VERSION_NUM < 502
	luaL_openlib (L, "gui", gui, 0);
	luaL_newmetatable(L, WINDOW_CLASS);  // create metatable for window objects
	lua_pushvalue(L, -1);  // push metatable
	lua_setfield(L, -2, "__index");  // metatable.__index = metatable
	luaL_register(L, NULL, window_methods);
#else
	luaL_newlib(L, gui);//luaL_openlib (L, "gui", gui, 0);
	lua_pushvalue(L, -1);  /* copy of module */
	lua_setglobal(L, "gui");
	luaL_newmetatable(L, WINDOW_CLASS);  // create metatable for window objects
	lua_pushvalue(L, -1);  // push metatable
	lua_setfield(L, -2, "__index");  // metatable.__index = metatable
	luaL_setfuncs(L, window_methods, 0); // luaL_register(L, NULL, window_methods);
	lua_pop(L, 1);
#endif
	return 1;
}

void on_destroy()
{
	//MessageBox(NULL, L"on_destroy", L"dbg", 0);
	delete extra_window;
	delete extra_window_splitter;
}