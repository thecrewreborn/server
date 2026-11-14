#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include "server/tls.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "mbedtls/dhm.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/rsa.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ssl_ciphersuites.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include "core/logtext.h"
#include "server/http_proto.h"

namespace tcr::tls {

const char* const kNames[2] = {"public-ubiservices.ubi.com", "*.ubi.com"};

namespace {

plat::Mutex g_lock;
bool g_psa_ready = false;

constexpr int kRecvFailed = -0x004C;
constexpr int kSendFailed = -0x004E;

bool psa_ready() {
  plat::Lock lock(g_lock);
  if (!g_psa_ready) g_psa_ready = psa_crypto_init() == PSA_SUCCESS;
  return g_psa_ready;
}

int system_rng(void*, unsigned char* out, size_t n) {
  return plat::random_bytes(out, n) ? 0 : MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
}

std::string mbed_text(int code) {
  char buf[200];
  mbedtls_strerror(code, buf, sizeof(buf));
  return buf;
}

const int kCipherSuites[] = {
    MBEDTLS_TLS1_3_AES_256_GCM_SHA384,
    MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS1_3_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
    MBEDTLS_TLS_DHE_RSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_DHE_RSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_DHE_RSA_WITH_AES_256_CBC_SHA256,
    MBEDTLS_TLS_DHE_RSA_WITH_AES_128_CBC_SHA256,
    0,
};

const unsigned char kDhP[] = MBEDTLS_DHM_RFC7919_FFDHE2048_P_BIN;
const unsigned char kDhG[] = MBEDTLS_DHM_RFC7919_FFDHE2048_G_BIN;

std::string alert_reason(int desc) {
  switch (desc) {
    case 0:
      return "SSLV3_ALERT_CLOSE_NOTIFY";
    case 10:
      return "SSLV3_ALERT_UNEXPECTED_MESSAGE";
    case 20:
      return "SSLV3_ALERT_BAD_RECORD_MAC";
    case 21:
      return "TLSV1_ALERT_DECRYPTION_FAILED";
    case 22:
      return "TLSV1_ALERT_RECORD_OVERFLOW";
    case 30:
      return "SSLV3_ALERT_DECOMPRESSION_FAILURE";
    case 40:
      return "SSLV3_ALERT_HANDSHAKE_FAILURE";
    case 41:
      return "SSLV3_ALERT_NO_CERTIFICATE";
    case 42:
      return "SSLV3_ALERT_BAD_CERTIFICATE";
    case 43:
      return "SSLV3_ALERT_UNSUPPORTED_CERTIFICATE";
    case 44:
      return "SSLV3_ALERT_CERTIFICATE_REVOKED";
    case 45:
      return "SSLV3_ALERT_CERTIFICATE_EXPIRED";
    case 46:
      return "SSLV3_ALERT_CERTIFICATE_UNKNOWN";
    case 47:
      return "SSLV3_ALERT_ILLEGAL_PARAMETER";
    case 48:
      return "TLSV1_ALERT_UNKNOWN_CA";
    case 49:
      return "TLSV1_ALERT_ACCESS_DENIED";
    case 50:
      return "TLSV1_ALERT_DECODE_ERROR";
    case 51:
      return "TLSV1_ALERT_DECRYPT_ERROR";
    case 60:
      return "TLSV1_ALERT_EXPORT_RESTRICTION";
    case 70:
      return "TLSV1_ALERT_PROTOCOL_VERSION";
    case 71:
      return "TLSV1_ALERT_INSUFFICIENT_SECURITY";
    case 80:
      return "TLSV1_ALERT_INTERNAL_ERROR";
    case 86:
      return "TLSV1_ALERT_INAPPROPRIATE_FALLBACK";
    case 90:
      return "TLSV1_ALERT_USER_CANCELLED";
    case 100:
      return "TLSV1_ALERT_NO_RENEGOTIATION";
    case 109:
      return "TLSV13_ALERT_MISSING_EXTENSION";
    case 110:
      return "TLSV1_ALERT_UNSUPPORTED_EXTENSION";
    case 112:
      return "TLSV1_ALERT_UNRECOGNIZED_NAME";
    case 113:
      return "TLSV1_ALERT_BAD_CERTIFICATE_STATUS_RESPONSE";
    case 115:
      return "TLSV1_ALERT_UNKNOWN_PSK_IDENTITY";
    case 116:
      return "TLSV13_ALERT_CERTIFICATE_REQUIRED";
    case 120:
      return "TLSV1_ALERT_NO_APPLICATION_PROTOCOL";
    default:
      return "SSLV3_ALERT_UNKNOWN";
  }
}

std::string error_reason(int code) {
  switch (code) {
    case MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION:
      return "UNSUPPORTED_PROTOCOL";
    case MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE:
      return "NO_SHARED_CIPHER";
    case MBEDTLS_ERR_SSL_INVALID_MAC:
      return "DECRYPTION_FAILED_OR_BAD_RECORD_MAC";
    case MBEDTLS_ERR_SSL_ALLOC_FAILED:
    case MBEDTLS_ERR_SSL_INTERNAL_ERROR:
    case MBEDTLS_ERR_SSL_BAD_CONFIG:
      return "INTERNAL_ERROR";
    default:
      return "UNEXPECTED_MESSAGE";
  }
}

struct Conn {
  plat::Socket sock = plat::kNoSocket;
  std::string head;
  size_t head_used = 0;
  bool checked = false;
  std::string refused;
  bool os_failed = false;
  plat::Error os;
  bool buffering = true;
  std::string out;
};

int flush_out(Conn* c) {
  while (!c->out.empty()) {
    plat::Error err;
    long r = plat::sock_send(c->sock, c->out.data(), c->out.size(), &err);
    if (r == plat::kWouldBlock) return MBEDTLS_ERR_SSL_WANT_WRITE;
    if (r < 0) {
      c->os_failed = true;
      c->os = err;
      return kSendFailed;
    }
    c->out.erase(0, static_cast<size_t>(r));
  }
  return 0;
}

std::string not_tls_reason(const std::string& p) {
  auto starts = [&](const char* prefix) { return p.compare(0, std::strlen(prefix), prefix) == 0; };
  if (starts("GET ") || starts("POST ") || starts("HEAD ") || starts("PATCH") || starts("OPTIO") ||
      starts("DELET") || starts("TRACE") || starts("PUT ")) {
    return "HTTP_REQUEST";
  }
  if (starts("CONNE")) return "HTTPS_PROXY_REQUEST";
  return "WRONG_VERSION_NUMBER";
}

int bio_recv(void* ctx, unsigned char* buf, size_t len) {
  Conn* c = static_cast<Conn*>(ctx);
  int flushed = flush_out(c);
  if (flushed != 0) return flushed;
  if (!c->checked) {
    while (c->head.size() < 5) {
      char tmp[5];
      plat::Error err;
      long r = plat::sock_recv(c->sock, tmp, 5 - c->head.size(), &err);
      if (r == plat::kWouldBlock) return MBEDTLS_ERR_SSL_WANT_READ;
      if (r < 0) {
        c->os_failed = true;
        c->os = err;
        return kRecvFailed;
      }
      if (r == 0) return 0;
      c->head.append(tmp, static_cast<size_t>(r));
    }
    const unsigned char* p = reinterpret_cast<const unsigned char*>(c->head.data());
    bool sslv2_hello = (p[0] & 0x80) && p[2] == 1;
    if (!sslv2_hello && p[1] != 3) {
      c->refused = not_tls_reason(c->head);
      return MBEDTLS_ERR_SSL_INVALID_RECORD;
    }
    c->checked = true;
  }
  if (c->head_used < c->head.size()) {
    size_t n = c->head.size() - c->head_used;
    if (n > len) n = len;
    std::memcpy(buf, c->head.data() + c->head_used, n);
    c->head_used += n;
    return static_cast<int>(n);
  }
  plat::Error err;
  long r = plat::sock_recv(c->sock, buf, len > 0x7fffffff ? 0x7fffffff : len, &err);
  if (r == plat::kWouldBlock) return MBEDTLS_ERR_SSL_WANT_READ;
  if (r < 0) {
    c->os_failed = true;
    c->os = err;
    return kRecvFailed;
  }
  return static_cast<int>(r);
}

int bio_send(void* ctx, const unsigned char* buf, size_t len) {
  Conn* c = static_cast<Conn*>(ctx);
  if (c->buffering) {
    c->out.append(reinterpret_cast<const char*>(buf), len);
    return static_cast<int>(len > 0x7fffffff ? 0x7fffffff : len);
  }
  int flushed = flush_out(c);
  if (flushed != 0) return flushed;
  plat::Error err;
  long r = plat::sock_send(c->sock, buf, len > 0x7fffffff ? 0x7fffffff : len, &err);
  if (r == plat::kWouldBlock) return MBEDTLS_ERR_SSL_WANT_WRITE;
  if (r < 0) {
    c->os_failed = true;
    c->os = err;
    return kSendFailed;
  }
  return static_cast<int>(r);
}

double remaining(double deadline) {
  if (deadline < 0) return -1;
  double left = deadline - plat::monotonic();
  return left < 0 ? 0 : left;
}

class TlsStream : public server::Stream {
 public:
  TlsStream(mbedtls_ssl_config* conf, plat::Socket s, bool* ok) {
    conn_.sock = s;
    plat::Lock lock(g_lock);
    mbedtls_ssl_init(&ssl_);
    *ok = mbedtls_ssl_setup(&ssl_, conf) == 0;
    mbedtls_ssl_set_bio(&ssl_, &conn_, bio_send, bio_recv, nullptr);
  }
  ~TlsStream() override {
    plat::Lock lock(g_lock);
    mbedtls_ssl_free(&ssl_);
  }

  server::IoStatus read(uint8_t* buf, size_t n, double timeout, size_t* got) override {
    *got = 0;
    double deadline = timeout >= 0 ? plat::monotonic() + timeout : -1;
    for (;;) {
      int r;
      {
        plat::Lock lock(g_lock);
        r = mbedtls_ssl_read(&ssl_, buf, n);
        if (conn_.buffering && mbedtls_ssl_is_handshake_over(&ssl_)) conn_.buffering = false;
      }
      if (r >= 0) {
        *got = static_cast<size_t>(r);
        return server::IoStatus();
      }
      if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == MBEDTLS_ERR_SSL_CONN_EOF) return server::IoStatus();
      server::IoStatus waited;
      if (wait(r, deadline, &waited)) {
        if (!waited.ok()) return waited;
        continue;
      }
      return failure(r);
    }
  }

  server::IoStatus write_all(const uint8_t* data, size_t n, double timeout) override {
    double deadline = timeout >= 0 ? plat::monotonic() + timeout : -1;
    while (n > 0) {
      int r;
      {
        plat::Lock lock(g_lock);
        r = mbedtls_ssl_write(&ssl_, data, n);
        if (conn_.buffering && mbedtls_ssl_is_handshake_over(&ssl_)) conn_.buffering = false;
      }
      if (r > 0) {
        data += r;
        n -= static_cast<size_t>(r);
        continue;
      }
      server::IoStatus waited;
      if (wait(r, deadline, &waited)) {
        if (!waited.ok()) return waited;
        continue;
      }
      return failure(r);
    }
    return server::IoStatus();
  }

  plat::Socket socket() const override { return conn_.sock; }

 private:
  bool wait(int r, double deadline, server::IoStatus* st) {
    *st = server::IoStatus();
    int w;
    plat::Error err;
    if (r == MBEDTLS_ERR_SSL_WANT_READ) {
      w = plat::wait_readable(conn_.sock, remaining(deadline), &err);
    } else if (r == MBEDTLS_ERR_SSL_WANT_WRITE) {
      w = plat::wait_writable(conn_.sock, remaining(deadline), &err);
    } else if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET || r == MBEDTLS_ERR_SSL_RECEIVED_EARLY_DATA) {
      return true;
    } else {
      return false;
    }
    if (w < 0) *st = server::IoStatus::os_error(err);
    if (w == 0) *st = server::IoStatus::timeout();
    return true;
  }

  server::IoStatus failure(int r) {
    if (conn_.os_failed && (r == kRecvFailed || r == kSendFailed)) {
      return server::IoStatus::os_error(conn_.os);
    }
    server::IoStatus st;
    st.kind = server::IoStatus::kSslError;
    st.ssl_type = "SSLError";
    if (!conn_.refused.empty()) {
      st.ssl_reason = conn_.refused;
    } else if (r == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE && ssl_.in_msg) {
      st.ssl_reason = alert_reason(ssl_.in_msg[1]);
    } else if (r == MBEDTLS_ERR_SSL_INVALID_MAC && ssl_.in_hdr && ssl_.in_hdr[0] == MBEDTLS_SSL_MSG_ALERT &&
               ssl_.in_hdr[3] == 0 && ssl_.in_hdr[4] == 2) {
      st.ssl_reason = alert_reason(ssl_.in_hdr[6]);
    } else {
      st.ssl_reason = error_reason(r);
    }
    return st;
  }

  Conn conn_;
  mbedtls_ssl_context ssl_;
};

}

struct ServerContext::Impl {
  mbedtls_ssl_config conf;
  mbedtls_x509_crt cert;
  mbedtls_pk_context key;
  bool loaded = false;
};

ServerContext::ServerContext() : impl_(new Impl) {
  plat::Lock lock(g_lock);
  mbedtls_ssl_config_init(&impl_->conf);
  mbedtls_x509_crt_init(&impl_->cert);
  mbedtls_pk_init(&impl_->key);
}

ServerContext::~ServerContext() {
  {
    plat::Lock lock(g_lock);
    mbedtls_ssl_config_free(&impl_->conf);
    mbedtls_x509_crt_free(&impl_->cert);
    mbedtls_pk_free(&impl_->key);
  }
  delete impl_;
}

bool ServerContext::load(const std::string& cert_path, const std::string& key_path, std::string* why,
                         plat::Error* os_error) {
  if (os_error) *os_error = plat::Error();
  if (!psa_ready()) {
    if (why) *why = "the crypto library did not start";
    return false;
  }
  std::string cert_pem, key_pem;
  plat::Error err;
  if (!plat::read_file(cert_path, &cert_pem, &err)) {
    if (why) *why = plat::python_error_text(err);
    if (os_error) *os_error = err;
    return false;
  }
  plat::Lock lock(g_lock);
  int r = mbedtls_x509_crt_parse(&impl_->cert, reinterpret_cast<const unsigned char*>(cert_pem.c_str()),
                                 cert_pem.size() + 1);
  if (r != 0 || cert_pem.find("-----BEGIN CERTIFICATE-----") == std::string::npos) {
    if (why) *why = "[SSL] PEM lib (_ssl.c:4097)";
    return false;
  }
  if (!plat::read_file(key_path, &key_pem, &err)) {
    if (why) *why = plat::python_error_text(err);
    if (os_error) *os_error = err;
    return false;
  }
  bool pem = key_pem.find("-----BEGIN") != std::string::npos;
  r = mbedtls_pk_parse_key(&impl_->key, reinterpret_cast<const unsigned char*>(key_pem.c_str()), key_pem.size() + 1,
                           nullptr, 0, system_rng, nullptr);
  mbedtls_platform_zeroize(&key_pem[0], key_pem.size());
  if (r != 0 || !pem) {
    if (why) *why = "[SSL] PEM lib (_ssl.c:4118)";
    return false;
  }
  if (mbedtls_pk_check_pair(&impl_->cert.pk, &impl_->key, system_rng, nullptr) != 0) {
    if (why) *why = "[X509: KEY_VALUES_MISMATCH] key values mismatch (_ssl.c:4118)";
    return false;
  }
  mbedtls_ssl_config* conf = &impl_->conf;
  r = mbedtls_ssl_config_defaults(conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                  MBEDTLS_SSL_PRESET_DEFAULT);
  if (r == 0) {
    mbedtls_ssl_conf_rng(conf, system_rng, nullptr);
    mbedtls_ssl_conf_min_tls_version(conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(conf, MBEDTLS_SSL_VERSION_TLS1_3);
    mbedtls_ssl_conf_ciphersuites(conf, kCipherSuites);
    mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);
    r = mbedtls_ssl_conf_dh_param_bin(conf, kDhP, sizeof(kDhP), kDhG, sizeof(kDhG));
  }
  if (r == 0) r = mbedtls_ssl_conf_own_cert(conf, &impl_->cert, &impl_->key);
  if (r != 0) {
    if (why) *why = mbed_text(r);
    return false;
  }
  impl_->loaded = true;
  return true;
}

std::unique_ptr<server::Stream> ServerContext::wrap(plat::Socket s) {
  if (!impl_->loaded) return nullptr;
  bool ok = false;
  std::unique_ptr<TlsStream> stream(new TlsStream(&impl_->conf, s, &ok));
  if (!ok) return nullptr;
  return std::unique_ptr<server::Stream>(stream.release());
}

namespace {

bool parse_ipv4(const std::string& ip, unsigned char out[4]) {
  int parts = 0;
  size_t i = 0;
  while (parts < 4) {
    if (i >= ip.size() || ip[i] < '0' || ip[i] > '9') return false;
    int value = 0, digits = 0;
    while (i < ip.size() && ip[i] >= '0' && ip[i] <= '9') {
      value = value * 10 + (ip[i] - '0');
      if (++digits > 3 || value > 255) return false;
      ++i;
    }
    if (digits > 1 && ip[i - digits] == '0') return false;
    out[parts++] = static_cast<unsigned char>(value);
    if (parts < 4) {
      if (i >= ip.size() || ip[i] != '.') return false;
      ++i;
    }
  }
  return i == ip.size();
}

bool parse_ipv6(const std::string& text, unsigned char out[16]) {
  std::string ip = text;
  size_t pct = ip.find('%');
  if (pct != std::string::npos) {
    std::string scope = ip.substr(pct + 1);
    if (scope.empty() || scope.find('%') != std::string::npos) return false;
    ip = ip.substr(0, pct);
  }
  std::vector<std::string> parts;
  size_t start = 0;
  for (;;) {
    size_t colon = ip.find(':', start);
    parts.push_back(ip.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
    if (colon == std::string::npos) break;
    start = colon + 1;
  }
  if (parts.size() < 3) return false;
  if (parts.back().find('.') != std::string::npos) {
    unsigned char v4[4];
    if (!parse_ipv4(parts.back(), v4)) return false;
    parts.pop_back();
    char hex[8];
    std::snprintf(hex, sizeof(hex), "%x", (v4[0] << 8) | v4[1]);
    parts.push_back(hex);
    std::snprintf(hex, sizeof(hex), "%x", (v4[2] << 8) | v4[3]);
    parts.push_back(hex);
  }
  if (parts.size() > 9) return false;
  size_t skip = 0;
  for (size_t i = 1; i + 1 < parts.size(); ++i) {
    if (parts[i].empty()) {
      if (skip) return false;
      skip = i;
    }
  }
  size_t hi, lo, skipped;
  if (skip) {
    hi = skip;
    lo = parts.size() - skip - 1;
    if (parts.front().empty() && --hi) return false;
    if (parts.back().empty() && --lo) return false;
    if (hi + lo >= 8) return false;
    skipped = 8 - hi - lo;
  } else {
    if (parts.size() != 8 || parts.front().empty() || parts.back().empty()) return false;
    hi = 8;
    lo = 0;
    skipped = 0;
  }
  uint16_t words[8] = {0};
  auto hextet = [](const std::string& h, uint16_t* w) {
    if (h.empty() || h.size() > 4) return false;
    unsigned v = 0;
    for (char c : h) {
      int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10
              : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
      if (d < 0) return false;
      v = v * 16 + static_cast<unsigned>(d);
    }
    *w = static_cast<uint16_t>(v);
    return true;
  };
  size_t n = 0;
  for (size_t i = 0; i < hi; ++i) {
    if (!hextet(parts[i], &words[n++])) return false;
  }
  n += skipped;
  for (size_t i = parts.size() - lo; i < parts.size(); ++i) {
    if (!hextet(parts[i], &words[n++])) return false;
  }
  for (int i = 0; i < 8; ++i) {
    out[2 * i] = static_cast<unsigned char>(words[i] >> 8);
    out[2 * i + 1] = static_cast<unsigned char>(words[i] & 0xff);
  }
  return true;
}

bool parse_ip(const std::string& ip, unsigned char out[16], size_t* len) {
  if (parse_ipv4(ip, out)) {
    *len = 4;
    return true;
  }
  if (parse_ipv6(ip, out)) {
    *len = 16;
    return true;
  }
  return false;
}

std::string asn1_time(double t) {
  http::UtcTime u = http::utc_time(t);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d", u.year, u.month, u.day, u.hour, u.minute, u.second);
  return buf;
}

}

bool ensure(const std::string& cert_path, const std::string& key_path, const std::string& ip, bool* wrote,
            std::string* why, plat::Error* os_error, std::string* os_path) {
  *wrote = false;
  if (plat::path_exists(cert_path) && plat::path_exists(key_path)) return true;
  unsigned char ip_bytes[16];
  size_t ip_len = 0;
  if (!ip.empty() && !parse_ip(ip, ip_bytes, &ip_len)) {
    if (why) *why = tcr::repr_utf8(ip) + " does not appear to be an IPv4 or IPv6 address";
    return false;
  }
  if (!psa_ready()) {
    if (why) *why = "the crypto library did not start";
    return false;
  }

  std::vector<unsigned char> key_pem(16000), cert_pem(16000);
  int r = 0;
  {
    plat::Lock lock(g_lock);
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    mbedtls_x509write_cert crt;
    mbedtls_x509write_crt_init(&crt);

    r = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (r == 0) r = mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), system_rng, nullptr, 2048, 65537);

    unsigned char serial[20];
    if (r == 0 && !plat::random_bytes(serial, sizeof(serial))) r = MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    for (int i = 19; i > 0; --i) serial[i] = static_cast<unsigned char>((serial[i] >> 1) | (serial[i - 1] << 7));
    serial[0] >>= 1;
    size_t skip = 0;
    while (skip < 19 && serial[skip] == 0) ++skip;

    double now = plat::wall_time();
    std::string subject = std::string("CN=") + kNames[0];
    if (r == 0) {
      mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
      mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
      mbedtls_x509write_crt_set_subject_key(&crt, &key);
      mbedtls_x509write_crt_set_issuer_key(&crt, &key);
      r = mbedtls_x509write_crt_set_subject_name(&crt, subject.c_str());
    }
    if (r == 0) r = mbedtls_x509write_crt_set_issuer_name(&crt, subject.c_str());
    if (r == 0) r = mbedtls_x509write_crt_set_serial_raw(&crt, serial + skip, sizeof(serial) - skip);
    if (r == 0) {
      r = mbedtls_x509write_crt_set_validity(&crt, asn1_time(now - 86400).c_str(),
                                             asn1_time(now + 3650.0 * 86400).c_str());
    }
    if (r == 0) r = mbedtls_x509write_crt_set_basic_constraints(&crt, 1, -1);
    mbedtls_x509_san_list sans[3];
    std::memset(sans, 0, sizeof(sans));
    size_t count = 0;
    for (const char* name : kNames) {
      sans[count].node.type = MBEDTLS_X509_SAN_DNS_NAME;
      sans[count].node.san.unstructured_name.p =
          reinterpret_cast<unsigned char*>(const_cast<char*>(name));
      sans[count].node.san.unstructured_name.len = std::strlen(name);
      ++count;
    }
    if (!ip.empty()) {
      sans[count].node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
      sans[count].node.san.unstructured_name.p = ip_bytes;
      sans[count].node.san.unstructured_name.len = ip_len;
      ++count;
    }
    for (size_t i = count - 1; i > 0; --i) sans[i].next = &sans[i - 1];
    if (r == 0) r = mbedtls_x509write_crt_set_subject_alternative_name(&crt, &sans[count - 1]);
    if (r == 0) r = mbedtls_x509write_crt_pem(&crt, cert_pem.data(), cert_pem.size(), system_rng, nullptr);
    if (r == 0) r = mbedtls_pk_write_key_pem(&key, key_pem.data(), key_pem.size());
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
  }
  if (r != 0) {
    if (why) *why = mbed_text(r);
    return false;
  }
  size_t key_len = std::strlen(reinterpret_cast<const char*>(key_pem.data()));
  size_t cert_len = std::strlen(reinterpret_cast<const char*>(cert_pem.data()));
  plat::Error err;
  for (const std::string* path : {&cert_path, &key_path}) {
    std::string abs;
    if (!plat::abs_path(*path, &abs, &err)) {
      if (why) *why = plat::python_error_text(err);
      if (os_error) *os_error = err;
      return false;
    }
    std::string folder = plat::dir_name(abs);
    if (!plat::make_dirs(folder, &err)) {
      if (why) *why = plat::python_error_text(err);
      if (os_error) *os_error = err;
      if (os_path) *os_path = folder;
      return false;
    }
  }
  bool ok = plat::write_atomic(key_path, key_pem.data(), key_len, true, &err);
  mbedtls_platform_zeroize(key_pem.data(), key_pem.size());
  if (!ok) {
    if (why) *why = plat::python_error_text(err);
    if (os_error) *os_error = err;
    if (os_path) *os_path = key_path + ".tmp";
    return false;
  }
  if (!plat::write_file(cert_path, cert_pem.data(), cert_len, &err)) {
    if (why) *why = plat::python_error_text(err) + ": " + tcr::repr_utf8(cert_path);
    if (os_error) *os_error = err;
    if (os_path) *os_path = cert_path;
    return false;
  }
  *wrote = true;
  return true;
}

}
