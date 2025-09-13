/* Copyright (c) 2013-2017 the Civetweb developers
 * Copyright (c) 2004-2013 Sergey Lyubka
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/* This file contains definition of all functions that are used from
 * OpenSSL, if it is loaded dynamically.
 * This file is included in civetweb.c, and does not need to be compiled
 * otherwise.
 */

#if defined(OPENSSL_API_1_0)
/* OpenSSL 1.0.x */

/* This is a replacement for the required functions from the OpenSSL
 * library.
 * The code is based on the functions of the same name in OpenSSL 1.0.2,
 * in particular crypto/crypto.h and ssl/ssl.h.
 * It is used if CivetWeb is built with -DNO_SSL_DL.
 */

struct ssl_func {
	const char *name; /* SSL function name */
	void (*ptr)(void);  /* Function pointer */
};

#define SSL_free (*(void (*)(SSL *))ssl_sw[0].ptr)
#define SSL_accept (*(int (*)(SSL *))ssl_sw[1].ptr)
#define SSL_connect (*(int (*)(SSL *))ssl_sw[2].ptr)
#define SSL_read (*(int (*)(SSL *, void *, int))ssl_sw[3].ptr)
#define SSL_write (*(int (*)(SSL *, const void *, int))ssl_sw[4].ptr)
#define SSL_get_error (*(int (*)(SSL *, int))ssl_sw[5].ptr)
#define SSL_set_fd (*(int (*)(SSL *, int))ssl_sw[6].ptr)
#define SSL_new (*(SSL * (*)(SSL_CTX *))ssl_sw[7].ptr)
#define SSL_CTX_new (*(SSL_CTX * (*)(const SSL_METHOD *))ssl_sw[8].ptr)
#define SSLv23_server_method (*(SSL_METHOD * (*)(void))ssl_sw[9].ptr)
#define SSL_library_init (*(int (*)(void))ssl_sw[10].ptr)
#define SSL_CTX_use_PrivateKey_file                                            \
	(*(int (*)(SSL_CTX *, const char *, int))ssl_sw[11].ptr)
#define SSL_CTX_use_certificate_file                                           \
	(*(int (*)(SSL_CTX *, const char *, int))ssl_sw[12].ptr)
#define SSL_CTX_set_default_passwd_cb                                          \
	(*(void (*)(SSL_CTX *, mg_callback_t))ssl_sw[13].ptr)
#define SSL_CTX_free (*(void (*)(SSL_CTX *))ssl_sw[14].ptr)
#define SSL_load_error_strings (*(void (*)(void))ssl_sw[15].ptr)
#define SSL_CTX_use_certificate_chain_file                                     \
	(*(int (*)(SSL_CTX *, const char *))ssl_sw[16].ptr)
#define SSLv23_client_method (*(SSL_METHOD * (*)(void))ssl_sw[17].ptr)
#define SSL_pending (*(int (*)(SSL *))ssl_sw[18].ptr)
#define SSL_CTX_set_verify                                                     \
	(*(void (*)(SSL_CTX *, int, int (*)(int, X509_STORE_CTX *)))ssl_sw[19].ptr)
#define SSL_shutdown (*(int (*)(SSL *))ssl_sw[20].ptr)
#define SSL_get_peer_certificate (*(X509 * (*)(SSL *))ssl_sw[21].ptr)
#define SSL_get_version (*(const char *(*)(void))ssl_sw[22].ptr)
#define SSL_CTX_load_verify_locations                                          \
	(*(int (*)(SSL_CTX *, const char *, const char *))ssl_sw[23].ptr)
#define SSL_CTX_set_default_verify_paths (*(int (*)(SSL_CTX *))ssl_sw[24].ptr)
#define SSL_CTX_set_verify_depth (*(void (*)(SSL_CTX *, int))ssl_sw[25].ptr)
#define SSL_get_app_data (*(char *(*)(SSL *))ssl_sw[26].ptr)
#define SSL_set_app_data (*(int (*)(SSL *, char *))ssl_sw[27].ptr)
#define SSL_CTX_get_app_data (*(char *(*)(SSL_CTX *))ssl_sw[28].ptr)
#define SSL_CTX_set_app_data (*(int (*)(SSL_CTX *, char *))ssl_sw[29].ptr)
#define SSL_CTX_set_cipher_list                                                \
	(*(int (*)(SSL_CTX *, const char *))ssl_sw[30].ptr)
#define SSL_CTX_ctrl (*(long (*)(SSL_CTX *, int, long, void *))ssl_sw[31].ptr)

#define CRYPTO_num_locks (*(int (*)(void))crypto_sw[0].ptr)
#define CRYPTO_set_locking_callback                                            \
	(*(void (*)(void (*)(int, int, const char *, int)))crypto_sw[1].ptr)
#define CRYPTO_set_id_callback                                                 \
	(*(void (*)(unsigned long (*)(void)))crypto_sw[2].ptr)
#define ERR_get_error (*(unsigned long (*)(void))crypto_sw[3].ptr)
#define ERR_error_string (*(char *(*)(unsigned long, char *))crypto_sw[4].ptr)
#define ERR_remove_thread_state (*(void (*)(void *))crypto_sw[5].ptr)
#define ERR_free_strings (*(void (*)(void))crypto_sw[6].ptr)
#define ENGINE_cleanup (*(void (*)(void))crypto_sw[7].ptr)
#define CONF_modules_unload (*(void (*)(int))crypto_sw[8].ptr)
#define CRYPTO_cleanup_all_ex_data (*(void (*)(void))crypto_sw[9].ptr)
#define EVP_cleanup (*(void (*)(void))crypto_sw[10].ptr)

#define SSL_CTX_set_options(ctx, op)                                           \
	SSL_CTX_ctrl((ctx), SSL_CTRL_OPTIONS, (op), NULL)
#define SSL_CTX_clear_options(ctx, op)                                         \
	SSL_CTX_ctrl((ctx), SSL_CTRL_CLEAR_OPTIONS, (op), NULL)
#define SSL_CTX_set_ecdh_auto(ctx, onoff)                                      \
	SSL_CTX_ctrl(ctx, SSL_CTRL_SET_ECDH_AUTO, onoff, NULL)

/* set_ssl_option() function updates this array.
 * It loads required functions dynamically. */
static struct ssl_func ssl_sw[] = {{"SSL_free", NULL},
                                   {"SSL_accept", NULL},
                                   {"SSL_connect", NULL},
                                   {"SSL_read", NULL},
                                   {"SSL_write", NULL},
                                   {"SSL_get_error", NULL},
                                   {"SSL_set_fd", NULL},
                                   {"SSL_new", NULL},
                                   {"SSL_CTX_new", NULL},
                                   {"SSLv23_server_method", NULL},
                                   {"SSL_library_init", NULL},
                                   {"SSL_CTX_use_PrivateKey_file", NULL},
                                   {"SSL_CTX_use_certificate_file", NULL},
                                   {"SSL_CTX_set_default_passwd_cb", NULL},
                                   {"SSL_CTX_free", NULL},
                                   {"SSL_load_error_strings", NULL},
                                   {"SSL_CTX_use_certificate_chain_file", NULL},
                                   {"SSLv23_client_method", NULL},
                                   {"SSL_pending", NULL},
                                   {"SSL_CTX_set_verify", NULL},
                                   {"SSL_shutdown", NULL},
                                   {"SSL_get_peer_certificate", NULL},
                                   {"SSL_get_version", NULL},
                                   {"SSL_CTX_load_verify_locations", NULL},
                                   {"SSL_CTX_set_default_verify_paths", NULL},
                                   {"SSL_CTX_set_verify_depth", NULL},
                                   {"SSL_get_app_data", NULL},
                                   {"SSL_set_app_data", NULL},
                                   {"SSL_CTX_get_app_data", NULL},
                                   {"SSL_CTX_set_app_data", NULL},
                                   {"SSL_CTX_set_cipher_list", NULL},
                                   {"SSL_CTX_ctrl", NULL},
                                   {NULL, NULL}};


static struct ssl_func crypto_sw[] = {
    {"CRYPTO_num_locks", NULL},
    {"CRYPTO_set_locking_callback", NULL},
    {"CRYPTO_set_id_callback", NULL},
    {"ERR_get_error", NULL},
    {"ERR_error_string", NULL},
    {"ERR_remove_thread_state", NULL},
    {"ERR_free_strings", NULL},
    {"ENGINE_cleanup", NULL},
    {"CONF_modules_unload", NULL},
    {"CRYPTO_cleanup_all_ex_data", NULL},
    {"EVP_cleanup", NULL},
    {NULL, NULL}};

#elif defined(OPENSSL_API_1_1) || defined(OPENSSL_API_3_0)
/* OpenSSL 1.1.x */

/* This is a replacement for the required functions from the OpenSSL
 * library.
 * The code is based on the functions of the same name in OpenSSL 1.0.2,
 * in particular crypto/crypto.h and ssl/ssl.h.
 * It is used if CivetWeb is built with -DNO_SSL_DL.
 */

struct ssl_func {
	const char *name; /* SSL function name */
	void (*ptr)(void);  /* Function pointer */
};

#define SSL_free (*(void (*)(SSL *))ssl_sw[0].ptr)
#define SSL_accept (*(int (*)(SSL *))ssl_sw[1].ptr)
#define SSL_connect (*(int (*)(SSL *))ssl_sw[2].ptr)
#define SSL_read (*(int (*)(SSL *, void *, int))ssl_sw[3].ptr)
#define SSL_write (*(int (*)(SSL *, const void *, int))ssl_sw[4].ptr)
#define SSL_get_error (*(int (*)(SSL *, int))ssl_sw[5].ptr)
#define SSL_set_fd (*(int (*)(SSL *, int))ssl_sw[6].ptr)
#define SSL_new (*(SSL * (*)(SSL_CTX *))ssl_sw[7].ptr)
#define SSL_CTX_new (*(SSL_CTX * (*)(const SSL_METHOD *))ssl_sw[8].ptr)
#define TLS_server_method (*(SSL_METHOD * (*)(void))ssl_sw[9].ptr)
#define OPENSSL_init_ssl (*(int (*)(uint64_t, const void *))ssl_sw[10].ptr)
#define SSL_CTX_use_PrivateKey_file                                            \
	(*(int (*)(SSL_CTX *, const char *, int))ssl_sw[11].ptr)
#define SSL_CTX_use_certificate_file                                           \
	(*(int (*)(SSL_CTX *, const char *, int))ssl_sw[12].ptr)
#define SSL_CTX_set_default_passwd_cb                                          \
	(*(void (*)(SSL_CTX *, mg_callback_t))ssl_sw[13].ptr)
#define SSL_CTX_free (*(void (*)(SSL_CTX *))ssl_sw[14].ptr)
#define OPENSSL_init_crypto (*(int (*)(uint64_t, const void *))ssl_sw[15].ptr)
#define SSL_CTX_use_certificate_chain_file                                     \
	(*(int (*)(SSL_CTX *, const char *))ssl_sw[16].ptr)
#define TLS_client_method (*(SSL_METHOD * (*)(void))ssl_sw[17].ptr)
#define SSL_pending (*(int (*)(SSL *))ssl_sw[18].ptr)
#define SSL_CTX_set_verify                                                     \
	(*(void (*)(SSL_CTX *, int, int (*)(int, X509_STORE_CTX *)))ssl_sw[19].ptr)
#define SSL_shutdown (*(int (*)(SSL *))ssl_sw[20].ptr)
#define SSL_get_peer_certificate (*(X509 * (*)(SSL *))ssl_sw[21].ptr)
#define SSL_get_version (*(const char *(*)(void))ssl_sw[22].ptr)
#define SSL_CTX_load_verify_locations                                          \
	(*(int (*)(SSL_CTX *, const char *, const char *))ssl_sw[23].ptr)
#define SSL_CTX_set_default_verify_paths (*(int (*)(SSL_CTX *))ssl_sw[24].ptr)
#define SSL_CTX_set_verify_depth (*(void (*)(SSL_CTX *, int))ssl_sw[25].ptr)
#define SSL_get_app_data (*(void *(*)(const SSL *))ssl_sw[26].ptr)
#define SSL_set_app_data (*(int (*)(SSL *, void *))ssl_sw[27].ptr)
#define SSL_CTX_get_app_data (*(void *(*)(const SSL_CTX *))ssl_sw[28].ptr)
#define SSL_CTX_set_app_data (*(int (*)(SSL_CTX *, void *))ssl_sw[29].ptr)
#define SSL_CTX_set_cipher_list                                                \
	(*(int (*)(SSL_CTX *, const char *))ssl_sw[30].ptr)
#define SSL_CTX_ctrl (*(long (*)(SSL_CTX *, int, long, void *))ssl_sw[31].ptr)

#define ERR_get_error (*(unsigned long (*)(void))crypto_sw[0].ptr)
#define ERR_error_string (*(char *(*)(unsigned long, char *))crypto_sw[1].ptr)
#define CONF_modules_unload (*(void (*)(int))crypto_sw[2].ptr)

#define SSL_CTX_set_options(ctx, op)                                           \
	SSL_CTX_ctrl((ctx), SSL_CTRL_OPTIONS, (op), NULL)
#define SSL_CTX_clear_options(ctx, op)                                         \
	SSL_CTX_ctrl((ctx), SSL_CTRL_CLEAR_OPTIONS, (op), NULL)
#define SSL_CTX_set_ecdh_auto(ctx, onoff)                                      \
	SSL_CTX_ctrl(ctx, SSL_CTRL_SET_ECDH_AUTO, onoff, NULL)

/* set_ssl_option() function updates this array.
 * It loads required functions dynamically. */
static struct ssl_func ssl_sw[] = {{"SSL_free", NULL},
                                   {"SSL_accept", NULL},
                                   {"SSL_connect", NULL},
                                   {"SSL_read", NULL},
                                   {"SSL_write", NULL},
                                   {"SSL_get_error", NULL},
                                   {"SSL_set_fd", NULL},
                                   {"SSL_new", NULL},
                                   {"SSL_CTX_new", NULL},
                                   {"TLS_server_method", NULL},
                                   {"OPENSSL_init_ssl", NULL},
                                   {"SSL_CTX_use_PrivateKey_file", NULL},
                                   {"SSL_CTX_use_certificate_file", NULL},
                                   {"SSL_CTX_set_default_passwd_cb", NULL},
                                   {"SSL_CTX_free", NULL},
                                   {"OPENSSL_init_crypto", NULL},
                                   {"SSL_CTX_use_certificate_chain_file", NULL},
                                   {"TLS_client_method", NULL},
                                   {"SSL_pending", NULL},
                                   {"SSL_CTX_set_verify", NULL},
                                   {"SSL_shutdown", NULL},
                                   {"SSL_get_peer_certificate", NULL},
                                   {"SSL_get_version", NULL},
                                   {"SSL_CTX_load_verify_locations", NULL},
                                   {"SSL_CTX_set_default_verify_paths", NULL},
                                   {"SSL_CTX_set_verify_depth", NULL},
                                   {"SSL_get_app_data", NULL},
                                   {"SSL_set_app_data", NULL},
                                   {"SSL_CTX_get_app_data", NULL},
                                   {"SSL_CTX_set_app_data", NULL},
                                   {"SSL_CTX_set_cipher_list", NULL},
                                   {"SSL_CTX_ctrl", NULL},
                                   {NULL, NULL}};


static struct ssl_func crypto_sw[] = {{"ERR_get_error", NULL},
                                      {"ERR_error_string", NULL},
                                      {"CONF_modules_unload", NULL},
                                      {NULL, NULL}};

#endif