// GB 1배속 두 방식 대조: 같은 롬·같은 입력으로 원본 코어 두 개를 화면 없이 돌려
//   gsc.wav : 엔진을 옮긴 재생기(GscPlayer) 쪽 1배속 소리
//   gen.wav : 게임 드라이버를 자체 CPU 로 돌리는 방식(GbDriverGeneric) 쪽 1배속 소리
//   ref.wav : 아무것도 건드리지 않은 원본 소리
// 배속 n 이면 게임을 실제 시간 1프레임마다 n 프레임 진행한다 (1배속 소리는 실제 시간만큼).
// 사용법: gb_generic_test <rom> <출력폴더> <초> [배속] [입력: 프레임:키:길이,...] [gen만=1]
//   키: A=1 B=2 Select=4 Start=8 Right=16 Left=32 Up=64 Down=128 (게임 프레임 기준)
#include <fcntl.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <mgba/core/blip_buf.h>
#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/gb/core.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/sm83/sm83.h>
#include <mgba-util/vfs.h>

#include "GbDriverBgm.h"
#include "GbNativeApuMgba.h"
#include <mgba/internal/gb/audio.h>
#include <memory>

using namespace QGBA;

static const int RATE = 48000;

struct Wav {
	std::vector<int16_t> s;
	void save(const std::string& path) {
		FILE* f = std::fopen(path.c_str(), "wb");
		uint32_t bytes = (uint32_t) s.size() * 2;
		auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
		auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
		std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
		w32(16); w16(1); w16(2); w32(RATE); w32(RATE * 4); w16(4); w16(16);
		std::fwrite("data", 1, 4, f); w32(bytes);
		std::fwrite(s.data(), 2, s.size(), f);
		std::fclose(f);
	}
};

static mCore* makeCore(std::vector<uint8_t>& rom, std::vector<uint32_t>& video, const char* sav) {
	mCore* c = GBCoreCreate();
	c->init(c);
	mCoreInitConfig(c, nullptr);
	video.assign(256 * 256, 0);
	c->setVideoBuffer(c, reinterpret_cast<color_t*>(video.data()), 256);
	c->setAudioBufferSize(c, 8192);
	c->loadROM(c, VFileFromConstMemory(rom.data(), rom.size()));
	if (sav) {
		struct VFile* vf = VFileOpen(sav, O_RDONLY);
		if (vf) {
			std::vector<uint8_t> data((size_t) vf->size(vf));
			vf->read(vf, data.data(), data.size());
			vf->close(vf);
			struct VFile* mem = VFileMemChunk(data.data(), data.size());
			c->loadSave(c, mem);
		}
	}
	// 확인용 (GB_BIOS=<부트 롬 경로>): 부트 롬을 켠다 (부트 롬을 켜고 만든 무비 입력과 맞출 때)
	if (const char* bios = std::getenv("GB_BIOS")) {
		struct VFile* bf = VFileOpen(bios, O_RDONLY);
		if (bf) {
			c->opts.useBios = true;
			c->loadBIOS(c, bf, 0);
		}
	}
	// 확인용 (GB_MODEL=DMG/CGB/AGB 등): 기종을 정한다 (무비 입력이 만든 기종과 맞출 때)
	if (const char* model = std::getenv("GB_MODEL")) {
		mCoreConfigSetValue(&c->config, "gb.model", model);
		mCoreConfigSetValue(&c->config, "cgb.model", model);
		c->loadConfig(c, &c->config);
	}
	c->reset(c);
	blip_set_rates(c->getAudioChannel(c, 0), c->frequency(c), RATE);
	blip_set_rates(c->getAudioChannel(c, 1), c->frequency(c), RATE);
	return c;
}

static void drain(mCore* c, Wav* w) {
	blip_t* l = c->getAudioChannel(c, 0);
	blip_t* r = c->getAudioChannel(c, 1);
	int n = blip_samples_avail(l);
	std::vector<int16_t> buf((size_t) n * 2);
	blip_read_samples(l, buf.data(), n, true);
	blip_read_samples(r, buf.data() + 1, n, true);
	if (w) w->s.insert(w->s.end(), buf.begin(), buf.end());
}

static double rms(const std::vector<int16_t>& s, size_t from, size_t to) {
	double a = 0;
	to = std::min(to, s.size());
	if (to <= from) return 0;
	for (size_t i = from; i < to; ++i) a += (double) s[i] * s[i];
	return std::sqrt(a / (to - from));
}

// 같은 구간의 정규화 상관 (어긋남 lag 표본을 ±maxLag 에서 찾는다, 모노로)
static double bestCorr(const std::vector<int16_t>& a, const std::vector<int16_t>& b, size_t from, size_t to, int maxLag, int& lagOut) {
	double best = -2;
	lagOut = 0;
	for (int lag = -maxLag; lag <= maxLag; lag += 8) {
		double sab = 0, saa = 0, sbb = 0;
		for (size_t i = from; i < to; i += 2) {
			long j = (long) i + lag * 2;
			if (j < 0 || (size_t) j + 1 >= b.size() || i + 1 >= a.size()) continue;
			double x = a[i] + a[i + 1], y = b[j] + b[j + 1];
			sab += x * y; saa += x * x; sbb += y * y;
		}
		if (saa > 0 && sbb > 0) {
			double c = sab / std::sqrt(saa * sbb);
			if (c > best) { best = c; lagOut = lag; }
		}
	}
	return best;
}

struct Press { int frame, keys, len; };

// 원본(기준) 코어의 사운드 레지스터 쓰기 기록 (GB_REGLOG)
static FILE* g_refRegLog = nullptr;
static int g_gameFrame = 0;
static FILE* g_refRegTime = nullptr;  // 원본 쓰기의 시각 기록 (시간 단위 값, 레지스터, 값)
static struct GB* g_refGb = nullptr;
// 확인용 (GB_APUCMP): 원본 쓰기를 같은 시각에 받는 재현 장치. 프레임마다 원본 장치와 상태를 비교한다
static std::unique_ptr<QGBA::GbNativeApu> g_cmpApu;
static double g_cmpNow = 0;
static void cmpAdvance() {
	const double t = mTimingGlobalTime(&g_refGb->timing) / 2.0;
	if (t > g_cmpNow) {
		float l, r;
		g_cmpApu->run(t - g_cmpNow, l, r);
		g_cmpNow = t;
	}
}
static void refStoreHook(struct SM83Core*, uint16_t address, uint8_t value) {
	if (g_cmpApu && address >= 0xFF10 && address < 0xFF40) {
		cmpAdvance();
		g_cmpApu->write(address & 0xFF, value);
	}
	if (g_refRegTime && g_refGb && address >= 0xFF10 && address < 0xFF40)
		std::fprintf(g_refRegTime, "%llu %02X %02X\n", (unsigned long long) mTimingGlobalTime(&g_refGb->timing), address & 0xFF, value);
	// 확인용 (GB_REGPC): 원본 레지스터 기록에 쓴 코드의 PC 와 롬 뱅크를 붙인다
	static const bool regPc = std::getenv("GB_REGPC") != nullptr;
	if (g_refRegLog && address >= 0xFF10 && address < 0xFF40) {
		if (regPc && g_refGb) std::fprintf(g_refRegLog, "%d %02X %02X %02X:%04X\n", g_gameFrame, address & 0xFF, value, g_refGb->memory.currentBank, g_refGb->cpu->pc);
		else std::fprintf(g_refRegLog, "%d %02X %02X\n", g_gameFrame, address & 0xFF, value);
	}
	// 확인용 (GB_WATCH=주소,주소,...): 원본이 그 RAM 에 쓴 값과 PC
	static std::vector<unsigned> watch;
	static bool watchInit = false;
	if (!watchInit) {
		watchInit = true;
		if (const char* w = std::getenv("GB_WATCH")) {
			for (const char* p = w; *p;) {
				watch.push_back(std::strtoul(p, const_cast<char**>(&p), 16));
				if (*p == ',') ++p;
			}
		}
	}
	for (unsigned a : watch) {
		if (a == address && g_refGb) std::printf("  [watch] f%d %04X=%02X pc %04X\n", g_gameFrame, address, value, g_refGb->cpu->pc);
	}
}

static FILE* g_regLog[2] = {nullptr, nullptr};
static int g_tag = -1;
static int g_outFrame = 0;
static void regHook(const GbApuSynth*, uint8_t reg, uint8_t v) {
	if (g_tag >= 0 && g_regLog[g_tag]) std::fprintf(g_regLog[g_tag], "%d %02X %02X\n", g_outFrame, reg, v);
}

int main(int argc, char** argv) {
	if (argc < 4) {
		std::printf("usage: gb_generic_test <rom> <outdir> <seconds> [speed] [inputs] [genOnly]\n");
		return 1;
	}
	std::string out = argv[2];
	const int seconds = std::atoi(argv[3]);
	const int speed = argc > 4 ? std::max(1, std::atoi(argv[4])) : 1;
	std::vector<Press> presses;
	if (argc > 5) {
		std::string s = argv[5];
		size_t p = 0;
		while (p < s.size()) {
			size_t q = s.find(',', p);
			if (q == std::string::npos) q = s.size();
			Press pr{};
			if (std::sscanf(s.substr(p, q - p).c_str(), "%d:%d:%d", &pr.frame, &pr.keys, &pr.len) == 3) presses.push_back(pr);
			p = q + 1;
		}
	}
	const bool genOnly = argc > 6 && std::atoi(argv[6]);
	const char* sav = std::getenv("GB_SAV");
	FILE* f = std::fopen(argv[1], "rb");
	if (!f) { std::printf("cannot open rom\n"); return 1; }
	std::fseek(f, 0, SEEK_END);
	std::vector<uint8_t> rom((size_t) std::ftell(f));
	std::fseek(f, 0, SEEK_SET);
	std::fread(rom.data(), 1, rom.size(), f);
	std::fclose(f);

	GbDriverProfile p;
	const bool haveGsc = !genOnly && detectGbDriver(rom, p);
	std::printf("GscPlayer engine: %s\n", haveGsc ? "yes" : "no");

	std::vector<uint32_t> v1, v2, v3;
	mCore* gscCore = haveGsc ? makeCore(rom, v1, sav) : nullptr;
	mCore* genCore = makeCore(rom, v2, sav);
	mCore* refCore = makeCore(rom, v3, sav);
	std::unique_ptr<GbDriverBgm> gsc(haveGsc ? new GbDriverBgm(rom, p) : nullptr);
	GbDriverBgm gen(rom);
	gen.tracer()->log = [&](const std::string& m) { std::printf("  [gen] f%d %s\n", g_gameFrame, m.c_str()); };
	gen.link()->log = [&](const std::string& m) { std::printf("  [shadow] f%d %s\n", g_gameFrame, m.c_str()); };
	gen.tracer()->traceWrites = std::getenv("GB_TRACE_WRITES") != nullptr;
	if (std::getenv("GB_TRACE_FROM")) { gen.tracer()->traceFrom = std::atoi(std::getenv("GB_TRACE_FROM")); gen.tracer()->traceTo = std::atoi(std::getenv("GB_TRACE_TO")); }
	Wav gscW, genW, refW, mainW;  // mainW: 범용 방식 쪽 원본 코어 출력 (1배속 채널을 끈 원본 소리)
	if (const char* m = std::getenv("GB_CH1X")) gen.setChannels1x(std::strtol(m, nullptr, 16));
	if (std::getenv("GB_REGLOG")) {
		g_regLog[0] = std::fopen((out + "/gsc_regs.txt").c_str(), "w");
		g_regLog[1] = std::fopen((out + "/gen_regs.txt").c_str(), "w");
		GbApuSynth::s_debugWrite = &regHook;
		g_refRegLog = std::fopen((out + "/ref_regs.txt").c_str(), "w");
		static_cast<struct GB*>(refCore->board)->cpu->ffbgmStore = &refStoreHook;
		g_refGb = static_cast<struct GB*>(refCore->board);
		if (const char* m = std::getenv("GB_REF_MUTE")) {
			const int mask = std::strtol(m, nullptr, 16);
			for (int i = 0; i < 4; ++i) g_refGb->audio.forceDisableCh[i] = (mask >> i) & 1;
		}
		g_refRegTime = std::fopen((out + "/ref_regs_t.txt").c_str(), "w");
		if (std::getenv("GB_APUCMP")) {
			g_cmpApu = QGBA::makeMgbaGbApu(g_refGb->audio.style);
			g_cmpApu->write(0x26, 0x80);
		}
	}
	// 원본 소리: 원본 코어가 blip 버퍼에 넣기 전의 표본 (131072Hz) 을 받아 48000Hz 로 평균 내린다.
	// (시험 도구에서 blip 버퍼로 꺼낸 소리는 튐이 섞여 기준으로 쓰지 않는다. ref_blip.wav 로 따로 남긴다)
	static std::vector<int16_t> rawRef;
	static mAVStream rawStream{};
	rawStream.postAudioFrame = [](mAVStream*, int16_t l, int16_t r) {
		rawRef.push_back(l);
		rawRef.push_back(r);
	};
	if (speed == 1) refCore->setAVStream(refCore, &rawStream);

	const int outFrames = (int) (seconds * 59.7275);
	const double perFrameExact = RATE * 70224.0 / 4194304.0;  // 한 프레임의 출력 표본 수 (정수가 아니다)
	double perFrameCarry = 0;
	uint64_t genCycles = 0;  // 이번 출력 프레임에 생성 쪽 코어가 실제로 돈 시간 (mGBA 단위: 사이클의 절반)
	int gameFrame = 0;
	// 확인용 (GB_LOADSTATE=<파일>): 저장해 둔 상태에서 시작한다 (입력 프레임은 그 상태부터 0). 입력을 짜며 앞부분을 다시 돌리지 않으려고
	if (const char* ls = std::getenv("GB_LOADSTATE")) {
		FILE* sf = std::fopen(ls, "rb");
		if (sf) {
			std::vector<uint8_t> st(refCore->stateSize(refCore));
			const size_t got = std::fread(st.data(), 1, st.size(), sf);
			std::fclose(sf);
			if (got == st.size()) {
				refCore->loadState(refCore, st.data());
				genCore->loadState(genCore, st.data());
				if (gscCore) gscCore->loadState(gscCore, st.data());
				gen.tracer()->resync();
			}
		}
	}
	// 확인용 (GB_SAVESTATE=<프레임>:<파일>): 그 프레임이 끝난 뒤 원본 코어 상태를 저장한다
	int saveAt = -1;
	std::string savePath;
	if (const char* ss = std::getenv("GB_SAVESTATE")) {
		const char* colon = std::strchr(ss, ':');
		if (colon) {
			saveAt = std::atoi(ss);
			savePath = colon + 1;
		}
	}
	for (int fr = 0; fr < outFrames; ++fr) {
		for (int k = 0; k < speed; ++k) {
			int keys = 0;
			for (const Press& pr : presses) {
				if (gameFrame >= pr.frame && gameFrame < pr.frame + pr.len) keys |= pr.keys;
			}
			if (gscCore) gscCore->setKeys(gscCore, keys);
			genCore->setKeys(genCore, keys);
			refCore->setKeys(refCore, keys);
			if (gscCore) {
				gscCore->runFrame(gscCore);
				gsc->onFrame(gscCore, true, true);
				drain(gscCore, nullptr);
			}
			{
				const uint64_t g0 = mTimingGlobalTime(&static_cast<struct GB*>(genCore->board)->timing);
				genCore->runFrame(genCore);
				genCycles += mTimingGlobalTime(&static_cast<struct GB*>(genCore->board)->timing) - g0;
			}
			gen.onFrame(genCore, true, true);
			if (std::getenv("GB_SELF_SYNTH")) GbDriverShadow::s_nativeApu = nullptr;  // 비교용: 자체 합성기
			drain(genCore, speed == 1 ? &mainW : nullptr);
			{
				const size_t before = rawRef.size();
				const uint64_t t0 = mTimingGlobalTime(&static_cast<struct GB*>(refCore->board)->timing);
				refCore->runFrame(refCore);
				const uint64_t t1 = mTimingGlobalTime(&static_cast<struct GB*>(refCore->board)->timing);
				// 확인용: 원본 프레임 길이가 보통(70224 사이클)과 다른 프레임
				if (std::getenv("GB_FRAMELEN") && (t1 - t0) / 2 != 70224)
					std::printf("framelen f%d %llu cycles raw %zu\n", gameFrame, (unsigned long long) ((t1 - t0) / 2), (rawRef.size() - before) / 2);
			}
			drain(refCore, speed == 1 ? &refW : nullptr);
			if (g_cmpApu) {
				cmpAdvance();
				const struct GBAudio* x = &g_refGb->audio;
				const struct GBAudio* y = static_cast<const struct GBAudio*>(QGBA::mgbaGbApuState(g_cmpApu.get()));
				static int shown = 0;
				for (int c = 0; c < 2 && shown < 60 && gameFrame >= 800; ++c) {
					const struct GBAudioSquareChannel* a = c ? &x->ch2 : &x->ch1;
					const struct GBAudioSquareChannel* b = c ? &y->ch2 : &y->ch1;
					const bool pa = c ? x->playingCh2 : x->playingCh1, pb = c ? y->playingCh2 : y->playingCh1;
					if (pa != pb || a->envelope.currentVolume != b->envelope.currentVolume || a->control.frequency != b->control.frequency ||
					    a->control.length != b->control.length || a->envelope.dead != b->envelope.dead || a->envelope.nextStep != b->envelope.nextStep) {
						++shown;
						std::printf("cmp f%d ch%d play %d/%d vol %d/%d freq %d/%d len %d/%d dead %d/%d step %d/%d frame %d/%d\n", gameFrame, c + 1, pa, pb,
						            a->envelope.currentVolume, b->envelope.currentVolume, a->control.frequency, b->control.frequency, a->control.length,
						            b->control.length, a->envelope.dead, b->envelope.dead, a->envelope.nextStep, b->envelope.nextStep, x->frame, y->frame);
					}
				}
			}
			++gameFrame;
			g_gameFrame = gameFrame;
			if (gameFrame == saveAt) {
				std::vector<uint8_t> st(refCore->stateSize(refCore));
				refCore->saveState(refCore, st.data());
				FILE* sf = std::fopen(savePath.c_str(), "wb");
				if (sf) {
					std::fwrite(st.data(), 1, st.size(), sf);
					std::fclose(sf);
				}
			}
			// 확인용 (GB_SHOT=<프레임 간격>): 그 간격마다 화면을 shot_<프레임>.ppm 으로
			static const int shotEvery = std::getenv("GB_SHOT") ? std::atoi(std::getenv("GB_SHOT")) : 0;
			if (shotEvery > 0 && gameFrame % shotEvery == 0) {
				FILE* img = std::fopen((out + "/shot_" + std::to_string(gameFrame) + ".ppm").c_str(), "wb");
				std::fprintf(img, "P6 160 144 255\n");
				for (int y = 0; y < 144; ++y)
					for (int x = 0; x < 160; ++x) {
						uint32_t c = v2[y * 256 + x];
						uint8_t rgb[3] = {(uint8_t) (c & 0xFF), (uint8_t) ((c >> 8) & 0xFF), (uint8_t) ((c >> 16) & 0xFF)};
						std::fwrite(rgb, 1, 3, img);
					}
				std::fclose(img);
			}
		}
		if (fr % 60 == 59) {
			GbDriverTracer* tt = gen.tracer();
			std::printf("t=%3ds writes=%u calls=%u bursts=%u updates=%u\n", (fr + 1) / 60, tt->statWrites, tt->statCalls, tt->statBursts, gen.link()->map.updates.load());
		}
		// 1배속: 앱처럼 코어가 실제로 돈 시간만큼 뽑는다 (화면을 꺼 길어진 프레임은 표본도 많다)
		perFrameCarry += speed == 1 ? RATE * (genCycles / 2.0) / 4194304.0 : perFrameExact;
		genCycles = 0;
		const int perFrame = (int) perFrameCarry;
		perFrameCarry -= perFrame;
		std::vector<int16_t> b((size_t) perFrame * 2, 0);
		g_outFrame = fr;
		g_tag = 0;
		if (gsc) gsc->mixRealtime(b.data(), perFrame, RATE, 0x100);
		gscW.s.insert(gscW.s.end(), b.begin(), b.end());
		std::fill(b.begin(), b.end(), 0);
		g_tag = 1;
		gen.mixRealtime(b.data(), perFrame, RATE, 0x100);
		g_tag = -1;
		genW.s.insert(genW.s.end(), b.begin(), b.end());
	}
	if (gsc) gscW.save(out + "/gsc.wav");
	genW.save(out + "/gen.wav");
	if (speed == 1) {
		refW.save(out + "/ref_blip.wav");
		Wav rawW;
		const double step = 131072.0 / RATE;
		for (size_t j = 0;; ++j) {
			const double a = j * step, e = (j + 1) * step;
			if ((size_t) e * 2 + 2 > rawRef.size()) break;
			double sl = 0, sr = 0;
			for (size_t k = (size_t) a; k < (size_t) std::ceil(e); ++k) {
				const double w = std::min<double>(k + 1, e) - std::max<double>(k, a);
				sl += rawRef[k * 2] * w;
				sr += rawRef[k * 2 + 1] * w;
			}
			rawW.s.push_back((int16_t) std::lround(sl / step));
			rawW.s.push_back((int16_t) std::lround(sr / step));
		}
		rawW.save(out + "/ref.wav");
	}
	if (speed == 1) mainW.save(out + "/main.wav");
	GbDriverTracer* t = gen.tracer();
	std::printf("update known=%d entry=%02X:%04X calls/frame=%.3f learned=%zu\n", t->updateKnown(),
	            t->updateRoutine().romBank, t->updateRoutine().entry, 0.0, t->learnedCount());
	for (uint32_t k : t->learned()) std::printf("  learned %02X:%04X\n", k >> 16, k & 0xFFFF);
	std::printf("update inputs:");
	for (int i = 0; i < kGbRamSize; ++i) {
		if (gen.link()->map.updateInput(i)) std::printf(" %04X", i < 0x8000 ? (i < 0x1000 ? 0xC000 + i : 0xD000 + (i & 0xFFF)) : 0xFF80 + (i - 0x8000));
	}
	std::printf("\n");
	// 화면 저장 (마지막 프레임, 확인용)
	{
		FILE* img = std::fopen((out + "/screen.ppm").c_str(), "wb");
		std::fprintf(img, "P6 160 144 255\n");
		for (int y = 0; y < 144; ++y)
			for (int x = 0; x < 160; ++x) {
				uint32_t c = v2[y * 256 + x];
				uint8_t rgb[3] = {(uint8_t) (c & 0xFF), (uint8_t) ((c >> 8) & 0xFF), (uint8_t) ((c >> 16) & 0xFF)};
				std::fwrite(rgb, 1, 3, img);
			}
		std::fclose(img);
	}
	std::printf("\nsec   gsc_rms   gen_rms   corr  lag(samples)\n");
	for (int s = 0; s < seconds; ++s) {
		size_t a = (size_t) s * RATE * 2, e = a + RATE * 2;
		int lag = 0;
		double c = gsc ? bestCorr(gscW.s, genW.s, a, e, 1600, lag) : 0;
		std::printf("%3d  %8.0f  %8.0f  %6.3f  %d\n", s, rms(gscW.s, a, e), rms(genW.s, a, e), c, lag);
	}
	return 0;
}
