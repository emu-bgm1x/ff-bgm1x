// ff-bgm1x: 소리 분류 문자열 명령 (1x Lab 앱 화면용, 3DS).
// 명령과 답의 형식은 GBA 판(common/catalog_command.h)과 같다. 다른 점:
//  - 이름 바꾸기(label)는 없다 (3DS 는 롬의 파일 이름이 소리 이름이다)
//  - 저장 폴더는 앱의 설정 폴더로 정해져 있어 dir 명령은 무시한다
// 재생 소리는 앱이 previewRender 로 받아 따로 낸다 (화면이 열려 있는 동안 게임은 멈춰 있다).
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include <jni.h>

#include "audio_core/hle/bgm_replacer.h"
#include "audio_core/hle/sound_catalog.h"
#include "core/core.h"

using AudioCore::HLE::BgmReplacer;
using AudioCore::HLE::SoundCatalog;

namespace {

std::string FolderOf(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

std::string CatalogCommand(const std::string& cmd) {
    BgmReplacer& bgm = BgmReplacer::Instance();
    SoundCatalog& cat = bgm.Catalog();
    const bool usable = Core::System::GetInstance().IsPoweredOn();
    const std::size_t sp = cmd.find_first_of(" \n");
    const std::string op = cmd.substr(0, sp);
    std::istringstream in(sp == std::string::npos ? std::string() : cmd.substr(sp + 1));
    if (op == "dir") return "ok";
    if (op == "state") {
        std::ostringstream o;
        o << "game\t" << cat.GameCode() << "\t" << cat.GameTitle() << "\n";
        o << "collect\t" << (cat.Collecting() ? 1 : 0) << "\n";
        o << "version\t" << cat.SeenVersion() << "\n";
        o << "path\t" << cat.StoragePath() << "\n";
        o << "usable\t" << (usable ? 1 : 0) << "\n";
        o << "labels\t0\n";
        return o.str();
    }
    if (!usable) return "error\tNo game is running.";
    if (op == "list") {
        std::ostringstream o;
        for (const SoundCatalog::Seen& s : cat.SeenList()) {
            o << s.key << "\t" << s.type << "\t" << s.name << "\t" << "" << "\t" << s.auto_kind << "\t" << s.user_kind
              << "\t" << s.volume << "\t" << s.count << "\t" << s.order << "\n";
        }
        return o.str();
    }
    if (op == "collect") {
        int on = 0;
        in >> on;
        cat.SetCollecting(on != 0);
        return "ok";
    }
    if (op == "kind" || op == "vol") {
        std::string key;
        int v = 0;
        if (!(in >> key >> v)) return "error\tMissing arguments.";
        if (op == "kind") {
            cat.SetUserKind(key, v);
        } else {
            cat.SetVolume(key, v);
        }
        return "ok";
    }
    if (op == "label") return "error\t3DS sounds cannot be renamed.";
    if (op == "reset") {
        std::string key;
        if (!(in >> key)) return "error\tMissing arguments.";
        cat.ResetUser(key);
        return "ok";
    }
    if (op == "clear") {
        cat.ClearSeen();
        return "ok";
    }
    if (op == "export" || op == "import") {
        // 파일로만 주고받는 목록이라 저장 폴더의 임시 파일을 거친다
        const std::string tmp = FolderOf(cat.StoragePath()) + "/.transfer.txt";
        if (op == "export") {
            if (!cat.ExportTo(tmp)) return "error\tCould not export.";
            std::ifstream f(tmp, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            f.close();
            std::remove(tmp.c_str());
            return text;
        }
        const std::size_t nl = cmd.find('\n');
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return "error\tCould not import.";
            if (nl != std::string::npos) f << cmd.substr(nl + 1);
        }
        std::string err;
        const bool ok = cat.ImportFrom(tmp, err);
        std::remove(tmp.c_str());
        return ok ? std::string("ok") : "error\t" + err;
    }
    if (op == "play") {
        std::string key, err;
        in >> key;
        bgm.SetPreviewExternal(true);
        if (!bgm.PreviewPlay(key, err)) return "error\t" + err;
        return "ok";
    }
    if (op == "stop") {
        bgm.PreviewStop();
        return "ok";
    }
    if (op == "pause") {
        int on = 0;
        in >> on;
        bgm.PreviewSetPaused(on != 0);
        return "ok";
    }
    if (op == "pstate") {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "playing\t%d\npaused\t%d\nlength\t%.1f\n", bgm.PreviewPlaying() ? 1 : 0,
                      bgm.PreviewPaused() ? 1 : 0, bgm.PreviewLength());
        return buf;
    }
    return "error\tUnknown command: " + op;
}

} // namespace

extern "C" {

// NewStringUTF 는 변형 UTF-8 이라 바이트로 넘겨 자바에서 UTF-8 로 푼다
JNIEXPORT jstring JNICALL Java_org_citra_citra_1emu_NativeLibrary_catalogCommand(JNIEnv* env,
                                                                                 [[maybe_unused]] jobject obj,
                                                                                 jstring j_cmd) {
    const char* c = env->GetStringUTFChars(j_cmd, nullptr);
    std::string command = c ? c : "";
    if (c) env->ReleaseStringUTFChars(j_cmd, c);
    const std::string result = CatalogCommand(command);
    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(result.size()));
    env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(result.size()),
                            reinterpret_cast<const jbyte*>(result.data()));
    jclass string_class = env->FindClass("java/lang/String");
    jmethodID ctor = env->GetMethodID(string_class, "<init>", "([BLjava/lang/String;)V");
    jstring charset = env->NewStringUTF("UTF-8");
    auto out = static_cast<jstring>(env->NewObject(string_class, ctor, bytes, charset));
    env->DeleteLocalRef(bytes);
    env->DeleteLocalRef(charset);
    env->DeleteLocalRef(string_class);
    return out;
}

// 재생 소리: buffer(스테레오 int16, frames*2 칸, 0 으로 채워 둔 것)에 더한다
JNIEXPORT jint JNICALL Java_org_citra_citra_1emu_NativeLibrary_previewRender(JNIEnv* env,
                                                                             [[maybe_unused]] jobject obj,
                                                                             jshortArray buffer, jint frames,
                                                                             jint sample_rate) {
    BgmReplacer& bgm = BgmReplacer::Instance();
    bgm.SetPreviewExternal(true);
    if (!bgm.PreviewPlaying()) return 0;
    jshort* data = env->GetShortArrayElements(buffer, nullptr);
    bgm.PreviewRender(reinterpret_cast<s16*>(data), static_cast<std::size_t>(frames),
                      static_cast<u32>(sample_rate));
    env->ReleaseShortArrayElements(buffer, data, 0);
    return frames;
}

} // extern "C"
