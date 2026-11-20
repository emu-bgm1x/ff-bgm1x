// ff-bgm1x: 소리 분류 문자열 명령 (NDS). 설명은 NdsCatalogCommand.h
#include "NdsCatalogCommand.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

#include "SoundCatalog.h"
#include "SoundPreview.h"

namespace
{

std::string folderOf(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

} // namespace

std::string ndsCatalogCommand(SoundCatalog* cat, SoundPreview* pv, bool usable, const std::string& cmd)
{
    const size_t sp = cmd.find_first_of(" \n");
    const std::string op = cmd.substr(0, sp);
    std::istringstream in(sp == std::string::npos ? std::string() : cmd.substr(sp + 1));
    if (op == "dir") return "ok";
    if (op == "state")
    {
        std::ostringstream o;
        o << "game\t" << (cat ? cat->gameCode() : std::string()) << "\t" << (cat ? cat->gameTitle() : std::string()) << "\n";
        o << "collect\t" << (cat && cat->collecting() ? 1 : 0) << "\n";
        o << "version\t" << (cat ? cat->seenVersion() : 0) << "\n";
        o << "path\t" << (cat ? cat->storagePath() : std::string()) << "\n";
        o << "usable\t" << (cat && usable ? 1 : 0) << "\n";
        o << "labels\t0\n";
        return o.str();
    }
    if (!cat) return "error\tNo game is running.";
    if (op == "list")
    {
        std::ostringstream o;
        for (const SoundCatalog::Seen& s : cat->seenList())
        {
            o << s.key << "\t" << s.type << "\t" << s.name << "\t" << "" << "\t" << s.autoKind << "\t" << s.userKind << "\t"
              << s.volume << "\t" << s.count << "\t" << s.order << "\n";
        }
        return o.str();
    }
    if (op == "collect")
    {
        int on = 0;
        in >> on;
        cat->setCollecting(on != 0);
        return "ok";
    }
    if (op == "kind" || op == "vol")
    {
        std::string key;
        int v = 0;
        if (!(in >> key >> v)) return "error\tMissing arguments.";
        if (op == "kind") cat->setUserKind(key, v);
        else cat->setVolume(key, v);
        return "ok";
    }
    if (op == "label") return "error\tNDS sounds cannot be renamed.";
    if (op == "reset")
    {
        std::string key;
        if (!(in >> key)) return "error\tMissing arguments.";
        cat->resetUser(key);
        return "ok";
    }
    if (op == "clear")
    {
        cat->clearSeen();
        return "ok";
    }
    if (op == "export" || op == "import")
    {
        // 파일로만 주고받는 목록이라 저장 폴더의 임시 파일을 거친다
        const std::string tmp = folderOf(cat->storagePath()) + "/.transfer.txt";
        if (op == "export")
        {
            if (!cat->exportTo(tmp)) return "error\tCould not export.";
            std::ifstream f(tmp, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            f.close();
            std::remove(tmp.c_str());
            return text;
        }
        const size_t nl = cmd.find('\n');
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return "error\tCould not import.";
            if (nl != std::string::npos) f << cmd.substr(nl + 1);
        }
        std::string err;
        const bool ok = cat->importFrom(tmp, err);
        std::remove(tmp.c_str());
        return ok ? std::string("ok") : "error\t" + err;
    }
    if (op == "play" || op == "stop" || op == "pause" || op == "pstate")
    {
        if (!pv) return op == "pstate" ? std::string("playing\t0\npaused\t0\nlength\t0\n") : std::string("error\tCannot play.");
        if (op == "play")
        {
            std::string key, err;
            in >> key;
            if (!pv->play(*cat, key, err)) return "error\t" + err;
            return "ok";
        }
        if (op == "stop")
        {
            pv->stop();
            return "ok";
        }
        if (op == "pause")
        {
            int on = 0;
            in >> on;
            pv->setPaused(on != 0);
            return "ok";
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "playing\t%d\npaused\t%d\nlength\t%.1f\n", pv->isPlaying() ? 1 : 0, pv->isPaused() ? 1 : 0,
                      pv->lengthSeconds());
        return buf;
    }
    return "error\tUnknown command: " + op;
}
