// ff-bgm1x: 소리 분류를 문자열 명령으로 다룬다 (안드로이드 앱이 libretro 코어에 보내는 명령).
//
// 코어는 `const char* ff_catalog_command(const char*)` 하나로 이 함수를 내보내고, 앱은 결과 글자를 읽는다.
// 명령 (한 줄, 낱말은 빈칸으로. 이름표에는 빈칸이 없다):
//   dir <폴더>            저장 폴더 (앱의 파일 폴더). 게임 파일을 다시 읽는다          -> ok
//   state                 game\t<코드>\t<제목> / collect\t0|1 / version\t<n> / path\t<파일> / usable\t0|1
//   list                  줄마다: 이름표\t종류\t이름\t붙인 이름\t자동\t지정\t볼륨\t횟수\t순서
//   collect 0|1           수집 켜고 끄기                                                  -> ok
//   kind <이름표> <분류>   분류 (SoundCatalog::Kind_ 값, -1 = 자동)                        -> ok
//   vol <이름표> <%>       볼륨                                                           -> ok
//   label <이름표> [이름]  이름 붙이기 (뒤 글자 전체, 비우면 원래 이름)                    -> ok
//   reset <이름표>         분류·볼륨을 기본값으로                                          -> ok
//   clear                 목록 비우기                                                     -> ok
//   export                파일 내용                                                       -> 글자
//   import\n<파일 내용>    가져오기                                                        -> ok / error\t<까닭>
//   play <이름표>          재생 (게임이 멈춰 있어도 ff_preview_render 로 나간다)           -> ok / error\t<까닭>
//   stop / pause 0|1      재생 멈춤 / 일시정지                                             -> ok
//   pstate                playing\t0|1 / paused\t0|1 / length\t<초>
// 모르는 명령은 error\t<까닭>.
#pragma once

#include <cstdint>
#include <string>

#include "sound_catalog.h"

// 코어마다 만드는 재생 기능
struct CatalogPreview {
    virtual ~CatalogPreview() = default;
    virtual bool play(const std::string& key, std::string& error) = 0;
    virtual void stop() = 0;
    virtual void setPaused(bool paused) = 0;
    virtual bool playing() const = 0;
    virtual bool paused() const = 0;
    virtual double length() const = 0;  // 알면 초, 모르면 0
    // 오디오 출력: stereo 에 frames 개를 더한다. 재생 중이 아니면 0 을 돌려준다 (그대로 둔다)
    virtual int render(int16_t* stereo, int frames, int sampleRate) = 0;
};

// cat 이 nullptr 이면(게임 없음) state·dir 만 답한다. usable = 이 게임의 소리를 알아볼 수 있는지
std::string catalogCommand(SoundCatalog* cat, CatalogPreview* preview, bool usable, const std::string& cmd);
