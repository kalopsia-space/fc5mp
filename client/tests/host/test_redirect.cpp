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

    // Load config — use a different prudp_port to verify rewrite changes both
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

    // Port 6570 → rewritten to 10.0.0.1:7000
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(6570);
    sa.sin_addr.s_addr = inet_addr("1.2.3.4");
    assert(maybe_rewrite_dest((sockaddr*)&sa, sizeof(sa)));
    assert(sa.sin_addr.s_addr == inet_addr("10.0.0.1"));
    assert(ntohs(sa.sin_port) == 7000);

    // Port 443 → not rewritten
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(443);
    sa.sin_addr.s_addr = inet_addr("1.2.3.4");
    assert(!maybe_rewrite_dest((sockaddr*)&sa, sizeof(sa)));
    assert(sa.sin_addr.s_addr == inet_addr("1.2.3.4"));
    assert(ntohs(sa.sin_port) == 443);

    // Non-AF_INET → not rewritten
    sockaddr_in6 sa6;
    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(6570);
    assert(!maybe_rewrite_dest((sockaddr*)&sa6, sizeof(sa6)));

    // destlen too small → not rewritten
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(6570);
    sa.sin_addr.s_addr = inet_addr("1.2.3.4");
    assert(!maybe_rewrite_dest((sockaddr*)&sa, 4));
    assert(sa.sin_addr.s_addr == inet_addr("1.2.3.4"));

    // null dest → not rewritten
    assert(!maybe_rewrite_dest(nullptr, sizeof(sa)));

    unlink(path);
    return 0;
}
