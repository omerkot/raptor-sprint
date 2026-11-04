#include "settings.hpp"

#include <fstream>
#include <sstream>

namespace rr {

void Settings::load(const std::string& path) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        int v = atoi(line.c_str() + eq + 1);
        if (k == "fx") fx = (int)clampf(v, 0, 10);
        else if (k == "forest") forest = (int)clampf(v, 0, 10);
        else if (k == "sprint_toggle") sprint_toggle = v != 0;
        else if (k == "bob") bob = (int)clampf(v, 0, 2);
        else if (k == "show_fps") show_fps = v != 0;
        else if (k == "detail") detail = (int)clampf(v, -1, 2);
        else if (k == "wild_region") wild_region = (int)clampf(v, 0, 5);
        else if (k == "length") length = (int)clampf(v, 0, 2);
        else if (k == "difficulty") difficulty = (int)clampf(v, 0, 2);
    }
}

void Settings::save(const std::string& path) const {
    std::ofstream out(path);
    out << "fx=" << fx << "\nforest=" << forest << "\nsprint_toggle=" << sprint_toggle << "\nbob=" << bob << "\nshow_fps=" << show_fps << "\ndetail=" << detail
        << "\nwild_region=" << wild_region << "\nlength=" << length << "\ndifficulty=" << difficulty << "\n";
}

float length_metres(int i) { return i == 0 ? 800.0f : i == 2 ? 3000.0f : 1600.0f; }
const char* length_name(int i) { return i == 0 ? "Short  800 m" : i == 2 ? "Long  3 km" : "Standard  1.6 km"; }

}  // namespace rr
