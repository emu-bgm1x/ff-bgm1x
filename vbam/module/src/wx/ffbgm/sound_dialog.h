// ff-bgm1x: 소리 분류 창 (mGBA·melonDS·DeSmuME·Azahar 판과 같은 사양, wxWidgets 판).
//
// 이 창이 앞에 있으면 게임을 일시정지하고, 에뮬레이터 창을 누르면 게임을 다시 돌리며 나온 소리를 목록에 모은다(수집 모드).
// 목록의 소리마다 재생, 분류 지정(BGM / 팡파레·대사 / 연출 / 효과음 / 아무것도 아님), 볼륨, 기본값(자동)으로 되돌리기.
// 이름·지정·볼륨 칸을 두 번 클릭하면 바꾸고, 그 밖의 칸을 두 번 클릭하면 재생한다.
// 분류는 게임별 파일에 바로 저장하고, 내보내기·가져오기로 다른 사람과 주고받는다.
// 창이 닫혀 있으면 수집하지 않는다 (성능에 영향 없음).
#ifndef VBAM_WX_FFBGM_SOUND_DIALOG_H_
#define VBAM_WX_FFBGM_SOUND_DIALOG_H_

class wxWindow;

namespace ffbgm {

// 하나만 연다 (이미 열려 있으면 앞으로 가져온다)
void OpenSoundDialog(wxWindow* parent);
// 열려 있으면 닫는다 (본 창을 닫을 때)
void CloseSoundDialog();

}  // namespace ffbgm

#endif  // VBAM_WX_FFBGM_SOUND_DIALOG_H_
