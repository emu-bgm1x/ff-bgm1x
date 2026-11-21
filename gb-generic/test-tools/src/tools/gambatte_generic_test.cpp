// Gambatte 코어 + 엔진에 상관없는 GB 1배속(GbDriverGeneric) 시험: 롬을 화면 없이 돌려 1배속 소리를 WAV 로 저장한다.
// 갈고리 처리는 libgambatte/libretro/ff_bgm1x_gb.cpp 와 같다.
// 사용법: gambatte_generic_test <rom> <출력 wav> <초> [배속] [입력 "프레임:키:길이,..."]  (키: A=1 B=2 Select=4 Start=8 오른쪽=16 왼쪽=32 위=64 아래=128)
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <gambatte.h>
#include <inputgetter.h>

#include "gb_driver_bgm.h"

// libretro.cpp 에 있는 것 (이 시험에서는 쓰지 않음)
void cartridge_set_rumble(unsigned) {}

namespace {

struct Main final : QGBA::GbMainCore {
	gambatte::GB* gb = nullptr;
	double cycles = 70224.0;  // 이번 프레임의 실제 길이 (libretro.cpp 와 같이 runFor 표본 수 x 2)
	double frameCycles() override { return cycles; }
	uint8_t r8(uint16_t address) override { return gb->ffbgmRead(address); }
	uint8_t r8Bank1(uint16_t address) override { return gb->ffbgmReadBank1(address); }
	void setChannelsDisabled(uint8_t mask) override { gb->ffbgmSetMuteMask(mask); }
	bool readGenericState(QGBA::GbRamImage& ram, std::array<uint8_t, 0x80>& io, int& romBank, int& wramBank) override {
		gb->ffbgmReadState(ram.ram.data(), io.data(), romBank, wramBank);
		return true;
	}
};

struct Press { int frame, keys, len; };
std::vector<Press> s_presses;
int s_frame = 0;  // 게임 프레임 번호 (입력·기록용)

struct Input final : gambatte::InputGetter {
	unsigned operator()() override {
		unsigned k = 0;
		for (const Press& p : s_presses) {
			if (s_frame >= p.frame && s_frame < p.frame + p.len) k |= p.keys;
		}
		return k;
	}
};

QGBA::GbDriverTracer* s_tracer = nullptr;
gambatte::GB* s_gb = nullptr;
QGBA::GbShadowCpu::Regs s_last;

void hookFetch(gambatte::FfbgmRegs const& in, unsigned char opcode) {
	QGBA::GbShadowCpu::Regs& r = s_last;
	r.a = in.a; r.f = in.f; r.b = in.b; r.c = in.c; r.d = in.d; r.e = in.e; r.h = in.h; r.l = in.l;
	r.sp = in.sp;
	r.pc = in.pc;
	s_tracer->fetch(r, opcode, s_gb->ffbgmRomBank(), s_gb->ffbgmWramBank());
}

void hookIrq(unsigned short returnPc, unsigned short sp, unsigned short vector) {
	QGBA::GbShadowCpu::Regs r = s_last;
	r.sp = sp;
	r.pc = vector;
	s_tracer->irq(r, returnPc, s_gb->ffbgmRomBank(), s_gb->ffbgmWramBank());
}

void hookStore(unsigned short address, unsigned char value) {
	s_tracer->store(address, value, s_last.sp, s_gb->ffbgmWramBank());
}

}

int main(int argc, char** argv) {
	if (argc < 4) {
		std::printf("usage: gambatte_generic_test <rom> <out.wav> <seconds> [speed]\n");
		return 1;
	}
	const int seconds = std::atoi(argv[3]);
	const int speed = argc > 4 ? std::max(1, std::atoi(argv[4])) : 1;
	if (argc > 5) {
		std::string s = argv[5];
		size_t p = 0;
		while (p < s.size()) {
			size_t q = s.find(',', p);
			if (q == std::string::npos) q = s.size();
			Press pr{};
			if (std::sscanf(s.substr(p, q - p).c_str(), "%d:%d:%d", &pr.frame, &pr.keys, &pr.len) == 3) s_presses.push_back(pr);
			p = q + 1;
		}
	}
	FILE* f = std::fopen(argv[1], "rb");
	if (!f) return 1;
	std::fseek(f, 0, SEEK_END);
	std::vector<uint8_t> rom((size_t) std::ftell(f));
	std::fseek(f, 0, SEEK_SET);
	std::fread(rom.data(), 1, rom.size(), f);
	std::fclose(f);

	gambatte::GB gb;
	if (gb.load(rom.data(), (unsigned) rom.size()) != 0) {
		std::printf("load failed\n");
		return 2;
	}
	Input input;
	gb.setInputGetter(&input);
	// 확인용 (GB_WATCHREG=<16진 레지스터>): 그림자가 그 사운드 레지스터에 쓴 값
	static int watchReg = std::getenv("GB_WATCHREG") ? (int) std::strtol(std::getenv("GB_WATCHREG"), nullptr, 16) : -1;
	if (watchReg >= 0) {
		QGBA::GbApuSynth::s_debugWrite = [](const QGBA::GbApuSynth*, uint8_t reg, uint8_t value) {
			if (reg == watchReg) std::printf("  [swrite] f%d %02X=%02X\n", s_frame, reg, value);
		};
	}
	QGBA::GbDriverBgm drv(rom);  // 범용 방식으로 강제
	drv.tracer()->log = [](const std::string& m) { std::printf("  [gen] f%d %s\n", s_frame, m.c_str()); };
	drv.link()->log = [](const std::string& m) { std::printf("  [shadow] f%d %s\n", s_frame, m.c_str()); };
	Main core;
	core.gb = &gb;
	s_gb = &gb;
	s_tracer = drv.tracer();
	drv.tracer()->readRam = [&gb](QGBA::GbRamImage& image) {
		std::array<uint8_t, 0x80> io;
		int rb, wb;
		gb.ffbgmReadState(image.ram.data(), io.data(), rb, wb);
	};
	drv.tracer()->resync();
	// GB_REF=1: 범용 방식을 끄고 Gambatte 원본 소리만 녹음한다 (같은 입력이면 게임 진행이 같으므로 비교 기준으로 쓴다)
	const bool refMode = std::getenv("GB_REF") != nullptr;
	if (!refMode) {
		gambatte::ffbgmStore = &hookStore;
		gambatte::ffbgmIrq = &hookIrq;
		gambatte::ffbgmFetch = &hookFetch;
	}
	std::vector<int16_t> raw;  // 원본 소리 (Gambatte 표본, 좌우)

	const int RATE = 48000;
	std::vector<gambatte::video_pixel_t> video(160 * 144);
	std::vector<gambatte::uint_least32_t> audio(35112 + 2064);
	std::vector<int16_t> out;
	const int outFrames = (int) (seconds * 59.7275);
	const double perFrameExact = RATE / 59.7275;
	double carry = 0;
	for (int fr = 0; fr < outFrames; ++fr) {
		unsigned long ran = 0;  // 이번 출력 프레임에 실제로 돈 시간 (Gambatte 표본 = 2 사이클)
		for (int k = 0; k < speed; ++k) {
			// 화면 한 프레임이 나올 때까지 (화면을 끈 동안은 여러 프레임 분량이 걸린다)
			unsigned long frameRan = 0;
			for (;;) {
				unsigned samples = 35112;
				const long done = gb.runFor(video.data(), 160, audio.data(), audio.size(), samples);
				ran += samples;
				frameRan += samples;
				if (refMode) {
					for (unsigned i = 0; i < samples; ++i) {
						const uint32_t v = audio[i];
						raw.push_back(static_cast<int16_t>(v & 0xFFFF));
						raw.push_back(static_cast<int16_t>(v >> 16));
					}
				}
				if (done >= 0) break;
			}
			core.cycles = frameRan * 2.0;
			if (!refMode) drv.onFrame(core, true, true);
			++s_frame;
			// 확인용 (GB_SHOT=<간격>, GB_SHOT_DIR=<폴더>): 그 간격마다 화면을 shot_<프레임>.ppm 으로
			static const int shotEvery = std::getenv("GB_SHOT") ? std::atoi(std::getenv("GB_SHOT")) : 0;
			if (shotEvery > 0 && s_frame % shotEvery == 0 && std::getenv("GB_SHOT_DIR")) {
				FILE* img = std::fopen((std::string(std::getenv("GB_SHOT_DIR")) + "/shot_" + std::to_string(s_frame) + ".ppm").c_str(), "wb");
				if (img) {
					std::fprintf(img, "P6 160 144 255\n");
					for (int i = 0; i < 160 * 144; ++i) {
						const uint32_t c = video[i];
						const uint8_t rgb[3] = {(uint8_t) ((c >> 16) & 0xFF), (uint8_t) ((c >> 8) & 0xFF), (uint8_t) (c & 0xFF)};
						std::fwrite(rgb, 1, 3, img);
					}
					std::fclose(img);
				}
			}
			static int known = -1;
			if ((int) drv.tracer()->updateKnown() != known) {
				known = drv.tracer()->updateKnown();
				std::printf("  [state] f%d update known %d\n", s_frame, known);
			}
		}
		// 1배속: 앱처럼 실제로 돈 시간만큼 뽑는다
		carry += speed == 1 ? RATE * (ran * 2.0) / 4194304.0 : perFrameExact;
		const int perFrame = (int) carry;
		carry -= perFrame;
		std::vector<int16_t> b((size_t) perFrame * 2, 0);
		if (refMode) {
			// 이번 프레임의 원본 표본을 출력 표본 수로 평균
			const size_t n = raw.size() / 2;
			for (int j = 0; j < perFrame && n; ++j) {
				const size_t a = n * j / perFrame, e = std::max(a + 1, n * (j + 1) / perFrame);
				long l = 0, r = 0;
				for (size_t q = a; q < e; ++q) { l += raw[q * 2]; r += raw[q * 2 + 1]; }
				b[j * 2] = static_cast<int16_t>(l / (long) (e - a));
				b[j * 2 + 1] = static_cast<int16_t>(r / (long) (e - a));
			}
			raw.clear();
			// 직류 제거 (그림자 출력과 같은 필터)
			static float capL = 0, capR = 0;
			const float kk = static_cast<float>(std::pow(65368.0 / 65536.0, 131072.0 / RATE));
			for (int j = 0; j < perFrame; ++j) {
				const float l = b[j * 2] - capL;
				capL = b[j * 2] - l * kk;
				const float r = b[j * 2 + 1] - capR;
				capR = b[j * 2 + 1] - r * kk;
				b[j * 2] = static_cast<int16_t>(std::max(-32768.f, std::min(32767.f, l)));
				b[j * 2 + 1] = static_cast<int16_t>(std::max(-32768.f, std::min(32767.f, r)));
			}
		} else {
			drv.mixRealtime(b.data(), perFrame, RATE, 0x100);
		}
		out.insert(out.end(), b.begin(), b.end());
	}
	FILE* w = std::fopen(argv[2], "wb");
	uint32_t bytes = (uint32_t) out.size() * 2;
	auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, w); };
	auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, w); };
	std::fwrite("RIFF", 1, 4, w); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, w);
	w32(16); w16(1); w16(2); w32(RATE); w32(RATE * 4); w16(4); w16(16);
	std::fwrite("data", 1, 4, w); w32(bytes);
	std::fwrite(out.data(), 2, out.size(), w);
	std::fclose(w);
	std::printf("update known=%d learned=%zu\n", drv.tracer()->updateKnown(), drv.tracer()->learnedCount());
	return 0;
}
