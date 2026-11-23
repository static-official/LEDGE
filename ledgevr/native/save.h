// LEDGE VR — local + cloud saves (JSON)
#pragma once
#include "world.h"
#include <string>

namespace lv {

struct SaveData {
    GameSettings settings;
    std::vector<Level> myLevels;      // user-created levels
    std::vector<std::string> friends; // added people (user ids or names)
    std::vector<std::string> ownedCosmetics; // cosmetic names owned (all free)
    int32_t goalsCompleted = 0;

    std::string toJson() const;
    bool fromJson(const std::string& s);
};

class SaveStore {
public:
    bool init(const std::string& dir);      // internalDataPath
    bool load(SaveData& out);
    bool save(const SaveData& data);
    std::string dir;
};

// cloud sync via photon: serialize SaveData and publish to room props
std::string cloudPackSave(const SaveData& data);
bool cloudUnpackSave(const std::string& blob, SaveData& out);

} // namespace lv
