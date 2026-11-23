// Debug what jsonParse emits for a level JSON blob.
#include "../ledgevr/native/world.h"
#include <cstdio>
using namespace lv;
int main() {
    GameWorld W; W.loadHub();
    std::string j = W.level.toJson();
    printf("json len=%zu head=%s\n\n", j.size(), j.substr(0, 160).c_str());
    jsonParse(j, [](const std::string& path, const std::string& key, const std::string& val) {
        if (key == "px" || key == "[0]" || key == "name" || key == "v")
            printf("path='%s' key='%s' val=%s\n", path.c_str(), key.c_str(), val.c_str());
    });
    Level L2;
    bool ok = L2.fromJson(j);
    printf("\nfromJson=%d blocks=%zu\n", (int)ok, L2.blocks.size());
    return 0;
}
