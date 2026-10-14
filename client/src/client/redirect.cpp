#include "redirect.hpp"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace fc5mp::redirect {
namespace {

struct Config {
    uint32_t server_ip;    // network byte order
    uint16_t prudp_port;   // host byte order
    uint16_t rest_port;    // host byte order (Phase 2)
};

bool g_active = false;
Config g_cfg{};

std::string trim(const std::string& s){
    size_t a = s.find_first_not_of(" \t\r\n");
    if(a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::string unquote(const std::string& s){
    if(s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

bool parse_config(const char* path, Config& cfg){
    FILE* f = fopen(path, "r");
    if(!f) return false;
    bool has_ip = false, has_prudp = false;
    char line[256];
    while(fgets(line, sizeof(line), f)){
        std::string l = trim(line);
        if(l.empty() || l[0] == '#') continue;
        auto eq = l.find('=');
        if(eq == std::string::npos) continue;
        auto key = trim(l.substr(0, eq));
        auto val = unquote(trim(l.substr(eq + 1)));
        if(key == "server_ip"){
            uint32_t addr = inet_addr(val.c_str());
            if(addr != INADDR_NONE){ cfg.server_ip = addr; has_ip = true; }
        } else if(key == "prudp_port"){
            int p = atoi(val.c_str());
            if(p > 0 && p <= 65535){ cfg.prudp_port = (uint16_t)p; has_prudp = true; }
        } else if(key == "rest_port"){
            int p = atoi(val.c_str());
            if(p > 0 && p <= 65535) cfg.rest_port = (uint16_t)p;
        }
    }
    fclose(f);
    return has_ip && has_prudp;
}

std::string config_path(){
    const char* env = getenv("FC5MP_REDIRECT");
    if(env && env[0]) return env;
    const char* home = getenv("USERPROFILE");
    if(!home) home = getenv("HOME");
    if(!home) return "";
    return std::string(home) + "/.fc5mp/redirect.toml";
}

}  // namespace

bool load(){
    auto path = config_path();
    if(path.empty()) return false;
    Config cfg{};
    if(!parse_config(path.c_str(), cfg)) return false;
    g_cfg = cfg;
    g_active = true;
    return true;
}

bool maybe_rewrite_dest(sockaddr* dest, int destlen){
    if(!g_active || !dest || destlen < (int)sizeof(sockaddr_in)) return false;
    auto* sin = reinterpret_cast<sockaddr_in*>(dest);
    if(sin->sin_family != AF_INET) return false;
    if(ntohs(sin->sin_port) != 6570) return false;
    sin->sin_addr.s_addr = g_cfg.server_ip;
    sin->sin_port = htons(g_cfg.prudp_port);
    return true;
}

bool active(){ return g_active; }

}
