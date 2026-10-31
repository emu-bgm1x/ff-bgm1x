/* ff-bgm1x: 정적 libepoxy 초기화 (Windows, 정적 빌드 전용).
 * libepoxy 는 OpenGL 함수표를 스레드별 저장소(TLS)에 두고, 그 준비를 DllMain 에서 한다.
 * 정적 라이브러리로 실행 파일에 넣으면 DllMain 이 불리지 않아 화면을 그리기 시작할 때 멈춘다.
 * 실행 파일의 TLS 콜백은 DLL 의 DllMain 과 같은 때(프로세스·스레드 시작과 끝)에 불리므로 거기서 대신 부른다. */
#include <windows.h>

extern "C" BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, LPVOID reserved);  // libepoxy.a(dispatch_wgl)

static void NTAPI epoxyTlsCallback(PVOID module, DWORD reason, PVOID reserved) {
	DllMain(static_cast<HINSTANCE>(module), reason, reserved);
}

extern "C" {
// .CRT$XL? 구역의 포인터는 C 런타임의 TLS 콜백 표에 들어간다
__attribute__((section(".CRT$XLF"), used)) PIMAGE_TLS_CALLBACK ffBgm1xEpoxyTlsCallback = epoxyTlsCallback;
}
