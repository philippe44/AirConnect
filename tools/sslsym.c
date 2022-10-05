/*
 *  SSL symbols dynamic loader
 *
 *  (c) Philippe, philippe_44@outlook.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <assert.h>
#include "platform.h"
#include "sslsym.h"

#if !WIN
#include <dlfcn.h>
#endif

#include "openssl/ssl.h"
#include "openssl/err.h"
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/engine.h>
#include <openssl/aes.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

#ifndef LINKALL
static void *SSLhandle = NULL;
static void *CRYPThandle = NULL;
#endif

#define P0() void
#define P1(t1, p1) t1 p1
#define P2(t1, p1, t2, p2) t1 p1, t2 p2
#define P3(t1, p1, t2, p2, t3, p3) t1 p1, t2 p2, t3 p3
#define P4(t1, p1, t2, p2, t3, p3, t4, p4) t1 p1, t2 p2, t3 p3, t4 p4
#define P5(t1, p1, t2, p2, t3, p3, t4, p4, t5, p5) t1 p1, t2 p2, t3 p3, t4 p4, t5 p5
#define P6(t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6) t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6
#define V0()
#define V1(t1, p1) p1
#define V2(t1, p1, t2, p2) p1, p2
#define V3(t1, p1, t2, p2, t3, p3) p1, p2, p3
#define V4(t1, p1, t2, p2, t3, p3, t4, p4) p1, p2, p3, p4
#define V5(t1, p1, t2, p2, t3, p3, t4, p4, t5, p5) p1, p2, p3, p4, p5
#define V6(t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6) p1, p2, p3, p4, p5, p6

#define P(n, ...) P##n(__VA_ARGS__)
#define V(n, ...) V##n(__VA_ARGS__)

#define STR(x) #x

#ifndef LINKALL

#define SYM(fn) dlsym_##fn
#define SHIM(fn) shim_##fn

#define SHIMNULL(fn, ret, n, ...) 	  	   	\
static ret (*SHIM(fn))(P(n,__VA_ARGS__))

#define SYMDECL(fn, ret, n, ...) 			\
static ret (*SYM(fn))(P(n,__VA_ARGS__));	\
ret fn(P(n,__VA_ARGS__)) {					\
	assert(SYM(fn));						\
	return (*SYM(fn))(V(n,__VA_ARGS__));	\
}

#define SYMDECLV(fn, ret, n, ...) 			\
static ret (*SYM(fn))(P(n,__VA_ARGS__));	\
ret fn(P(n,__VA_ARGS__)) {					\
	assert(SYM(fn));						\
	(*SYM(fn))(V(n,__VA_ARGS__));			\
}

#define SHIMDECL(fn, ret, n, ...) 	  	   		\
static ret (*SYM(fn))(P(n,__VA_ARGS__));		\
ret fn(P(n,__VA_ARGS__)) {						\
	if (SYM(fn)) 								\
		return (*SYM(fn))(V(n,__VA_ARGS__));	\
	else        								\
		return SHIM(fn)(V(n,__VA_ARGS__));		\
}

#define SHIMDECLV(fn, ret, n, ...) 	  	   	\
static ret (*SYM(fn))(P(n,__VA_ARGS__));	\
ret fn(P(n,__VA_ARGS__)) {					\
	if (SYM(fn)) 							\
		(*SYM(fn))(V(n,__VA_ARGS__));		\
	else        							\
		SHIM(fn)(V(n,__VA_ARGS__));			\
}

#define SYMLOAD(h, fn) SYM(fn) = dlsym(h, STR(fn));
#define SYMLOADA(h, fn, name) SYM(fn) = dlsym(h, name);
#define SHIMSET(fn) if (!SYM(fn)) SYM(fn) = shim_##fn

/*
 MNNFFPPS: major minor fix patch status
 0x101ffpps = 1.1. fix->ff patch->pp status->s
*/

#if WIN
static char *LIBSSL[] = {
			"libssl-1_1.dll",
			"libssl.dll",
			"ssleay32.dll", NULL };
static char *LIBCRYPTO[] = {
			"libcrypto-1_1.dll",
			"libssl.dll",
			"libeay32.dll", NULL };
#elif OSX
static char *LIBSSL[] = {
			"libssl.dylib", NULL };
static char *LIBCRYPTO[] 	= {
			"libcrypto.dylib", NULL };
#else
static char *LIBSSL[] 		= {
			"libssl.so",
			"libssl.so.1.1",
			"libssl.so.1.0.2",
			"libssl.so.1.0.1",
			"libssl.so.1.0.0", NULL };
static char *LIBCRYPTO[] 	= {
			"libcrypto.so",
			"libcrypto.so.1.1",
			"libcrypto.so.1.0.2",
			"libcrypto.so.1.0.1",
			"libcrypto.so.1.0.0", NULL };
#endif

#if WIN
static void* dlopen(const char* filename, int flag) {
	SetLastError(0);
	return LoadLibraryA(filename);
}

static void dlclose(void* handle) {
	FreeLibrary(handle);
}

static void* dlsym(void* handle, const char* symbol) {
	SetLastError(0);
	return (void*)GetProcAddress(handle, symbol);
}
#endif

static void* dlopen_try(char** filenames, int flag) {
	void* handle;
	for (handle = NULL; !handle && *filenames; filenames++) handle = dlopen(*filenames, flag);
	return handle;
}

SYMDECL(_SSL_library_init, int, 0);
SYMDECL(SSL_CTX_ctrl, long, 4, SSL_CTX*, ctx, int, cmd, long, larg, void*, parg);
SYMDECL(_SSLv23_client_method, const SSL_METHOD*, 0);
SYMDECLV(ERR_remove_state, void, 1, unsigned long, pid);

static unsigned long shim_OPENSSL_init_ssl(uint64_t opts, const OPENSSL_INIT_SETTINGS* settings) {
	if (SYM(_SSL_library_init)) return SYM(_SSL_library_init());
	else return 1;
}

static unsigned long shim_SSL_CTX_set_options(SSL_CTX* ctx, unsigned long op) {
	assert(SYM(SSL_CTX_ctrl));
	return SYM(SSL_CTX_ctrl)(ctx, 32, op, NULL);
}

static const SSL_METHOD* shim_TLS_client_method(void) {
	assert(SYM(_SSLv23_client_method));
	return SYM(_SSLv23_client_method)();
}

static void shim_ERR_remove_thread_state(void* tid) {
	assert(SYM(ERR_remove_state));
	SYM(ERR_remove_state)(0);
}

SHIMDECL(OPENSSL_init_ssl, int, 2, uint64_t, opts, const OPENSSL_INIT_SETTINGS*, settings);
SHIMDECL(SSL_CTX_set_options, unsigned long, 2, SSL_CTX*, ctx, unsigned long, op);
SHIMDECL(TLS_client_method, const SSL_METHOD*, 0);
SHIMDECLV(ERR_remove_thread_state, void, 1, void*, tid);

SYMDECL(SSL_read, int, 3, SSL*, s, void*, buf, int, len);
SYMDECL(SSL_write, int, 3, SSL*, s, const void*, buf, int, len);
SYMDECL(OpenSSL_version_num, unsigned long, 0);
SYMDECL(SSL_CTX_set_cipher_list, int, 2, SSL_CTX *, ctx, const char*, str);
SYMDECL(SSL_CTX_new, SSL_CTX*, 1, const SSL_METHOD *, meth);
SYMDECL(SSL_new, SSL*, 1, SSL_CTX*, s);
SYMDECL(SSL_connect, int, 1, SSL*, s);
SYMDECL(SSL_shutdown, int, 1, SSL*, s);
SYMDECL(SSL_clear, int, 1, SSL*, s);
SYMDECL(SSL_get_fd, int, 1, const SSL*, s);
SYMDECL(SSL_set_fd, int, 2, SSL*, s, int, fd);
SYMDECL(SSL_get_error, int, 2, const SSL*, s, int, ret_code);
SYMDECL(SSL_ctrl, long, 4, SSL*, ssl, int, cmd, long, larg, void*, parg);
SYMDECL(SSL_pending, int, 1, const SSL*, s);

SYMDECLV(SSL_free, void, 1, SSL*, s);
SYMDECLV(SSL_CTX_free, void, 1, SSL_CTX *, ctx);

SYMDECL(ERR_get_error, unsigned long, 0);
SYMDECL(SHA512_Init, int, 1, SHA512_CTX*, c);
SYMDECL(SHA512_Update, int, 3, SHA512_CTX*, c, const void*, data, size_t, len);
SYMDECL(SHA512_Final, int, 2, unsigned char*, md, SHA512_CTX*, c);
SYMDECL(RAND_bytes, int, 2, unsigned char*, buf, int, num);
SYMDECL(RSA_new, RSA*, 0);
SYMDECL(RSA_size, int, 1, const RSA*, rsa);
SYMDECL(RSA_public_encrypt, int, 5, int, flen, const unsigned char*, from, unsigned char*, to, RSA*, rsa, int, padding);
SYMDECL(RSA_public_decrypt, int, 5, int, flen, const unsigned char*, from, unsigned char*, to, RSA*, rsa, int, padding);
SYMDECL(RSA_private_encrypt, int, 5, int, flen, const unsigned char*, from, unsigned char*, to, RSA*, rsa, int, padding);
SYMDECL(RSA_private_decrypt, int, 5, int, flen, const unsigned char*, from, unsigned char*, to, RSA*, rsa, int, padding);
SYMDECL(BN_bin2bn, BIGNUM*, 3, const unsigned char*, s, int, len, BIGNUM*,ret);
SYMDECL(AES_set_decrypt_key, int, 3, const unsigned char*, userKey, const int, bits, AES_KEY*, key);
SYMDECL(BIO_new_mem_buf, BIO*, 2, const void*, buf, int, len);
SYMDECL(BIO_free, int, 1, BIO*, a);
SYMDECL(PEM_read_bio_RSAPrivateKey, RSA *, 4, BIO*, bp, RSA**, x, pem_password_cb*, cb, void*, u);

SYMDECLV(AES_cbc_encrypt, void, 6, const unsigned char*, in, unsigned char*, out, size_t, length, const AES_KEY*, key, unsigned char*, ivec, const int, enc);
SYMDECLV(RAND_seed, void, 2, const void*, buf, int, num);
SYMDECLV(RSA_free, void, 1, RSA*, r);
SYMDECLV(ERR_clear_error, void, 0);

SYMDECL(RSA_set0_key, int, 4, RSA*, r, BIGNUM*, n, BIGNUM*, e, BIGNUM*, d);
SYMDECL(EVP_MD_CTX_new, EVP_MD_CTX*, 0);
SYMDECLV(EVP_MD_CTX_free, void, 1, EVP_MD_CTX*, ctx);
SYMDECL(EVP_DigestSign, int, 5, EVP_MD_CTX*, ctx, unsigned char*, sigret, size_t*, siglen, const unsigned char*, tbs, size_t, tbslen);
SYMDECL(EVP_DigestSignInit, int, 5, EVP_MD_CTX*, ctx, EVP_PKEY_CTX**, pctx, const EVP_MD*, type, ENGINE*, e, EVP_PKEY*, pkey);
SYMDECLV(EVP_PKEY_free, void, 1, EVP_PKEY*, ctx);
SYMDECL(EVP_PKEY_CTX_new, EVP_PKEY_CTX*, 2, EVP_PKEY*, pkey, ENGINE*, e);
SYMDECLV(EVP_PKEY_CTX_free, void, 1, EVP_PKEY_CTX*, ctx);
SYMDECL(EVP_PKEY_new_raw_private_key, EVP_PKEY*, 4, int, type, ENGINE*, e, const unsigned char*, priv, size_t, len);
SYMDECL(EVP_PKEY_new_raw_public_key, EVP_PKEY*, 4, int, type, ENGINE*, e, const unsigned char*, pub, size_t, len);
SYMDECL(EVP_PKEY_get_raw_private_key, int, 3, const EVP_PKEY*, pkey, unsigned char*, priv, size_t*, len);
SYMDECL(EVP_PKEY_get_raw_public_key, int, 3, const EVP_PKEY*, pkey, unsigned char*, pub, size_t*, len);
SYMDECL(EVP_PKEY_derive_init, int, 1, EVP_PKEY_CTX *,ctx);
SYMDECL(EVP_PKEY_derive_set_peer, int, 2, EVP_PKEY_CTX*, ctx, EVP_PKEY*, peer);
SYMDECL(EVP_PKEY_derive, int, 3, EVP_PKEY_CTX*, ctx, unsigned char*, key, size_t*, keylen);

bool load_ssl_symbols(void) {
	CRYPThandle = dlopen_try(LIBCRYPTO, RTLD_NOW);
	SSLhandle = dlopen_try(LIBSSL, RTLD_NOW);

	if (!SSLhandle || !CRYPThandle) {
		free_ssl_symbols();
		return false;
    }

	SYMLOAD(SSLhandle, SSL_CTX_new);
	SYMLOAD(SSLhandle, SSL_CTX_ctrl);
	SYMLOAD(SSLhandle, SSL_CTX_set_cipher_list);
	SYMLOAD(SSLhandle, SSL_CTX_free);
	SYMLOAD(SSLhandle, SSL_CTX_set_options);
	SYMLOAD(SSLhandle, SSL_ctrl);
	SYMLOAD(SSLhandle, SSL_free);
	SYMLOAD(SSLhandle, SSL_new);
	SYMLOAD(SSLhandle, SSL_connect);
	SYMLOAD(SSLhandle, SSL_get_fd);
	SYMLOAD(SSLhandle, SSL_set_fd);
	SYMLOAD(SSLhandle, SSL_get_error);
	SYMLOAD(SSLhandle, SSL_shutdown);
	SYMLOAD(SSLhandle, SSL_clear);
	SYMLOAD(SSLhandle, SSL_read);
	SYMLOAD(SSLhandle, SSL_write);
	SYMLOAD(SSLhandle, SSL_pending);
	SYMLOAD(SSLhandle, TLS_client_method);
	SYMLOAD(SSLhandle, OpenSSL_version_num);
	SYMLOAD(SSLhandle, OPENSSL_init_ssl);
	
	SYMLOADA(SSLhandle, _SSL_library_init, "SSL_library_init");
	SYMLOADA(SSLhandle, _SSLv23_client_method, "SSLv23_client_method");

	SYMLOAD(CRYPThandle, RAND_seed);
	SYMLOAD(CRYPThandle, RAND_bytes);
	SYMLOAD(CRYPThandle, SHA512_Init);
	SYMLOAD(CRYPThandle, SHA512_Update);
	SYMLOAD(CRYPThandle, SHA512_Final);
	SYMLOAD(CRYPThandle, ERR_clear_error);
	SYMLOAD(CRYPThandle, ERR_get_error);
	SYMLOAD(CRYPThandle, ERR_remove_state);
	SYMLOAD(CRYPThandle, RSA_new);
	SYMLOAD(CRYPThandle, RSA_size);
	SYMLOAD(CRYPThandle, RSA_public_encrypt);
	SYMLOAD(CRYPThandle, RSA_private_encrypt);
	SYMLOAD(CRYPThandle, RSA_public_decrypt);
	SYMLOAD(CRYPThandle, RSA_private_decrypt);
	SYMLOAD(CRYPThandle, RSA_free);
	SYMLOAD(CRYPThandle, BN_bin2bn);
	SYMLOAD(CRYPThandle, AES_set_decrypt_key);
	SYMLOAD(CRYPThandle, AES_cbc_encrypt);
	SYMLOAD(CRYPThandle, BIO_new_mem_buf);
	SYMLOAD(CRYPThandle, BIO_free);
	SYMLOAD(CRYPThandle, PEM_read_bio_RSAPrivateKey);

	SYMLOAD(CRYPThandle, RSA_set0_key);
	SYMLOAD(CRYPThandle, EVP_MD_CTX_new);
	SYMLOAD(CRYPThandle, EVP_MD_CTX_free);
	SYMLOAD(CRYPThandle, EVP_DigestSign);
	SYMLOAD(CRYPThandle, EVP_DigestSignInit);
	SYMLOAD(CRYPThandle, EVP_PKEY_free);
	SYMLOAD(CRYPThandle, EVP_PKEY_CTX_new);
	SYMLOAD(CRYPThandle, EVP_PKEY_CTX_free);
	SYMLOAD(CRYPThandle, EVP_PKEY_new_raw_private_key);
	SYMLOAD(CRYPThandle, EVP_PKEY_new_raw_public_key);
	SYMLOAD(CRYPThandle, EVP_PKEY_get_raw_private_key);
	SYMLOAD(CRYPThandle, EVP_PKEY_get_raw_public_key);
	SYMLOAD(CRYPThandle, EVP_PKEY_derive_init);
	SYMLOAD(CRYPThandle, EVP_PKEY_derive_set_peer);
	SYMLOAD(CRYPThandle, EVP_PKEY_derive);

	OPENSSL_init_ssl(0, NULL);

	return true;
}

void free_ssl_symbols(void) {
	if (SSLhandle) dlclose(SSLhandle);
	if (CRYPThandle) dlclose(CRYPThandle);
}

#else
bool load_ssl_symbols(void) {
	return true;
}

void free_ssl_symbols(void) {
}
#endif
