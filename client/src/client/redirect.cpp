#include "redirect.hpp"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <atomic>
#include <cctype>
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
    uint16_t rest_port;    // host byte order
};

bool g_active = false;
Config g_cfg{};

// Phase 2: lock-free set of redirected sockets (REST connections via our server)
constexpr int kMaxRedirected = 16;
std::atomic<uint64_t> g_redirected[kMaxRedirected]{};

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

bool ends_with_ci(const char* s, size_t slen, const char* suffix, size_t suflen){
    if(slen < suflen) return false;
    for(size_t i = 0; i < suflen; i++)
        if(std::tolower((unsigned char)s[slen - suflen + i]) != suffix[i]) return false;
    return true;
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

uint32_t server_ip_nbo(){ return g_cfg.server_ip; }

const char* server_ip_str(){
    static thread_local char buf[16];
    auto* b = reinterpret_cast<const uint8_t*>(&g_cfg.server_ip);
    snprintf(buf, sizeof buf, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return buf;
}

uint16_t rest_port(){ return g_cfg.rest_port; }

bool is_redirect_hostname(const char* name){
    if(!name || !g_active) return false;
    size_t len = std::strlen(name);
    return ends_with_ci(name, len, ".ubi.com", 8) ||
           ends_with_ci(name, len, ".ubisoft.com", 12) ||
           (len == 7 && ends_with_ci(name, len, "ubi.com", 7)) ||
           (len == 11 && ends_with_ci(name, len, "ubisoft.com", 11));
}

void mark_redirected(uint64_t sock){
    for(auto& s : g_redirected){
        uint64_t expected = 0;
        if(s.compare_exchange_strong(expected, sock)) return;
        if(expected == sock) return;
    }
}

bool is_redirected(uint64_t sock){
    if(!sock) return false;
    for(const auto& s : g_redirected)
        if(s.load(std::memory_order_relaxed) == sock) return true;
    return false;
}

int ssl_get_fd(void* ssl){
    if(!ssl) return -1;
    // OpenSSL 1.0.2 struct layout (x86_64):
    //   SSL->rbio at offset 0x10
    //   BIO->num  at offset 0x28  (socket fd for socket BIOs)
    // Verified: SSL->handshake_func at offset 0x30 matches capture signatures.
    auto* rbio = *reinterpret_cast<void**>(static_cast<uint8_t*>(ssl) + 0x10);
    if(!rbio) return -1;
    return *reinterpret_cast<int*>(static_cast<uint8_t*>(rbio) + 0x28);
}

}
