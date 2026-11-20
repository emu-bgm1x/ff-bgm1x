# LibretroDroid — ff-bgm1x

기준: Swordfish90/LibretroDroid 커밋 `8835c30`. Lemuroid(GBA) 판의 일부입니다.
구조·끼우는 지점·순서·확인 방법은 **`lemuroid/PORTING-AI.md`** 에 함께 있습니다.

- `module/`: `sensors.cpp/.h` (NDK 센서를 libretro 센서 인터페이스로)
- `hooks.patch`: 1배속 소리 후처리 믹서, 배속 중 연출 곡 멈춤, 센서, 늘이기·화면 비율, JNI
