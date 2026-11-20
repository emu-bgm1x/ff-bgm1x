// ff-bgm1x: 소리 분류 문자열 명령. 설명은 catalog_command.h
#include "catalog_command.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace {

std::string pendingDir;  // 게임이 없을 때 받은 저장 폴더 (게임을 열면 적용)

std::string rest(const std::string& s, size_t from) {
    if (from >= s.size()) return {};
    size_t b = s.find_first_not_of(' ', from);
    return b == std::string::npos ? std::string() : s.substr(b);
}

}  // namespace

std::string catalogCommand(SoundCatalog* cat, CatalogPreview* pv, bool usable, const std::string& cmd) {
    const size_t sp = cmd.find_first_of(" \n");
    const std::string op = cmd.substr(0, sp);
    std::istringstream in(sp == std::string::npos ? std::string() : cmd.substr(sp + 1));
    if (op == "dir") {
        const std::string d = rest(cmd, op.size());
        pendingDir = d;
        if (cat) cat->SetStorageDir(d);
        return "ok";
    }
    if (op == "state") {
        std::ostringstream o;
        o << "game\t" << (cat ? cat->GameCode() : std::string()) << "\t" << (cat ? cat->GameTitle() : std::string()) << "\n";
        o << "collect\t" << (cat && cat->Collecting() ? 1 : 0) << "\n";
        o << "version\t" << (cat ? cat->SeenVersion() : 0) << "\n";
        o << "path\t" << (cat ? cat->StoragePath() : std::string()) << "\n";
        o << "usable\t" << (cat && usable ? 1 : 0) << "\n";
        return o.str();
    }
    if (!cat) return "error\t게임이 없습니다.";
    if (!pendingDir.empty()) {
        cat->SetStorageDir(pendingDir);
    }
    if (op == "list") {
        std::ostringstream o;
        for (const SoundCatalog::Seen& s : cat->SeenList()) {
            o << s.key << "\t" << s.type << "\t" << s.name << "\t" << s.label << "\t" << s.auto_kind << "\t" << s.user_kind
              << "\t" << s.volume << "\t" << s.count << "\t" << s.order << "\n";
        }
        return o.str();
    }
    if (op == "collect") {
        int on = 0;
        in >> on;
        cat->SetCollecting(on != 0);
        return "ok";
    }
    if (op == "kind" || op == "vol") {
        std::string key;
        int v = 0;
        if (!(in >> key >> v)) return "error\t인자가 모자랍니다.";
        if (op == "kind") cat->SetUserKind(key, v);
        else cat->SetVolume(key, v);
        return "ok";
    }
    if (op == "label") {
        std::string key;
        if (!(in >> key)) return "error\t인자가 모자랍니다.";
        const size_t at = cmd.find(key, op.size()) + key.size();
        cat->SetLabel(key, rest(cmd, at));
        return "ok";
    }
    if (op == "reset") {
        std::string key;
        if (!(in >> key)) return "error\t인자가 모자랍니다.";
        cat->ResetUser(key);
        return "ok";
    }
    if (op == "clear") {
        cat->ClearSeen();
        return "ok";
    }
    if (op == "export") return cat->ExportText();
    if (op == "import") {
        const size_t nl = cmd.find('\n');
        std::string err;
        if (!cat->ImportText(nl == std::string::npos ? std::string() : cmd.substr(nl + 1), err)) return "error\t" + err;
        return "ok";
    }
    if (op == "play" || op == "stop" || op == "pause" || op == "pstate") {
        if (!pv) return op == "pstate" ? std::string("playing\t0\npaused\t0\nlength\t0\n") : std::string("error\t재생할 수 없습니다.");
        if (op == "play") {
            std::string key, err;
            in >> key;
            if (!pv->play(key, err)) return "error\t" + err;
            return "ok";
        }
        if (op == "stop") {
            pv->stop();
            return "ok";
        }
        if (op == "pause") {
            int on = 0;
            in >> on;
            pv->setPaused(on != 0);
            return "ok";
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "playing\t%d\npaused\t%d\nlength\t%.1f\n", pv->playing() ? 1 : 0, pv->paused() ? 1 : 0,
                      pv->length());
        return buf;
    }
    return "error\t알 수 없는 명령입니다: " + op;
}
