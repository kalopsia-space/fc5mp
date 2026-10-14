#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "client/redirect.hpp"
using namespace fc5mp::redirect;

static void write_config(const char* path, const char* content){
    FILE* f = fopen(path, "w");
    assert(f);
    fputs(content, f);
    fclose(f);
}

int main(){
    // Before load: redirect is disabled
    assert(!active());
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(6570);
    sa.sin_addr.s_addr = inet_addr("1.2.3.4");
    assert(!maybe_rewrite_dest((sockaddr*)&sa, sizeof(sa)));
    assert(sa.sin_addr.s_addr == inet_addr("1.2.3.4"));

    // Phase 2 functions return defaults when inactive
    assert(!is_redirect_hostname("public-ubiservices.ubi.com"));
    assert(!is_redirected(42));

    // Load config
    char path[] = "/tmp/test_redirect_XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    write_config(path,
        "# redirect config\n"
        "server_ip = \"10.0.0.1\"\n"
        "prudp_port = 7000\n"
        "rest_port = 21000\n");
    setenv("FC5MP_REDIRECT", path, 1);
    assert(load());
    assert(active());

    // Phase 1: port 6570 → rewritten to 10.0.0.1:7000
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(6570);
    sa.sin_addr.s_addr = inet_addr("1.2.3.4");
    assert(maybe_rewrite_dest((sockaddr*)&sa, sizeof(sa)));
    assert(sa.sin_addr.s_addr == inet_addr("10.0.0.1"));
    assert(ntohs(sa.sin_port) == 7000);

    // Phase 1: port 443 → not rewritten (UDP redirect only matches 6570)
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(443);
    sa.sin_addr.s_addr = inet_addr("1.2.3.4");
    assert(!maybe_rewrite_dest((sockaddr*)&sa, sizeof(sa)));

    // Phase 1: non-AF_INET → not rewritten
    sockaddr_in6 sa6;
    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(6570);
    assert(!maybe_rewrite_dest((sockaddr*)&sa6, sizeof(sa6)));

    // Phase 1: edge cases
    assert(!maybe_rewrite_dest((sockaddr*)&sa, 4));
    assert(!maybe_rewrite_dest(nullptr, sizeof(sa)));

    // Phase 2: config accessors
    assert(server_ip_nbo() == inet_addr("10.0.0.1"));
    assert(strcmp(server_ip_str(), "10.0.0.1") == 0);
    assert(rest_port() == 21000);

    // Phase 2: hostname matching
    assert(is_redirect_hostname("public-ubiservices.ubi.com"));
    assert(is_redirect_hostname("msr-public-ubiservices.ubi.com"));
    assert(is_redirect_hostname("PUBLIC-UBISERVICES.UBI.COM"));
    assert(is_redirect_hostname("something.ubisoft.com"));
    assert(!is_redirect_hostname("evil-ubi.com"));
    assert(!is_redirect_hostname("notubi.com"));
    assert(!is_redirect_hostname("google.com"));
    assert(!is_redirect_hostname(nullptr));
    assert(!is_redirect_hostname(""));

    // Phase 2: socket tracking
    assert(!is_redirected(100));
    mark_redirected(100);
    assert(is_redirected(100));
    assert(!is_redirected(200));
    mark_redirected(100);  // duplicate is fine
    assert(is_redirected(100));
    mark_redirected(200);
    assert(is_redirected(200));

    // ssl_get_fd returns -1 for null
    assert(ssl_get_fd(nullptr) == -1);

    unlink(path);
    return 0;
}
