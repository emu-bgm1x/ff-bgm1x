// ff-bgm1x: DeSmuME 의 소리 분류 창 (melonDS 판 SoundCatalogDialog 와 같은 동작, Windows 기본 창으로 만든다).
//
// 이 창이 앞에 있으면 게임을 일시정지하고, 에뮬레이터 창을 누르면 게임을 다시 돌리며 나온 소리를 목록에 모은다(수집 모드).
// 목록의 소리마다 재생/일시정지, 중지, 분류 지정, 볼륨, 기본값으로 되돌리기. 지정 칸·볼륨 칸을 두 번 클릭하면 그 자리에서 바꾸고,
// 그 밖의 칸을 두 번 클릭하면 재생한다. 분류는 게임별 파일에 바로 저장하고, 내보내기·가져오기로 주고받는다.
// 창이 닫혀 있으면 수집하지 않는다.
#include <string>
#include <vector>

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include "ffbgm.h"
#include "SoundCatalog.h"
#include "SoundPreview.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "winmm.lib")

SoundCatalog& ffbgm_catalog();  // BgmReplacer.cpp
void Pause();                   // frontend/windows/main.cpp
void Unpause();
extern int emu_paused;

namespace
{

enum
{
    ID_LIST = 100,
    ID_PLAY,
    ID_STOP,
    ID_KIND,
    ID_VOLUME,
    ID_RESET,
    ID_IMPORT,
    ID_EXPORT,
    ID_CLEAR,
    ID_INLINE,
    ID_TIMER = 1,
    WM_FF_INLINE_DONE = WM_APP + 1,
};

enum Column { Col_Order, Col_Type, Col_Name, Col_Auto, Col_User, Col_Volume, Col_Count, Col_Key, Col_Num };

const int kKinds[] = {SoundCatalog::Kind_Auto, SoundCatalog::Kind_Bgm, SoundCatalog::Kind_Jingle,
                      SoundCatalog::Kind_Cutscene, SoundCatalog::Kind_Sfx, SoundCatalog::Kind_None};

std::wstring W(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string U(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring kindText(int k)
{
    return k == SoundCatalog::Kind_Auto ? std::wstring(L"자동 (기본값)") : W(SoundCatalog::kindName(k));
}

const wchar_t* typeText(int type)
{
    switch (type)
    {
    case SoundCatalog::Type_Seq: return L"시퀀스";
    case SoundCatalog::Type_Ssar: return L"효과음 묶음";
    case SoundCatalog::Type_Strm: return L"스트림(STRM)";
    }
    return L"?";
}

struct Window
{
    HWND hwnd = nullptr, list = nullptr, status = nullptr, playing = nullptr;
    HWND playBtn = nullptr, kindBox = nullptr, volEdit = nullptr, inlineEdit = nullptr;
    SoundPreview preview;
    unsigned shownVersion = ~0u;
    bool pausedByUs = false;
    bool updatingUi = false;
    std::vector<SoundCatalog::Seen> rows;  // 목록에 보이는 순서
    std::string playingKey;
    std::wstring playingName;
    ULONGLONG playStart = 0;
    double playElapsed = 0, playLength = 0;
    int inlineRow = -1;
    WNDPROC inlineOld = nullptr;
};

Window* g_win = nullptr;

std::string selectedKey(Window& w)
{
    const int i = ListView_GetNextItem(w.list, -1, LVNI_SELECTED);
    if (i < 0 || i >= (int)w.rows.size()) return {};
    return w.rows[i].key;
}

void setText(HWND h, const std::wstring& s)
{
    SetWindowTextW(h, s.c_str());
}

void updateMode(Window& w)
{
    SoundCatalog& cat = ffbgm_catalog();
    const std::wstring game = cat.gameCode().empty() ? std::wstring(L"(게임 없음)")
                                                     : W(cat.gameTitle() + " [" + cat.gameCode() + "]");
    if (GetForegroundWindow() == w.hwnd)
        setText(w.status, game + L" — 일시정지 중. 에뮬레이터 창을 누르면 게임이 진행되며 나오는 소리를 모읍니다.\r\n"
                                 L"분류는 바로 저장됩니다: " + W(cat.storagePath()));
    else
        setText(w.status, game + L" — 수집 중. 이 창을 누르면 게임이 일시정지됩니다.");
}

void updatePlayButton(Window& w)
{
    setText(w.playBtn, w.preview.isPlaying() && !w.preview.isPaused() ? L"일시정지" : L"재생");
}

void selectControls(Window& w)
{
    const std::string key = selectedKey(w);
    SoundCatalog& cat = ffbgm_catalog();
    const int user = key.empty() ? SoundCatalog::Kind_Auto : cat.userKind(key);
    w.updatingUi = true;
    for (int i = 0; i < (int)(sizeof(kKinds) / sizeof(kKinds[0])); i++)
        if (kKinds[i] == user) SendMessageW(w.kindBox, CB_SETCURSEL, i, 0);
    setText(w.volEdit, std::to_wstring(key.empty() ? 100 : cat.volume(key)));
    w.updatingUi = false;
}

void refresh(Window& w, bool force)
{
    if (w.inlineEdit) return;  // 볼륨 칸을 고치는 중에는 다시 그리지 않는다
    SoundCatalog& cat = ffbgm_catalog();
    const unsigned v = cat.seenVersion();
    if (!force && v == w.shownVersion) return;
    w.shownVersion = v;
    const std::string keep = selectedKey(w);
    w.rows = cat.seenList();
    SendMessageW(w.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(w.list);
    int select = -1;
    for (int r = 0; r < (int)w.rows.size(); r++)
    {
        const SoundCatalog::Seen& s = w.rows[r];
        std::wstring cols[Col_Num] = {
            std::to_wstring(s.order + 1), typeText(s.type), W(s.name), W(SoundCatalog::kindName(s.autoKind)),
            s.userKind == SoundCatalog::Kind_Auto ? std::wstring(L"-") : W(SoundCatalog::kindName(s.userKind)),
            std::to_wstring(s.volume) + L" %", std::to_wstring(s.count), W(s.key)};
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = r;
        it.pszText = (LPWSTR)cols[0].c_str();
        ListView_InsertItem(w.list, &it);
        for (int c = 1; c < Col_Num; c++)
        {
            LVITEMW sub{};
            sub.iSubItem = c;
            sub.pszText = (LPWSTR)cols[c].c_str();
            SendMessageW(w.list, LVM_SETITEMTEXTW, r, (LPARAM)&sub);
        }
        if (s.key == keep) select = r;
    }
    if (select >= 0)
    {
        ListView_SetItemState(w.list, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(w.list, select, FALSE);
    }
    SendMessageW(w.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(w.list, nullptr, TRUE);
}

void applyKind(Window& w, int kind)
{
    const std::string key = selectedKey(w);
    if (key.empty()) return;
    ffbgm_catalog().setUserKind(key, kind);
    refresh(w, true);
    selectControls(w);
}

void applyVolume(Window& w, const std::string& key, int v)
{
    if (key.empty()) return;
    ffbgm_catalog().setVolume(key, v);
    refresh(w, true);
    selectControls(w);
}

void playToggle(Window& w)
{
    const std::string key = selectedKey(w);
    const ULONGLONG now = GetTickCount64();
    if (w.preview.isPlaying() && !w.preview.isPaused())
    {
        w.preview.setPaused(true);
        w.playElapsed += (now - w.playStart) / 1000.0;
        updatePlayButton(w);
        return;
    }
    if (w.preview.isPlaying() && w.preview.isPaused() && (key.empty() || key == w.playingKey))
    {
        w.preview.setPaused(false);
        w.playStart = now;
        updatePlayButton(w);
        return;
    }
    if (key.empty()) return;
    std::string err;
    if (!w.preview.play(ffbgm_catalog(), key, err))
    {
        MessageBoxW(w.hwnd, W(err).c_str(), L"소리 분류", MB_OK | MB_ICONWARNING);
        return;
    }
    const int r = ListView_GetNextItem(w.list, -1, LVNI_SELECTED);
    w.playingKey = key;
    w.playingName = r >= 0 && r < (int)w.rows.size() ? W(w.rows[r].name) : W(key);
    w.playStart = now;
    w.playElapsed = 0;
    w.playLength = w.preview.lengthSeconds();
    updatePlayButton(w);
}

void tick(Window& w)
{
    refresh(w, false);
    updatePlayButton(w);
    wchar_t buf[512];
    if (w.preview.isPlaying())
    {
        const bool paused = w.preview.isPaused();
        const double sec = w.playElapsed + (paused ? 0 : (GetTickCount64() - w.playStart) / 1000.0);
        if (w.playLength > 0)
            swprintf(buf, 512, L"%s: %s — %.1f초 / %.1f초", paused ? L"일시정지" : L"재생 중", w.playingName.c_str(), sec, w.playLength);
        else
            swprintf(buf, 512, L"%s: %s — %.1f초", paused ? L"일시정지" : L"재생 중", w.playingName.c_str(), sec);
        setText(w.playing, buf);
    }
    else
        setText(w.playing, w.playingName.empty() ? L"" : L"재생 끝: " + w.playingName);
}

// 볼륨 칸 고치기: 그 칸 위에 입력 상자를 띄운다
LRESULT CALLBACK inlineProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    Window& w = *g_win;
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE))
    {
        PostMessageW(w.hwnd, WM_FF_INLINE_DONE, wp == VK_RETURN, 0);
        return 0;
    }
    if (msg == WM_KILLFOCUS) PostMessageW(w.hwnd, WM_FF_INLINE_DONE, 1, 0);
    if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
    return CallWindowProcW(w.inlineOld, h, msg, wp, lp);
}

void beginInlineVolume(Window& w, int row)
{
    RECT rc;
    ListView_GetSubItemRect(w.list, row, Col_Volume, LVIR_LABEL, &rc);
    w.inlineRow = row;
    w.inlineEdit = CreateWindowExW(0, L"EDIT", std::to_wstring(w.rows[row].volume).c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, rc.left, rc.top,
                                   rc.right - rc.left, rc.bottom - rc.top, w.list, (HMENU)ID_INLINE, nullptr, nullptr);
    SendMessageW(w.inlineEdit, WM_SETFONT, SendMessageW(w.list, WM_GETFONT, 0, 0), TRUE);
    w.inlineOld = (WNDPROC)SetWindowLongPtrW(w.inlineEdit, GWLP_WNDPROC, (LONG_PTR)inlineProc);
    SetFocus(w.inlineEdit);
    SendMessageW(w.inlineEdit, EM_SETSEL, 0, -1);
}

void endInline(Window& w, bool apply)
{
    if (!w.inlineEdit) return;
    HWND e = w.inlineEdit;
    w.inlineEdit = nullptr;
    wchar_t buf[32];
    GetWindowTextW(e, buf, 32);
    DestroyWindow(e);
    if (apply && w.inlineRow >= 0 && w.inlineRow < (int)w.rows.size())
        applyVolume(w, w.rows[w.inlineRow].key, (std::min)(_wtoi(buf), (int)SoundCatalog::kMaxVolume));
    w.inlineRow = -1;
}

void kindMenu(Window& w, int row)
{
    const std::string key = w.rows[row].key;
    const int now = ffbgm_catalog().userKind(key);
    HMENU m = CreatePopupMenu();
    for (int i = 0; i < (int)(sizeof(kKinds) / sizeof(kKinds[0])); i++)
        AppendMenuW(m, MF_STRING | (kKinds[i] == now ? MF_CHECKED : 0), 1 + i, kindText(kKinds[i]).c_str());
    RECT rc;
    ListView_GetSubItemRect(w.list, row, Col_User, LVIR_LABEL, &rc);
    POINT pt{rc.left, rc.bottom};
    ClientToScreen(w.list, &pt);
    const int chosen = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN, pt.x, pt.y, 0, w.hwnd, nullptr);
    DestroyMenu(m);
    if (chosen > 0) applyKind(w, kKinds[chosen - 1]);
}

std::wstring fileDialog(Window& w, bool save)
{
    wchar_t path[MAX_PATH * 2] = L"";
    if (save) wcscpy_s(path, (W(ffbgm_catalog().gameCode().empty() ? "sounds" : ffbgm_catalog().gameCode()) + L".txt").c_str());
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = w.hwnd;
    ofn.lpstrFilter = L"소리 분류 (*.txt)\0*.txt\0모든 파일 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? std::wstring(path) : std::wstring();
}

HWND button(HWND parent, const wchar_t* text, int id)
{
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           nullptr, nullptr);
}

void layout(Window& w)
{
    RECT rc;
    GetClientRect(w.hwnd, &rc);
    const int W_ = rc.right, H_ = rc.bottom, m = 8, bh = 26;
    MoveWindow(w.status, m, m, W_ - 2 * m, 36, TRUE);
    const int listTop = m + 40, rowY2 = H_ - m - bh, rowY1 = rowY2 - bh - 6, playY = rowY1 - 24;
    MoveWindow(w.list, m, listTop, W_ - 2 * m, playY - listTop - 4, TRUE);
    MoveWindow(w.playing, m, playY, W_ - 2 * m, 20, TRUE);
    int x = m;
    auto place = [&](int id, int width, int y) {
        MoveWindow(GetDlgItem(w.hwnd, id), x, y, width, bh, TRUE);
        x += width + 6;
    };
    place(ID_PLAY, 80, rowY1);
    place(ID_STOP, 60, rowY1);
    x += 10;
    MoveWindow(GetDlgItem(w.hwnd, 900), x, rowY1 + 5, 36, 20, TRUE);  // "분류:"
    x += 38;
    MoveWindow(w.kindBox, x, rowY1, 130, 200, TRUE);
    x += 136;
    MoveWindow(GetDlgItem(w.hwnd, 901), x, rowY1 + 5, 36, 20, TRUE);  // "볼륨:"
    x += 38;
    place(ID_VOLUME, 50, rowY1);
    MoveWindow(GetDlgItem(w.hwnd, 902), x - 4, rowY1 + 5, 16, 20, TRUE);  // "%"
    x += 16;
    place(ID_RESET, 90, rowY1);
    x = m;
    place(ID_IMPORT, 90, rowY2);
    place(ID_EXPORT, 90, rowY2);
    MoveWindow(GetDlgItem(w.hwnd, ID_CLEAR), W_ - m - 100, rowY2, 100, bh, TRUE);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Window* wp_ = g_win;
    if (!wp_ || (wp_->hwnd && wp_->hwnd != hwnd)) return DefWindowProcW(hwnd, msg, wp, lp);
    Window& w = *wp_;
    switch (msg)
    {
    case WM_CREATE:
    {
        w.hwnd = hwnd;
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        w.status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, nullptr, nullptr, nullptr);
        w.list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                 0, 0, 10, 10, hwnd, (HMENU)ID_LIST, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(w.list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        const wchar_t* heads[Col_Num] = {L"순서", L"종류", L"이름", L"자동 판단", L"지정", L"볼륨", L"나온 횟수", L"이름표"};
        const int widths[Col_Num] = {45, 90, 260, 90, 90, 55, 65, 110};
        for (int c = 0; c < Col_Num; c++)
        {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.pszText = (LPWSTR)heads[c];
            col.cx = widths[c];
            SendMessageW(w.list, LVM_INSERTCOLUMNW, c, (LPARAM)&col);
        }
        w.playing = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, nullptr, nullptr, nullptr);
        w.playBtn = button(hwnd, L"재생", ID_PLAY);
        button(hwnd, L"중지", ID_STOP);
        CreateWindowExW(0, L"STATIC", L"분류:", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, (HMENU)900, nullptr, nullptr);
        w.kindBox = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 10, 10, hwnd,
                                    (HMENU)ID_KIND, nullptr, nullptr);
        for (int k : kKinds) SendMessageW(w.kindBox, CB_ADDSTRING, 0, (LPARAM)kindText(k).c_str());
        CreateWindowExW(0, L"STATIC", L"볼륨:", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, (HMENU)901, nullptr, nullptr);
        w.volEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"100", WS_CHILD | WS_VISIBLE | ES_NUMBER, 0, 0, 10, 10, hwnd,
                                    (HMENU)ID_VOLUME, nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"%", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, (HMENU)902, nullptr, nullptr);
        button(hwnd, L"기본값으로", ID_RESET);
        button(hwnd, L"가져오기...", ID_IMPORT);
        button(hwnd, L"내보내기...", ID_EXPORT);
        button(hwnd, L"목록 비우기", ID_CLEAR);
        EnumChildWindows(hwnd, [](HWND c, LPARAM f) -> BOOL { SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE); return TRUE; }, (LPARAM)font);
        SendMessageW(w.kindBox, CB_SETCURSEL, 0, 0);
        ffbgm_catalog().setCollecting(true);
        SetTimer(hwnd, ID_TIMER, 300, nullptr);
        refresh(w, true);
        return 0;
    }
    case WM_SIZE:
        layout(w);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lp)->ptMinTrackSize = {640, 360};
        return 0;
    case WM_ACTIVATE:
        // 이 창이 앞에 오면 게임을 멈추고, 에뮬레이터 창으로 가면 다시 돌린다 (우리가 건 일시정지만 푼다)
        if (LOWORD(wp) != WA_INACTIVE)
        {
            if (!emu_paused && !w.pausedByUs)
            {
                Pause();
                w.pausedByUs = true;
            }
        }
        else if (w.pausedByUs)
        {
            w.preview.stop();
            Unpause();
            w.pausedByUs = false;
        }
        updateMode(w);
        return 0;
    case WM_TIMER:
        tick(w);
        return 0;
    case WM_NOTIFY:
    {
        NMHDR* nh = (NMHDR*)lp;
        if (nh->idFrom == ID_LIST && nh->code == LVN_ITEMCHANGED)
        {
            NMLISTVIEW* nl = (NMLISTVIEW*)lp;
            if ((nl->uChanged & LVIF_STATE) && (nl->uNewState & LVIS_SELECTED)) selectControls(w);
        }
        else if (nh->idFrom == ID_LIST && nh->code == NM_DBLCLK)
        {
            NMITEMACTIVATE* ia = (NMITEMACTIVATE*)lp;
            if (ia->iItem < 0 || ia->iItem >= (int)w.rows.size()) return 0;
            if (ia->iSubItem == Col_User)
                kindMenu(w, ia->iItem);
            else if (ia->iSubItem == Col_Volume)
                beginInlineVolume(w, ia->iItem);
            else
            {
                w.preview.stop();  // 두 번 클릭은 언제나 그 소리를 처음부터
                playToggle(w);
            }
        }
        return 0;
    }
    case WM_FF_INLINE_DONE:
        endInline(w, wp != 0);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case ID_PLAY: playToggle(w); break;
        case ID_STOP:
            w.preview.stop();
            updatePlayButton(w);
            break;
        case ID_KIND:
            if (HIWORD(wp) == CBN_SELCHANGE && !w.updatingUi)
            {
                const int i = (int)SendMessageW(w.kindBox, CB_GETCURSEL, 0, 0);
                if (i >= 0) applyKind(w, kKinds[i]);
            }
            break;
        case ID_VOLUME:
            if (HIWORD(wp) == EN_KILLFOCUS && !w.updatingUi)
            {
                wchar_t buf[32];
                GetWindowTextW(w.volEdit, buf, 32);
                const std::string key = selectedKey(w);
                if (!key.empty() && _wtoi(buf) != ffbgm_catalog().volume(key))
                    applyVolume(w, key, (std::min)(_wtoi(buf), (int)SoundCatalog::kMaxVolume));
            }
            break;
        case ID_RESET:
        {
            const std::string key = selectedKey(w);
            if (key.empty()) break;
            ffbgm_catalog().resetUser(key);
            refresh(w, true);
            selectControls(w);
            break;
        }
        case ID_IMPORT:
        {
            const std::wstring p = fileDialog(w, false);
            if (p.empty()) break;
            std::string err;
            if (!ffbgm_catalog().importFrom(U(p), err)) MessageBoxW(hwnd, W(err).c_str(), L"소리 분류", MB_OK | MB_ICONWARNING);
            refresh(w, true);
            break;
        }
        case ID_EXPORT:
        {
            const std::wstring p = fileDialog(w, true);
            if (!p.empty() && !ffbgm_catalog().exportTo(U(p)))
                MessageBoxW(hwnd, L"파일을 쓸 수 없습니다.", L"소리 분류", MB_OK | MB_ICONWARNING);
            break;
        }
        case ID_CLEAR:
            ffbgm_catalog().clearSeen();
            refresh(w, true);
            break;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER);
        w.preview.stop();
        ffbgm_catalog().setCollecting(false);
        if (w.pausedByUs)
        {
            Unpause();
            w.pausedByUs = false;
        }
        g_win = nullptr;
        delete &w;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void ffbgm_openSoundWindow(void* parentHwnd)
{
    if (g_win)
    {
        ShowWindow(g_win->hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_win->hwnd);
        return;
    }
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered)
    {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&icc);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"FfBgm1xSoundWindow";
        RegisterClassExW(&wc);
        registered = true;
    }
    g_win = new Window();
    HWND h = CreateWindowExW(0, L"FfBgm1xSoundWindow", L"소리 분류 (ff-bgm1x)", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900,
                             560, (HWND)parentHwnd, nullptr, inst, nullptr);
    if (!h)
    {
        delete g_win;
        g_win = nullptr;
        return;
    }
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
}

void ffbgm_closeSoundWindow()
{
    if (g_win && g_win->hwnd) DestroyWindow(g_win->hwnd);
}
