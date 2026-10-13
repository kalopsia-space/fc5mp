#include "../../src/capture/redact.hpp"
#include "../../src/capture/udp_frame.hpp"
#include <cassert>
#include <cstring>
#include <random>
#include <string>
#include <vector>
using namespace fc5mp::cap;

static std::string R(std::string s){
  size_t n = redact(reinterpret_cast<uint8_t*>(s.data()), s.size());
  (void)n; return s;
}

int main(){
  // Authorization header: value masked up to CRLF, name kept, case-insensitive
  { std::string in = "POST /v3/profiles/sessions HTTP/1.1\r\nHost: x\r\nAuthorization: Ubi_v1 t=SECRET123\r\nUbi-AppId: abc\r\n\r\n";
    std::string out = R(in);
    assert(out.size() == in.size());
    assert(out.find("SECRET") == std::string::npos);
    assert(out.find("Authorization: ******************\r\n") != std::string::npos);
    assert(out.find("Ubi-AppId: abc") != std::string::npos);
    std::string lc = R("authorization: Basic Zm9vOmJhcg==\r\n");
    assert(lc == "authorization: ******************\r\n");
  }
  // header without CRLF at buffer end: masked to end
  assert(R("Authorization: Basic abc") == "Authorization: *********");
  // header name at very end, no value: nothing to mask, no OOB
  assert(R("Authorization:") == "Authorization:");
  assert(R("Authoriz") == "Authoriz");

  // JSON secret keys, with and without whitespace, multiple occurrences
  { std::string in = R"({"ticket":"abc.def","sessionKey" : "k1","name":"bob","rememberMeTicket":"zz"})";
    std::string out = R(in);
    assert(out.size() == in.size());
    assert(out == R"({"ticket":"*******","sessionKey" : "**","name":"bob","rememberMeTicket":"**"})");
  }
  assert(R(R"({"password":"hunter2","twoFactorAuthenticationTicket":"t"})")
         == R"({"password":"*******","twoFactorAuthenticationTicket":"*"})");
  // non-secret keys that merely contain a secret name are untouched
  assert(R(R"({"ticketType":"x","myticket":"y"})") == R"({"ticketType":"x","myticket":"y"})");
  // escaped quote inside a value doesn't end it early
  assert(R(R"({"ticket":"a\"b","n":1})") == R"({"ticket":"****","n":1})");
  // unterminated value (split across reads): masked to the end
  assert(R(R"({"ticket":"abcdef)") == R"({"ticket":"******)");
  // key at the very end, value not started yet
  assert(R(R"({"ticket":)") == R"({"ticket":)");
  assert(R(R"({"ticket")") == R"({"ticket")");
  // non-string value: left alone
  assert(R(R"({"ticket":null})") == R"({"ticket":null})");

  // redact returns how many bytes it masked
  { std::string s = R"({"ticket":"abc"})";
    assert(redact(reinterpret_cast<uint8_t*>(s.data()), s.size()) == 3); }

  // fuzz: random bytes salted with key fragments never crash and keep length
  { std::mt19937 rng(1234);
    const char* frags[] = {"\"ticket\"", ":", "\"", "\\", "Authorization:", "\r\n", " "};
    for(int it = 0; it < 20000; it++){
      std::vector<uint8_t> v(rng() % 64);
      for(auto& b : v) b = (uint8_t)rng();
      for(int k = 0; k < 3 && !v.empty(); k++){
        const char* f = frags[rng() % 7]; size_t fl = std::strlen(f);
        size_t at = rng() % v.size();
        for(size_t j = 0; j < fl && at + j < v.size(); j++) v[at + j] = (uint8_t)f[j];
      }
      size_t before = v.size();
      redact(v.data(), v.size());
      assert(v.size() == before);
    }
    redact(nullptr, 0);
  }

  // encode_tls: [u64 ssl][redacted bytes], caller's buffer untouched, capped
  { std::string body = R"({"ticket":"abc"})";
    auto rec = encode_tls(0xABCDu, reinterpret_cast<const uint8_t*>(body.data()), body.size());
    assert(rec.size() == 8 + body.size());
    uint64_t ssl; std::memcpy(&ssl, rec.data(), 8); assert(ssl == 0xABCD);
    assert(std::string(rec.begin() + 8, rec.end()) == R"({"ticket":"***"})");
    assert(body == R"({"ticket":"abc"})");          // original not modified
    std::vector<uint8_t> big(kMaxPayload + 5, 'a');
    assert(encode_tls(1, big.data(), big.size()).size() == 8 + kMaxPayload);
  }
  return 0;
}
