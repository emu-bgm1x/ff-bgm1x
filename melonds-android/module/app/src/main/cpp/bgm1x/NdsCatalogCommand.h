// ff-bgm1x: 소리 분류 문자열 명령 (안드로이드 앱 화면용, NDS).
// 명령과 답의 형식은 GBA 판(common/catalog_command.h)과 같다. 다른 점:
//  - 이름 바꾸기(label)는 없다 (NDS 는 롬의 사운드 데이터에 소리 이름이 있다)
//  - 저장 폴더는 앱 내부 폴더로 정해져 있어 dir 명령은 무시한다
#ifndef NDSCATALOGCOMMAND_H
#define NDSCATALOGCOMMAND_H

#include <string>

class SoundCatalog;
class SoundPreview;

// cat: 지금 게임의 소리 목록 (없으면 nullptr), usable: 게임 카트가 들어 있는지
std::string ndsCatalogCommand(SoundCatalog* cat, SoundPreview* preview, bool usable, const std::string& cmd);

#endif // NDSCATALOGCOMMAND_H
