#ifndef TCR_H
#define TCR_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(TCR_BUILD_SHARED)
#    define TCR_API __declspec(dllexport)
#  else
#    define TCR_API
#  endif
#else
#  define TCR_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TCR_OK 0
#define TCR_E_NOT_IMPLEMENTED (-1)
#define TCR_E_ARGUMENT (-2)
#define TCR_E_FAILED (-3)

TCR_API const char* tcr_status_text(int status);

TCR_API const char* tcr_version(void);

typedef struct tcr_out tcr_out;

#define TCR_OUT_BYTES 0
#define TCR_OUT_JSON 1
#define TCR_OUT_TEXT 2

TCR_API size_t tcr_out_count(const tcr_out* out);
TCR_API const uint8_t* tcr_out_data(const tcr_out* out, size_t i, size_t* n);
TCR_API int tcr_out_kind(const tcr_out* out, size_t i);
TCR_API const char* tcr_out_ip(const tcr_out* out, size_t i);
TCR_API int tcr_out_port(const tcr_out* out, size_t i);

typedef struct tcr_core tcr_core;

TCR_API tcr_core* tcr_core_new(const char* home, const char* config_ini_text);
TCR_API void tcr_core_free(tcr_core* core);

TCR_API void tcr_test_hooks(tcr_core* core, double epoch);

TCR_API void tcr_test_suite_defaults(tcr_core* core);

TCR_API void tcr_set_clock(tcr_core* core, double now);

TCR_API void tcr_set_config(tcr_core* core, const char* config_ini_text);

TCR_API int tcr_http(tcr_core* core, const char* method, const char* path, const char* body_json, const char* host,
                     const char* client_ip, int client_port, const char* auth, const uint8_t* raw, size_t raw_n,
                     tcr_out** reply);

TCR_API int tcr_accounts_open_link(tcr_core* core, const char* name, double minutes);

TCR_API int tcr_ticket_owner(tcr_core* core, const char* ticket, tcr_out** owner);
TCR_API int tcr_ticket_owner_refresh(tcr_core* core, const char* ticket, tcr_out** owner);
TCR_API int tcr_active(tcr_core* core, const char* name);
TCR_API int tcr_console(tcr_core* core, tcr_out** lines);

TCR_API int tcr_probe(tcr_core* core, const char* version, tcr_out** json);

typedef struct tcr_relay tcr_relay;

TCR_API tcr_relay* tcr_relay_new(tcr_core* core, const char* version, const char* server_ip, int port,
                                 const char* save_path);
TCR_API tcr_relay* tcr_relay_new_with(tcr_core* core, const char* options_json, const char** error);
TCR_API void tcr_relay_free(tcr_relay* relay);

TCR_API int tcr_relay_datagram(tcr_relay* relay, const uint8_t* data, size_t n, const char* ip, int port,
                               tcr_out** out);

TCR_API int tcr_relay_session_key(tcr_relay* relay, const char* ip, int port, tcr_out** key);

typedef struct tcr_debug_random tcr_debug_random;

TCR_API tcr_debug_random* tcr_debug_random_new(const uint8_t* seed_le, size_t n);
TCR_API void tcr_debug_random_free(tcr_debug_random* r);
TCR_API int tcr_debug_random_getrandbits(tcr_debug_random* r, uint32_t k, uint8_t* out, size_t out_n);
TCR_API int tcr_debug_random_randbytes(tcr_debug_random* r, size_t n, uint8_t* out);
TCR_API double tcr_debug_random_random(tcr_debug_random* r);
TCR_API double tcr_debug_random_uniform(tcr_debug_random* r, double a, double b);
TCR_API int tcr_debug_random_randbelow(tcr_debug_random* r, uint64_t n, uint64_t* result);
TCR_API int tcr_debug_random_randrange(tcr_debug_random* r, int64_t start, int64_t stop, int64_t step,
                                       int64_t* result);
TCR_API int tcr_debug_random_randint(tcr_debug_random* r, int64_t a, int64_t b, int64_t* result);
TCR_API int tcr_debug_random_choices(tcr_debug_random* r, const double* weights, size_t n, size_t* result);

TCR_API int tcr_debug_core_urandom(tcr_core* core, size_t n, uint8_t* out);
TCR_API double tcr_debug_core_time(tcr_core* core);
TCR_API double tcr_debug_core_monotonic(tcr_core* core);
TCR_API double tcr_debug_core_module_random(tcr_core* core);
TCR_API double tcr_debug_core_unseeded_random(tcr_core* core);

typedef struct tcr_debug tcr_debug;

TCR_API tcr_debug* tcr_debug_new(void);
TCR_API void tcr_debug_free(tcr_debug* d);
TCR_API int tcr_debug_call(tcr_debug* d, const char* name, const char* args_json, tcr_out** result);

TCR_API int tcr_debug_v1_21(tcr_core* core, const char* args_json, tcr_out** result);
TCR_API int tcr_debug_peers(tcr_core* core, const char* name, const char* args_json, tcr_out** result);

#ifdef __cplusplus
}
#endif

#endif
