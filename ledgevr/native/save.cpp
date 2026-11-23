// LEDGE VR — saves implementation
#include "save.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>

namespace lv {

std::string SaveData::toJson() const {
    char buf[1024];
    std::string r = "{";
    r += "\"name\":\"" + jsonEscape(settings.playerName) + "\",";
    snprintf(buf, sizeof(buf), "\"strafe\":%d,\"snap\":%d,\"snapAng\":%.1f,\"smooth\":%.1f,",
        settings.strafeEnabled ? 1 : 0, settings.snapTurn ? 1 : 0,
        settings.snapTurnAngle, settings.smoothTurnSpeed);
    r += buf;
    snprintf(buf, sizeof(buf), "\"strafeSpd\":%.2f,\"grab\":%.2f,\"voice\":%d,\"mic\":%d,",
        settings.strafeSpeed, settings.grabStrength, settings.voiceEnabled ? 1 : 0,
        settings.micOpen ? 1 : 0);
    r += buf;
    snprintf(buf, sizeof(buf), "\"body\":%d,\"hat\":%d,\"menu\":%.2f,\"theme\":%d,",
        settings.bodyColor, settings.hat, settings.menuScale, settings.uiTheme);
    r += buf;
    r += "\"region\":\"" + jsonEscape(settings.region) + "\",";
    snprintf(buf, sizeof(buf), "\"goals\":%d,", goalsCompleted);
    r += buf;
    r += "\"friends\":[";
    for (size_t i = 0; i < friends.size(); i++) {
        if (i) r += ",";
        r += "\"" + jsonEscape(friends[i]) + "\"";
    }
    r += "],\"levels\":[";
    for (size_t i = 0; i < myLevels.size(); i++) {
        if (i) r += ",";
        r += myLevels[i].toJson();
    }
    r += "]}";
    return r;
}

bool SaveData::fromJson(const std::string& s) {
    Level cur;
    bool inLevel = false;
    jsonParse(s, [&](const std::string& path, const std::string& key, const std::string& val) {
        if (path == "") {
            if (key == "name") settings.playerName = val;
            else if (key == "strafe") settings.strafeEnabled = atoi(val.c_str()) != 0;
            else if (key == "snap") settings.snapTurn = atoi(val.c_str()) != 0;
            else if (key == "snapAng") settings.snapTurnAngle = (float)atof(val.c_str());
            else if (key == "smooth") settings.smoothTurnSpeed = (float)atof(val.c_str());
            else if (key == "strafeSpd") settings.strafeSpeed = (float)atof(val.c_str());
            else if (key == "grab") settings.grabStrength = (float)atof(val.c_str());
            else if (key == "voice") settings.voiceEnabled = atoi(val.c_str()) != 0;
            else if (key == "mic") settings.micOpen = atoi(val.c_str()) != 0;
            else if (key == "body") settings.bodyColor = atoi(val.c_str());
            else if (key == "hat") settings.hat = atoi(val.c_str());
            else if (key == "menu") settings.menuScale = (float)atof(val.c_str());
            else if (key == "theme") settings.uiTheme = atoi(val.c_str());
            else if (key == "region") settings.region = val;
            else if (key == "goals") goalsCompleted = atoi(val.c_str());
            else if (key.rfind("friends/", 0) == 0) friends.push_back(val);
        } else if (path.rfind("levels/", 0) == 0) {
            size_t s1 = 7;
            size_t s2 = path.find('/', s1);
            std::string rest = s2 == std::string::npos ? "" : path.substr(s2 + 1);
            int idx = atoi(path.substr(s1, s2 - s1).c_str());
            while ((int)myLevels.size() <= idx) myLevels.push_back(Level());
            Level& L = myLevels[idx];
            if (rest == "name") L.name = val;
            else if (rest == "author") L.author = val;
            else if (rest == "v") L.version = atoi(val.c_str());
            else if (rest.rfind("blocks/", 0) == 0) {
                size_t b1 = 7, b2 = rest.find('/', b1);
                int bi = atoi(rest.substr(b1, b2 - b1).c_str());
                std::string bk = rest.substr(b2 + 1);
                while ((int)L.blocks.size() <= bi) L.blocks.push_back(Block());
                Block& b = L.blocks[bi];
                float v = (float)atof(val.c_str());
                if (bk == "t") b.type = (uint8_t)atoi(val.c_str());
                else if (bk == "px") b.pos.x = v;
                else if (bk == "py") b.pos.y = v;
                else if (bk == "pz") b.pos.z = v;
                else if (bk == "sx") b.size.x = v;
                else if (bk == "sy") b.size.y = v;
                else if (bk == "sz") b.size.z = v;
                else if (bk == "r") b.rotY = v;
                else if (bk == "c/0") b.color.x = v;
                else if (bk == "c/1") b.color.y = v;
                else if (bk == "c/2") b.color.z = v;
            }
        }
        (void)cur; (void)inLevel;
    });
    for (auto& L : myLevels)
        for (auto& b : L.blocks) {
            if (b.size.x <= 0) b.size.x = 1;
            if (b.size.y <= 0) b.size.y = 1;
            if (b.size.z <= 0) b.size.z = 1;
        }
    return true;
}

bool SaveStore::init(const std::string& d) {
    dir = d;
    mkdir(dir.c_str(), 0755);
    return true;
}

bool SaveStore::load(SaveData& out) {
    std::string path = dir + "/save.json";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string s(n, 0);
    fread(&s[0], 1, n, f);
    fclose(f);
    return out.fromJson(s);
}

bool SaveStore::save(const SaveData& data) {
    std::string path = dir + "/save.json";
    FILE* f = fopen((path + ".tmp").c_str(), "wb");
    if (!f) return false;
    std::string s = data.toJson();
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
    rename((path + ".tmp").c_str(), path.c_str());
    return true;
}

std::string cloudPackSave(const SaveData& data) { return data.toJson(); }
bool cloudUnpackSave(const std::string& blob, SaveData& out) { return out.fromJson(blob); }

} // namespace lv
