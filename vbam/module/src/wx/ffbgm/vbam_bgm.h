// ff-bgm1x: 배속과 관계없이 GBA BGM·효과음을 1배속으로 낸다 (VBA-M 연결부).
// 게임의 MP2K 플레이어를 무음 처리하고, 같은 곡을 내장 재생기(bgm_mirror)로
//  - 실시간: SDL3 오디오 스트림을 하나 더 열어 실제 시간에 맞춰 낸다 (출력 드라이버가 배속에 맞춰 재생 속도를 바꾸거나
//    소리를 버려도 영향이 없게). 장치는 VBA-M 오디오 설정에서 고른 장치를 따른다.
//  - 녹화(Tools -> Record): 게임 시간 기준으로 따로 만들어 녹화 소리에 섞는다.
// 모든 함수는 에뮬레이션(메인) 스레드에서 부른다.
#ifndef VBAM_WX_FFBGM_VBAM_BGM_H_
#define VBAM_WX_FFBGM_VBAM_BGM_H_

#include <cstddef>
#include <cstdint>

namespace ffbgm {

// GBA 롬을 불러온 뒤 (패치 적용 뒤의 크기)
void OnGbaLoaded(const uint8_t* rom, size_t size);
// GB/GBC 롬을 불러온 뒤 (지원 엔진: 포켓몬 금·은·크리스탈. mGBA 의 GB 코어를 숨겨진 코어로 쓴다)
void OnGbLoaded(const uint8_t* rom, size_t size);
// 게임을 내릴 때
void OnUnload();
// 프레임마다 (systemFrame)
void OnFrame();
// 연출 중이고 "연출 중에도 배속" 이 꺼져 있으면 true: 배속 버튼을 무시한다
bool SpeedHold();
// 녹화로 가는 소리(16비트 스테레오, frames 개)에 1배속 소리를 더한다 (녹화 중일 때만 부른다)
void MixRecording(int16_t* stereo, int frames);

}  // namespace ffbgm

#endif  // VBAM_WX_FFBGM_VBAM_BGM_H_
