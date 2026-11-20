// ff-bgm1x: 소리 분류 창 (wxWidgets 판). 설명은 sound_dialog.h
#include "wx/ffbgm/sound_dialog.h"

#include <algorithm>
#include <string>
#include <vector>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/numdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/time.h>
#include <wx/timer.h>

#include "wx/ffbgm/sound_catalog.h"
#include "wx/ffbgm/vbam_bgm.h"
#include "wx/wxvbam.h"

namespace ffbgm {

namespace {

const int kKinds[] = {SoundCatalog::Kind_Auto,     SoundCatalog::Kind_Bgm, SoundCatalog::Kind_Jingle,
                      SoundCatalog::Kind_Cutscene, SoundCatalog::Kind_Sfx, SoundCatalog::Kind_None};
const int kNumKinds = static_cast<int>(sizeof(kKinds) / sizeof(kKinds[0]));

enum Column { Col_Order, Col_Type, Col_Name, Col_Auto, Col_User, Col_Volume, Col_Count, Col_Key, Col_Num };

wxString U(const char* s) {
    return wxString::FromUTF8(s);
}

wxString U(const std::string& s) {
    return wxString::FromUTF8(s.c_str());
}

wxString KindLabel(int kind) {
    return kind == SoundCatalog::Kind_Auto ? U("자동 (기본값)") : U(SoundCatalog::KindName(kind));
}

wxString TypeName(int type) {
    switch (type) {
        case SoundCatalog::Type_Song:
            return U("곡");
        case SoundCatalog::Type_Cry:
            return U("울음소리");
        case SoundCatalog::Type_Sfx:
            return U("효과음");
    }
    return U("기타");
}

class SoundDialog;
SoundDialog* g_current = nullptr;

class SoundDialog final : public wxDialog {
public:
    explicit SoundDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, U("소리 분류 (ff-bgm1x)"), wxDefaultPosition, wxSize(920, 540),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          timer_(this) {
        wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);
        status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
        top->Add(status_, 0, wxEXPAND | wxALL, 6);

        list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
        const char* heads[Col_Num] = {"순서", "종류", "이름", "자동 판단", "지정", "볼륨", "나온 횟수", "이름표"};
        const int widths[Col_Num] = {50, 70, 300, 90, 90, 60, 70, 160};
        for (int c = 0; c < Col_Num; ++c)
            list_->AppendColumn(U(heads[c]), wxLIST_FORMAT_LEFT, widths[c]);
        top->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

        playing_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
        top->Add(playing_, 0, wxEXPAND | wxALL, 6);

        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        playButton_ = new wxButton(this, wxID_ANY, U("재생"));
        wxButton* stopButton = new wxButton(this, wxID_ANY, U("중지"));
        kindBox_ = new wxChoice(this, wxID_ANY);
        for (int k : kKinds)
            kindBox_->Append(KindLabel(k));
        kindBox_->SetSelection(0);
        volumeBox_ = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS,
                                    0, SoundCatalog::kMaxVolume, 100);
        volumeBox_->SetToolTip(U("이 소리의 볼륨 (100 = 그대로, 최대 200). 1배속으로 내는 소리와 원본 그대로 나오는 소리 모두에 적용됩니다."));
        wxButton* resetButton = new wxButton(this, wxID_ANY, U("기본값으로"));
        row->Add(playButton_, 0, wxRIGHT, 4);
        row->Add(stopButton, 0, wxRIGHT, 16);
        row->Add(new wxStaticText(this, wxID_ANY, U("분류:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        row->Add(kindBox_, 0, wxRIGHT, 8);
        row->Add(new wxStaticText(this, wxID_ANY, U("볼륨(%):")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        row->Add(volumeBox_, 0, wxRIGHT, 8);
        row->Add(resetButton, 0);
        top->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

        wxBoxSizer* row2 = new wxBoxSizer(wxHORIZONTAL);
        wxButton* importButton = new wxButton(this, wxID_ANY, U("가져오기..."));
        wxButton* exportButton = new wxButton(this, wxID_ANY, U("내보내기..."));
        wxButton* clearButton = new wxButton(this, wxID_ANY, U("목록 비우기"));
        row2->Add(importButton, 0, wxRIGHT, 4);
        row2->Add(exportButton, 0);
        row2->AddStretchSpacer(1);
        row2->Add(clearButton, 0);
        top->Add(row2, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
        SetSizer(top);

        playButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnPlay(); });
        stopButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            PreviewStop();
            UpdatePlayButton();
        });
        resetButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            SoundCatalog* cat = Cat();
            const std::string key = SelectedKey();
            if (!cat || key.empty())
                return;
            cat->ResetUser(key);
            RefreshList(true);
            SyncControls();
        });
        kindBox_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            if (updating_)
                return;
            const int i = kindBox_->GetSelection();
            if (i >= 0 && i < kNumKinds)
                ApplyKind(kKinds[i]);
        });
        volumeBox_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
            if (updating_)
                return;
            SoundCatalog* cat = Cat();
            const std::string key = SelectedKey();
            if (!cat || key.empty())
                return;
            cat->SetVolume(key, volumeBox_->GetValue());
            RefreshList(true);
        });
        list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { SyncControls(); });
        list_->Bind(wxEVT_LIST_COL_CLICK, [this](wxListEvent& e) {
            // 칸 제목을 누르면 그 칸으로 정렬 (다시 누르면 반대로)
            if (sortColumn_ == e.GetColumn())
                sortDescending_ = !sortDescending_;
            else {
                sortColumn_ = e.GetColumn();
                sortDescending_ = false;
            }
            RefreshList(true);
        });
        list_->Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent& e) { OnDoubleClick(e.GetPosition()); });
        importButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            SoundCatalog* cat = Cat();
            if (!cat)
                return;
            wxFileDialog dlg(this, U("소리 분류 가져오기"), wxEmptyString, wxEmptyString,
                             U("소리 분류 (*.txt)|*.txt|모든 파일 (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dlg.ShowModal() != wxID_OK)
                return;
            std::string err;
            if (!cat->ImportFrom(std::string(dlg.GetPath().utf8_str()), err))
                wxMessageBox(U(err), GetTitle(), wxOK | wxICON_WARNING, this);
            RefreshList(true);
        });
        exportButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            SoundCatalog* cat = Cat();
            if (!cat)
                return;
            const std::string code = cat->GameCode();
            wxFileDialog dlg(this, U("소리 분류 내보내기"), wxEmptyString, U((code.empty() ? std::string("sounds") : code) + ".txt"),
                             U("소리 분류 (*.txt)|*.txt"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (dlg.ShowModal() != wxID_OK)
                return;
            if (!cat->ExportTo(std::string(dlg.GetPath().utf8_str())))
                wxMessageBox(U("파일을 쓸 수 없습니다."), GetTitle(), wxOK | wxICON_WARNING, this);
        });
        clearButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (SoundCatalog* cat = Cat())
                cat->ClearSeen();
            RefreshList(true);
        });
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { OnTimer(); });
        Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& e) {
            e.Skip();
            OnActivate(e.GetActive());
        });
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { Shutdown(); });

        Cat();
        RefreshList(true);
        UpdateMode(true);
        timer_.Start(300);
    }

    // 본 창이 닫히며 같이 지워질 때 (Shutdown 을 거치지 않은 경우)
    ~SoundDialog() override {
        timer_.Stop();
        PreviewStop();
        if (g_current == this)
            g_current = nullptr;
    }

    // 닫을 때: 재생 멈춤, 수집 끔, 우리가 건 일시정지 풀기
    void Shutdown() {
        if (closing_)
            return;
        closing_ = true;
        timer_.Stop();
        PreviewStop();
        if (SoundCatalog* cat = Catalog())
            cat->SetCollecting(false);
        ResumeGame();
        if (g_current == this)
            g_current = nullptr;
        Destroy();
    }

private:
    SoundCatalog* Cat() {
        SoundCatalog* cat = Catalog();
        if (cat != shownCatalog_) {
            PreviewStop();
            shownCatalog_ = cat;
            shownVersion_ = ~0u;
        }
        if (cat && !closing_ && !cat->Collecting())
            cat->SetCollecting(true);
        return cat;
    }

    std::string SelectedKey() const {
        const long r = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (r < 0 || r >= static_cast<long>(rows_.size()))
            return {};
        return rows_[r].key;
    }

    void SyncControls() {
        SoundCatalog* cat = Cat();
        const std::string key = SelectedKey();
        const int user = (!cat || key.empty()) ? SoundCatalog::Kind_Auto : cat->UserKind(key);
        updating_ = true;
        for (int i = 0; i < kNumKinds; ++i)
            if (kKinds[i] == user)
                kindBox_->SetSelection(i);
        volumeBox_->SetValue((!cat || key.empty()) ? 100 : cat->Volume(key));
        updating_ = false;
    }

    void ApplyKind(int kind) {
        SoundCatalog* cat = Cat();
        const std::string key = SelectedKey();
        if (!cat || key.empty())
            return;
        cat->SetUserKind(key, kind);
        RefreshList(true);
    }

    static bool Less(const SoundCatalog::Seen& a, const SoundCatalog::Seen& b, int col) {
        switch (col) {
            case Col_Type:
                return a.type < b.type;
            case Col_Name:
                return (a.label.empty() ? a.name : a.label) < (b.label.empty() ? b.name : b.label);
            case Col_Auto:
                return a.auto_kind < b.auto_kind;
            case Col_User:
                return a.user_kind < b.user_kind;
            case Col_Volume:
                return a.volume < b.volume;
            case Col_Count:
                return a.count < b.count;
            case Col_Key:
                return a.key < b.key;
        }
        return a.order < b.order;
    }

    void RefreshList(bool force) {
        SoundCatalog* cat = Cat();
        const unsigned v = cat ? cat->SeenVersion() : 0;
        if (!force && v == shownVersion_)
            return;
        shownVersion_ = v;
        const std::string keep = SelectedKey();
        rows_ = cat ? cat->SeenList() : std::vector<SoundCatalog::Seen>();
        const int col = sortColumn_;
        const bool desc = sortDescending_;
        std::stable_sort(rows_.begin(), rows_.end(), [col, desc](const SoundCatalog::Seen& a, const SoundCatalog::Seen& b) {
            return desc ? Less(b, a, col) : Less(a, b, col);
        });
        list_->Freeze();
        if (list_->GetItemCount() != static_cast<int>(rows_.size())) {
            list_->DeleteAllItems();
            for (size_t r = 0; r < rows_.size(); ++r)
                list_->InsertItem(static_cast<long>(r), wxEmptyString);
        }
        for (size_t r = 0; r < rows_.size(); ++r) {
            const SoundCatalog::Seen& s = rows_[r];
            const long i = static_cast<long>(r);
            list_->SetItem(i, Col_Order, wxString::Format("%u", static_cast<unsigned>(s.order + 1)));
            list_->SetItem(i, Col_Type, TypeName(s.type));
            list_->SetItem(i, Col_Name, U(s.label.empty() ? s.name : s.label));
            list_->SetItem(i, Col_Auto, U(SoundCatalog::KindName(s.auto_kind)));
            list_->SetItem(i, Col_User, s.user_kind == SoundCatalog::Kind_Auto ? wxString("-") : U(SoundCatalog::KindName(s.user_kind)));
            list_->SetItem(i, Col_Volume, wxString::Format("%d %%", s.volume));
            list_->SetItem(i, Col_Count, wxString::Format("%d", s.count));
            list_->SetItem(i, Col_Key, U(s.key));
            const bool sel = !keep.empty() && s.key == keep;
            list_->SetItemState(i, sel ? wxLIST_STATE_SELECTED : 0, wxLIST_STATE_SELECTED);
        }
        list_->Thaw();
    }

    void OnDoubleClick(const wxPoint& pos) {
        int flags = 0;
        long col = -1;
        const long r = list_->HitTest(pos, flags, &col);
        if (r < 0 || r >= static_cast<long>(rows_.size()))
            return;
        list_->SetItemState(r, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        SoundCatalog* cat = Cat();
        const std::string key = rows_[r].key;
        if (!cat)
            return;
        if (col == Col_User) {
            // 지정 칸: 분류 고르기
            wxMenu menu;
            const int now = cat->UserKind(key);
            for (int i = 0; i < kNumKinds; ++i) {
                wxMenuItem* item = menu.AppendCheckItem(wxID_HIGHEST + 100 + i, KindLabel(kKinds[i]));
                item->Check(kKinds[i] == now);
            }
            const int chosen = list_->GetPopupMenuSelectionFromUser(menu, pos);
            if (chosen >= wxID_HIGHEST + 100 && chosen < wxID_HIGHEST + 100 + kNumKinds) {
                cat->SetUserKind(key, kKinds[chosen - wxID_HIGHEST - 100]);
                RefreshList(true);
                SyncControls();
            }
            return;
        }
        if (col == Col_Volume) {
            // 볼륨 칸: 볼륨 고치기
            const long v = wxGetNumberFromUser(U("이 소리의 볼륨 (%)"), U("볼륨:"), GetTitle(), cat->Volume(key), 0,
                                               SoundCatalog::kMaxVolume, this);
            if (v >= 0) {
                cat->SetVolume(key, static_cast<int>(v));
                RefreshList(true);
                SyncControls();
            }
            return;
        }
        if (col == Col_Name) {
            // 이름 칸: 이름 바꾸기 (비우면 원래 이름)
            const std::string label = cat->Label(key);
            wxTextEntryDialog dlg(this, U("이 소리의 이름 (비우면 원래 이름: ") + U(rows_[r].name) + U(")"), GetTitle(),
                                  U(label.empty() ? rows_[r].name : label));
            if (dlg.ShowModal() == wxID_OK) {
                wxString text = dlg.GetValue();
                text.Trim(true).Trim(false);
                const std::string t(text.utf8_str());
                if (t != (label.empty() ? rows_[r].name : label) || (t.empty() && !label.empty()))
                    cat->SetLabel(key, t);
                RefreshList(true);
            }
            return;
        }
        OnPlay();
    }

    // 재생/일시정지: 재생 중이면 일시정지, 같은 소리를 일시정지해 둔 상태면 이어서 재생, 그 밖에는 고른 소리를 처음부터
    void OnPlay() {
        const std::string key = SelectedKey();
        const wxLongLong now = wxGetLocalTimeMillis();
        if (PreviewPlaying() && !PreviewPaused()) {
            PreviewSetPaused(true);
            elapsed_ += (now - startMs_).ToDouble() / 1000.0;
            UpdatePlayButton();
            return;
        }
        if (PreviewPaused() && (key.empty() || key == playingKey_)) {
            PreviewSetPaused(false);
            startMs_ = now;
            UpdatePlayButton();
            return;
        }
        if (key.empty())
            return;
        std::string err;
        if (!PreviewPlay(key, err)) {
            wxMessageBox(U(err), GetTitle(), wxOK | wxICON_WARNING, this);
            return;
        }
        const long r = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        playingName_ = r >= 0 ? list_->GetItemText(r, Col_Name) : U(key);
        playingKey_ = key;
        startMs_ = now;
        elapsed_ = 0;
        length_ = PreviewLength();
        UpdatePlayButton();
    }

    void UpdatePlayButton() {
        const wxString text = PreviewPlaying() && !PreviewPaused() ? U("일시정지") : U("재생");
        if (playButton_->GetLabel() != text)
            playButton_->SetLabel(text);
    }

    void OnTimer() {
        RefreshList(false);
        UpdateMode(false);
        UpdatePlayButton();
        if (PreviewPlaying()) {
            const bool paused = PreviewPaused();
            const double sec = elapsed_ + (paused ? 0.0 : (wxGetLocalTimeMillis() - startMs_).ToDouble() / 1000.0);
            wxString t = (paused ? U("일시정지: ") : U("재생 중: ")) + playingName_ + wxString::Format(" - %.1f", sec) + U("초");
            if (length_ > 0)
                t += wxString::Format(" / %.1f", length_) + U("초");
            playing_->SetLabel(t);
        } else {
            playing_->SetLabel(playingName_.empty() ? wxString() : U("재생 끝: ") + playingName_);
        }
    }

    void UpdateMode(bool force) {
        SoundCatalog* cat = Cat();
        wxString game;
        if (!cat || !CatalogUsable())
            game = U("(게임 없음, 또는 소리를 알아볼 수 없는 게임)");
        else
            game = U(cat->GameTitle() + " [" + cat->GameCode() + "]");
        wxString text;
        if (IsActive())
            text = game + U(" — 일시정지 중. 에뮬레이터 창을 누르면 게임이 진행되며 나오는 소리를 모읍니다.\n분류는 바로 저장됩니다: ") +
                   U(cat ? cat->StoragePath() : std::string());
        else
            text = game + U(" — 수집 중. 이 창을 누르면 게임이 일시정지됩니다.");
        if (force || status_->GetLabel() != text) {
            status_->SetLabel(text);
            Layout();
        }
    }

    void PauseGame() {
        MainFrame* frame = wxGetApp().frame;
        GameArea* panel = frame ? frame->GetPanel() : nullptr;
        if (pausedByUs_ || !panel || panel->game_type() == IMAGE_UNKNOWN || frame->IsPaused())
            return;
        panel->Pause();
        pausedByUs_ = true;
    }

    void ResumeGame() {
        if (!pausedByUs_)
            return;
        pausedByUs_ = false;
        MainFrame* frame = wxGetApp().frame;
        GameArea* panel = frame ? frame->GetPanel() : nullptr;
        if (panel && panel->game_type() != IMAGE_UNKNOWN && !frame->IsPaused())
            panel->Resume();
    }

    void OnActivate(bool active) {
        if (closing_)
            return;
        // 이 창이 앞에 오면 게임을 멈추고, 에뮬레이터 창으로 가면 다시 돌린다 (우리가 건 일시정지만 푼다)
        if (active) {
            PauseGame();
        } else if (pausedByUs_) {
            PreviewStop();
            ResumeGame();
        }
        UpdateMode(true);
    }

    wxTimer timer_;
    wxStaticText* status_ = nullptr;
    wxStaticText* playing_ = nullptr;
    wxListCtrl* list_ = nullptr;
    wxButton* playButton_ = nullptr;
    wxChoice* kindBox_ = nullptr;
    wxSpinCtrl* volumeBox_ = nullptr;
    std::vector<SoundCatalog::Seen> rows_;
    SoundCatalog* shownCatalog_ = nullptr;
    unsigned shownVersion_ = ~0u;
    int sortColumn_ = Col_Order;
    bool sortDescending_ = false;
    bool updating_ = false;
    bool closing_ = false;
    bool pausedByUs_ = false;
    wxString playingName_;
    std::string playingKey_;
    wxLongLong startMs_ = 0;
    double elapsed_ = 0;
    double length_ = 0;
};

}  // namespace

void OpenSoundDialog(wxWindow* parent) {
    if (g_current) {
        g_current->Show();
        g_current->Raise();
        return;
    }
    g_current = new SoundDialog(parent);
    g_current->Show();
}

void CloseSoundDialog() {
    if (g_current)
        g_current->Shutdown();
}

}  // namespace ffbgm
