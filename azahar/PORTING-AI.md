# Azahar (3DS) 이식 안내서 — ff-bgm1x

기준: Azahar **2126.2** 태그. 기능 설명과 메뉴는 `FEATURES-ko.txt` 를 보세요.

## 1. 구조 한눈에

새 코드는 `src/audio_core/hle/` 의 새 파일 두 묶음에 있고, 원본 파일에는 그 코드를 부르는 짧은 연결부만 있습니다.

| 새 파일 (`module/`) | 하는 일 |
|---|---|
| `bgm_replacer.cpp/.h` | 중심. 게임 RomFS 의 BGM 파일(.bcstm, .aac)을 색인하고, DSP 음성에 들어오는 버퍼를 보고 BGM 인지 알아낸 뒤, 그 음성은 원본 믹스에서 빼고 같은 곡을 직접 해독해 실제 시간 기준(1배속)으로 출력에 섞는다. 곡 분류(반복 곡, 팡파레, 연출 곡), 대사 겹침 소리, 연출 중 배속 멈춤 판단, 효과음 미러의 켜고 끄기·볼륨도 여기서 한다. 싱글턴 `BgmReplacer::Instance()`. |
| `sfx_mirror.cpp/.h` | 효과음 음성 미러. 배속 중에 DSP 음성이 새 소리를 시작하면, 그 음성의 실제 출력(해독·음높이·필터 적용 후, 게임이 준 믹스별 볼륨 × 믹서 볼륨을 곱해 스테레오로 접은 것)을 게임 시간 그대로 쌓고, 출력 쪽에서 원래 샘플레이트로(=1배속) 다시 낸다. 게임이 계속 내는 소리가 2.5초 넘게 밀리면 따라잡는다. 사운드 라이브러리(시퀀스, 뱅크)는 해석하지 않는다. |

효과음 쪽은 **게임 데이터를 해석하지 않고 DSP 음성의 출력만 다시 낸다** 는 것이 핵심이다. 새 버전에서도 이 원칙을 지킬 것
(악보·뱅크를 읽어 다시 연주하는 방식은 v3 에서 시도했다가, 변형·볼륨·멈춤을 게임마다 맞히지 못해 버렸다).

에뮬레이터 내부 의존 (새 버전에서 이름이 바뀌었는지 먼저 확인할 것):
`Core::System::GetInstance().GetAppLoader().ReadRomFS / ReadProgramId`, `FileSys::RomFSReader`,
`FileUtil::GetExeDirectory / GetUserPath(ConfigDir)`, `Settings::GetFrameLimit` 와 아래 `cutscene_speed_hold`,
`StereoFrame16`·`native_sample_rate`·`samples_per_frame` (`audio_core/audio_types.h`), `LOG_*` 매크로, FAAD2(`neaacdec.h`).

## 2. 원본에 끼우는 지점 (`hooks/`)

패치가 그대로 안 들어가면 **'역할'** 을 기준으로 새 버전에서 같은 일을 하는 곳을 찾으세요.

| 원본 파일 | 끼우는 곳 | 역할 |
|---|---|---|
| `audio_core/CMakeLists.txt` | `audio_core` 라이브러리 소스 목록 | 새 파일 4개를 빌드에 넣는다 |
| `audio_core/dsp_interface.cpp` | `DspInterface::OutputCallback`, 하드웨어 볼륨 적용 **직전** | 오디오 장치가 요청한 분량 전체에 `Mix(buffer, frames, native_rate)` — 1배속 BGM·효과음을 출력에 더한다. 에뮬레이션이 멈칫해도 끊기지 않게 출력 콜백에서 한다 |
| `audio_core/hle/hle.cpp` | `DspHle::Impl::Initialize` | `Reset()` — 게임을 새로 켤 때 상태 초기화 |
| 〃 | `GenerateCurrentFrame` 맨 앞 | `OnDspFrame()` — DSP 오디오 프레임(160 샘플)마다. **게임 시간**의 기준 |
| `audio_core/hle/mixers.cpp` | `Mixers::ParseConfig` | `UpdateMixerVolumes(intermediate_mixer_volume)` — 대체 BGM·효과음 미러도 게임 믹서 볼륨을 따르게 |
| `audio_core/hle/source.h` | `Source` 멤버 | `bool bgm_replaced`, `bool mirror_new`, `bool mirrored` |
| `audio_core/hle/source.cpp` | `Source::ParseConfig` 의 enable 처리 | 꺼져 있던 음성이 켜지면 `mirror_new = true` (새 소리) |
| 〃 | `ParseConfig` 의 `embedded_buffer_dirty` 처리 | `mirror_new = true` (새 임베디드 버퍼 = 음 하나의 시작) |
| 〃 | `Source::DequeueBuffer` 의 ADPCM 분기 | `OnAdpcmBuffer(...)` 결과로 `bgm_replaced` 설정 (계수+데이터로 BGM 스트림 판별) |
| 〃 | 같은 함수 PCM16 분기 | `OnPcmBuffer(...)` (AAC 를 푼 버퍼 판별), 아니면 `OnSourceIdle` |
| 〃 | PCM8 분기 | `bgm_replaced` 해제 + `OnSourceIdle` |
| 〃 | `Source::MixInto` | 비활성 음성: `OnSourceIdle`, 미러 중이면 `MirrorEnd`. `bgm_replaced` 음성: 원본 믹스에 넣지 않고 `UpdateSourceGain(id, gain)` 만. 그 밖의 음성: 믹스 0 을 처리할 때 `mirror_new` 면 `MirrorBegin(id)` (배속 중이면 true), 미러 중이면 `MirrorFeed(id, current_frame, gain)` 하고 원본 믹스에 넣지 않는다 |
| 〃 | `Source::Reset` | `OnSourceIdle`, 미러 중이면 `MirrorEnd`, `mirror_new = true` |
| `audio_core/hle/aac_decoder.cpp` | `AACDecoder::Decode`, 실제 해독 직전 | `OnAacDecode(data, len, dst_ch0, dst_ch1)` — AAC 프레임이 어느 파일 몇 번째인지, 어디로 풀리는지 |
| `core/file_sys/romfs_reader.cpp` | `DirectRomFSReader::ReadFile` 맨 앞 | `OnRomFsRead(*this, offset, length)` — 동영상(.moflex) 읽기 = 연출 감지 |
| `core/hle/service/cam/y2r_u.cpp` | `Y2R_U::StartConversion` 맨 앞 | `OnVideoFrame()` — 동영상 화면 변환 = 연출 감지 |
| `common/settings.h/.cpp` | `GetFrameLimit()` | 전역 `std::atomic<bool> cutscene_speed_hold`. 켜져 있으면 제한 없음(0)이나 100 초과 속도를 100 으로 — 연출 중 배속 멈춤. BgmReplacer 가 켜고 끈다. 효과음 미러는 `GetFrameLimit()` 가 0 이거나 100 초과일 때(=배속 중)만 켠다 |
| `citra_qt/bootmanager.h` | `EmuThread::SetRunning` | `SetEmulationRunning(running)` — 사용자 일시정지 때만 1배속 소리도 멈춤 |
| `citra_qt/citra_qt.cpp` | `GMainWindow::ConnectMenuEvents` 맨 앞 | Emulation 메뉴에 체크 항목 3개 (BGM 1배속 유지 / 연출 중에도 배속 / 대사·효과음 1배속). `QSettings("ff-bgm1x","azahar")` 에 저장, 시작 때 읽어 `SetEnabled / SetFastForwardCutscenes / SetVoices1x / SetSfxVolume` |
| 〃 | `InitializeHotkeys` | 단축키 3개가 위 메뉴 항목(objectName `ff_bgm1x_*`)을 토글 |
| `citra_qt/configuration/config.cpp` | `default_hotkeys` | 단축키 3개 등록 (기본 키 없음) |
| `citra_qt/configuration/configure_audio.cpp/.h` | 생성자, `ApplyConfiguration` | 설정 > 오디오, 기존 볼륨 줄 **바로 아래** 같은 구성의 "효과음 볼륨" 줄 (전체 설정에서만). 적용 때 `QSettings` 의 `sfx_volume` 에 저장하고 `SetSfxVolume` |
| `core/file_sys/ncch_container.cpp` | `NCCHContainer::LoadHeader`, 암호화 검사 직전 | (선택, 기능과 무관) NoCrypto 플래그가 꺼져 있지만 실제로는 평문인 덤프를 열 수 있게 함. 새 버전이 이미 처리하면 빼도 된다 |

## 3. 진행 순서

1. 공식 저장소(`https://github.com/azahar-emu/azahar`)에서 최신 릴리스 태그를 받는다. 서브모듈까지 받는다.
2. `module/` 를 소스 루트에 덮어 복사한다.
3. 소스 루트에서 `git apply -3 <kit>/azahar/hooks.patch`. 충돌 파일은 `hooks/` 의 해당 패치와 위 표의 '역할'을 보고 손으로 옮긴다.
4. 1절의 의존 API 가 새 버전에도 같은지 확인하고, 바뀌었으면 `module/` 쪽을 맞춘다.
5. 빌드. 원래는 Windows MSYS2 CLANG64 + Ninja (`-DENABLE_TESTS=OFF -DENABLE_ROOM=OFF -DENABLE_WEB_SERVICE=OFF -DENABLE_DISCORD_RPC=OFF`), 배포용은 `bundle` 타깃.
6. 아래 확인 방법.

## 4. 확인 방법 (게임 필요, 사람이 듣고 판단)

로그(`user/log/azahar_log.txt`)에 `ff-bgm1x:` 로 시작하는 줄이 나온다. 먼저 게임 시작 직후 BGM 파일 색인 수가 0 이 아닌지 본다.

그다음 배속을 켜고:
- 필드 BGM 이 1배속으로 들리고 음정이 원래와 같다.
- 팡파레(아이템 획득 등)는 끝까지 1배속, 그 뒤 곡은 게임이 진행된 위치에서 이어진다.
- 연출 곡·동영상 동안은 배속이 멈춘다 ("연출 중에도 배속" 꺼짐 기본).
- 메뉴음·대화 넘김음·발소리·기술음·능력 변화음·울음소리가 1배속으로, 원래 음높이로, 배속 없을 때와 같은 소리로 난다.
- 게임이 아주 작게 트는 주변음이 커지지 않고, 게임이 멈춘 반복음이 계속 울리지 않는다.
- 배속을 끄면 효과음은 게임 원본 그대로다.
- 설정 > 오디오의 효과음 볼륨이 효과음에만 적용된다.
- "BGM 1배속 유지" 를 끄면 원본 에뮬레이터와 똑같다.

효과음 미러를 확인한 게임: 포켓몬스터 울트라썬, 오메가루비. BGM 을 확인한 게임: 포켓몬스터 오메가루비/알파사파이어, X·Y(AAC), 울트라썬, 레이튼 교수 시리즈, 동물의 숲.

## 5. 알려진 함정

- **Qt 매크로 충돌**: `audio_core` 헤더가 `citra_qt` 에서도 포함된다. 멤버나 변수 이름을 `slots`, `signals`, `emit` 으로 지으면 Qt 매크로 때문에 빌드가 깨진다.
- **HLE 오디오 전용**: 오디오 에뮬레이션을 LLE 로 바꾸면 동작하지 않는다 (연결부가 HLE 경로에만 있다).
- **스레드**: `MixInto`·`Mirror*` 는 에뮬레이션(DSP) 스레드, `Mix` 는 오디오 출력 스레드, 메뉴 설정은 UI 스레드에서 불린다. `SfxMirror` 는 자체 mutex 로 모든 상태를 지킨다.
- **배속 중 밀림**: 게임은 배속 배수만큼 빨리 소리를 만든다. 짧은 소리는 다 쌓인 뒤 1배속으로 끝까지 나지만, 게임이 계속 내는 소리는 밀리므로 `kMaxLag`(2.5초)를 넘으면 건너뛴다. 이 값을 줄이면 긴 효과음이 잘리고, 늘리면 주변음이 화면보다 오래 남는다.
- **반향(aux) 믹스**: 게임이 믹스 1·2 로 보내는 소리는 실제로는 게임의 반향 효과를 거쳐 돌아오지만, 미러는 그 볼륨만큼 원래 소리로 더한다.
- 오래 켜 두면(18시간) 메모리 사용이 크게 늘어난 사례가 있다 (v3 이전, 원인은 밝히지 못했다).
- 줄바꿈: 이 자료는 LF. Windows 에서 `core.autocrlf` 때문에 패치가 통째로 다르게 보이면 줄바꿈 차이부터 의심한다.
