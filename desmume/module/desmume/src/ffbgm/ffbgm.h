// ff-bgm1x: 배속 중에도 BGM 을 1배속으로 내는 모듈의 바깥 인터페이스 (DeSmuME 쪽 호출부가 쓴다)
#ifndef FFBGM_H
#define FFBGM_H

#include "types.h"

void ffbgm_onRomLoaded(const char* romPath);  // 롬을 불러온 뒤 (메인 스레드). romPath: 롬 파일
void ffbgm_onReset();                         // 리셋 뒤
void ffbgm_onStateLoaded();                   // 상태 불러오기 뒤
void ffbgm_onArm9Fifo(u32 word);              // ARM9 가 IPC FIFO 로 값을 보낼 때
void ffbgm_onCartRead(u32 addr);              // 게임이 카트에서 롬을 읽을 때 (명령 0xB7, 0x200 바이트마다)
void ffbgm_afterMainFrame();                  // 원본 코어가 프레임 하나를 돌린 뒤
void ffbgm_mix(s16* stream, int frames);      // 오디오 출력 직전: 1배속 소리를 더한다
bool ffbgm_cutsceneHold();                    // 연출 중이고 "연출 중에도 배속"이 꺼져 있으면 true

// 설정 (메뉴·설정 파일에서 쓴다)
void ffbgm_setBgm1x(bool on);
bool ffbgm_bgm1x();
void ffbgm_setFfCutscenes(bool on);          // true = 연출 중에도 배속
bool ffbgm_ffCutscenes();
void ffbgm_setSfx1x(bool on);
bool ffbgm_sfx1x();
void ffbgm_setSfxVolume(int volume);        // 0~256
int ffbgm_sfxVolume();

// 소리 분류 창 (ffbgm_soundwin.cpp)
void ffbgm_openSoundWindow(void* parentHwnd);  // 열기 (이미 열려 있으면 앞으로)
void ffbgm_closeSoundWindow();                 // 닫기 (프로그램을 끝낼 때)

#endif // FFBGM_H
