// ff-bgm1x: 소리 분류 창 (melonDS·DeSmuME 판과 같은 사양).
//
// 이 창이 앞에 있으면 게임을 일시정지하고, 에뮬레이터 창을 누르면 게임을 다시 돌리며 나온 소리를 목록에 모은다(수집 모드).
// 목록의 소리마다 재생, 분류 지정(BGM / 팡파레·대사 / 연출 / 효과음 / 아무것도 아님), 볼륨, 기본값(자동)으로 되돌리기.
// 지정 칸·볼륨 칸을 두 번 클릭하면 그 자리에서 바꾸고, 그 밖의 칸을 두 번 클릭하면 재생한다.
// 분류는 게임별 파일에 바로 저장하고, 내보내기·가져오기로 다른 사람과 주고받는다.
// 창이 닫혀 있으면 수집하지 않는다 (성능에 영향 없음).
#pragma once

#include <functional>
#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

class FfSoundCatalogDialog : public QDialog {
    Q_OBJECT

public:
    struct Hooks {
        std::function<bool()> is_running;  // 게임이 돌고 있는지 (일시정지·정지가 아님)
        std::function<void()> pause;
        std::function<void()> resume;
    };
    // 하나만 연다 (이미 열려 있으면 앞으로 가져온다)
    static void Open(QWidget* parent, Hooks hooks);
    // 열려 있으면 바로 닫고 지운다 (본 창을 닫을 때)
    static void CloseIfOpen();
    ~FfSoundCatalogDialog() override;

protected:
    void changeEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    FfSoundCatalogDialog(QWidget* parent, Hooks hooks);
    void Refresh(bool force);
    void UpdateMode();
    QString SelectedKey() const;
    void ApplyKind(int kind);
    void UpdatePlayButton();  // 재생 중이면 "일시정지", 아니면 "재생"
    void PauseGame();
    void ResumeGame();

    static FfSoundCatalogDialog* current;
    Hooks hooks;
    QTableWidget* table;
    QComboBox* kind_box;
    QSpinBox* volume_box;
    QLabel* status;
    QLabel* playing;  // 재생 중인 소리
    QPushButton* play_button;
    QTimer* timer;
    unsigned shown_version = ~0u;
    bool paused_by_us = false;
    bool updating_ui = false;
    bool editing = false;  // 볼륨 칸을 고치는 중
    bool closing = false;  // 닫는 중: 이때 오는 '창이 앞으로 옴' 알림으로 다시 일시정지하지 않는다
    QString playing_name;
    QString playing_key;
    double play_elapsed = 0;  // 일시정지 전까지 재생한 시간 (초)
    qint64 play_start_ms = 0;
    double play_length = 0;
};
