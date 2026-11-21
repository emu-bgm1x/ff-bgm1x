// 사운드 레지스터 쓰기 기록("프레임 레지스터 값" 줄)을 자체 GB 합성기(GbApuSynth)에 그대로 넣어 WAV 로 만든다.
// 프레임마다 그 프레임의 쓰기를 한꺼번에 넣고 한 프레임(70224 사이클)만큼 소리를 낸다. 직류 제거는 GscPlayer 와 같다.
// 사용법: gb_regs_render <기록.txt> <출력.wav> <프레임 수> [mgba <기종 번호>]  (mgba: mGBA 의 GB 사운드 장치로 낸다)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

#include <cstring>
#include <memory>

#include "GscPlayer.h"
#include "GbNativeApuMgba.h"

using namespace QGBA;

// 시각 기록("시간 단위 값 레지스터 값", 단위 = 4.19MHz 사이클의 절반)을 그 시각에 넣어 mGBA 장치로 낸다
static int renderTimed(const char* in, const char* outPath, int frames, int style) {
	std::vector<std::pair<double, std::pair<int, int>>> ws;
	FILE* f = std::fopen(in, "r");
	if (!f) return 1;
	unsigned long long t;
	int reg, val;
	while (std::fscanf(f, "%llu %x %x", &t, &reg, &val) == 3) ws.push_back({t / 2.0, {reg, val}});
	std::fclose(f);
	const int RATE = 48000;
	const double kClock = 4194304.0, kFrame = 70224.0;
	std::unique_ptr<GbNativeApu> apu = makeMgbaGbApu(style);
	apu->write(0x26, 0x80);
	if (const char* ph = std::getenv("GB_PHASE")) apu->setFramePhase(std::atoi(ph));
	if (const char* m = std::getenv("GB_MUTE")) apu->setMutedChannels(static_cast<uint8_t>(std::strtol(m, nullptr, 16)));
	std::vector<int16_t> out;
	float capL = 0, capR = 0;
	const float k = static_cast<float>(std::pow(65368.0 / 65536.0, 131072.0 / RATE));
	const double per = kClock / RATE;
	const double end = frames * kFrame;
	size_t wi = 0;
	double now = 0;
	for (double next = per; next <= end; next += per) {
		double accL = 0, accR = 0;
		while (true) {
			const double stop = (wi < ws.size() && ws[wi].first < next) ? ws[wi].first : next;
			if (stop > now) {
				float l, r;
				apu->run(stop - now, l, r);
				accL += l * (stop - now);
				accR += r * (stop - now);
				now = stop;
			}
			if (wi < ws.size() && ws[wi].first < next) {
				apu->write(static_cast<uint8_t>(ws[wi].second.first), static_cast<uint8_t>(ws[wi].second.second));
				++wi;
				continue;
			}
			break;
		}
		const float l = static_cast<float>(accL / per), r = static_cast<float>(accR / per);
		const float ol = l - capL;
		capL = l - ol * k;
		const float orr = r - capR;
		capR = r - orr * k;
		out.push_back(static_cast<int16_t>(std::max(-32768.f, std::min(32767.f, ol))));
		out.push_back(static_cast<int16_t>(std::max(-32768.f, std::min(32767.f, orr))));
	}
	FILE* w = std::fopen(outPath, "wb");
	uint32_t bytes = (uint32_t) out.size() * 2;
	auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, w); };
	auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, w); };
	std::fwrite("RIFF", 1, 4, w); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, w);
	w32(16); w16(1); w16(2); w32(RATE); w32(RATE * 4); w16(4); w16(16);
	std::fwrite("data", 1, 4, w); w32(bytes);
	std::fwrite(out.data(), 2, out.size(), w);
	std::fclose(w);
	return 0;
}

int main(int argc, char** argv) {
	if (argc < 4) return 1;
	if (argc > 5 && !std::strcmp(argv[4], "mgbaT")) return renderTimed(argv[1], argv[2], std::atoi(argv[3]), std::atoi(argv[5]));
	std::map<int, std::vector<std::pair<int, int>>> writes;
	FILE* f = std::fopen(argv[1], "r");
	if (!f) return 1;
	int fr, reg, val;
	while (std::fscanf(f, "%d %x %x", &fr, &reg, &val) == 3) writes[fr].push_back({reg, val});
	std::fclose(f);
	const int frames = std::atoi(argv[3]);
	const int RATE = 48000;
	const double kClock = 4194304.0, kFrame = 70224.0;
	GbApuSynth apu;
	std::unique_ptr<GbNativeApu> native;
	if (argc > 5 && !std::strcmp(argv[4], "mgba")) {
		native = makeMgbaGbApu(std::atoi(argv[5]));
		native->write(0x26, 0x80);
	}
	std::vector<int16_t> out;
	double carry = 0;
	float capL = 0, capR = 0;
	const float k = static_cast<float>(std::pow(65368.0 / 65536.0, 131072.0 / RATE));
	for (int i = 0; i < frames; ++i) {
		auto it = writes.find(i);
		if (it != writes.end())
			for (auto& w : it->second) {
				apu.write(static_cast<uint8_t>(w.first), static_cast<uint8_t>(w.second));
				if (native) native->write(static_cast<uint8_t>(w.first), static_cast<uint8_t>(w.second));
			}
		carry += kFrame * RATE / kClock;
		const int n = static_cast<int>(carry);
		carry -= n;
		const double per = kClock / RATE;
		for (int s = 0; s < n; ++s) {
			float l, r;
			apu.run(per, l, r);
			if (native) native->run(per, l, r);
			const float ol = l - capL;
			capL = l - ol * k;
			const float orr = r - capR;
			capR = r - orr * k;
			out.push_back(static_cast<int16_t>(std::max(-32768.f, std::min(32767.f, ol))));
			out.push_back(static_cast<int16_t>(std::max(-32768.f, std::min(32767.f, orr))));
		}
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
	return 0;
}
