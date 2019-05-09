/*
 *  SSL symboles dynamic loader
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

static void *SSLhandle = NULL;
static void *CRYPThandle = NULL;

#ifndef LINKALL

#if WIN
static char *LIBSSL[] 		= { "ssleay32.dll", NULL };
static char *LIBCRYPTO[] 	= { "libeay32.dll", NULL };
#elif OSX
static char *LIBSSL[] 		= { "libssl.dylib", NULL };
static char *LIBCRYPTO[] 	= { "libcrypto.dylib", NULL };
#else
static char *LIBSSL[] 		= {	"libssl.so",
							"libssl.so.1.1",
							"libssl.so.1.0.2",
							"libssl.so.1.0.1",
							"libssl.so.1.0.0", NULL };
static char *LIBCRYPTO[] 	= {	"libcrypto.so",
							"libcrypto.so.1.1",
							"libcrypto.so.1.0.2",
							"libcrypto.so.1.0.1",
							"libcrypto.so.1.0.0", NULL };
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

#define SYM(fn) dlsym_##fn
#define SYMDECL(fn, ret, n, ...) 			\
	static ret (*dlsym_##fn)(P(n,__VA_ARGS__));		\
	ret fn(P(n,__VA_ARGS__)) {				\
		return (*dlsym_##fn)(V(n,__VA_ARGS__));	\
	}

#define SYMDECLVOID(fn, n, ...) 			\
	static void (*dlsym_##fn)(P(n,__VA_ARGS__));		\
	void fn(P(n,__VA_ARGS__)) {				\
		(*dlsym_##fn)(V(n,__VA_ARGS__));	\
	}

#if 0
#define SYMLOAD(h, fn) {					\
	dlsym_##fn = dlsym(h, #fn);             \
	printf("%s %p\n", #fn, dlsym_##fn);		\
}
#else
#define SYMLOAD(h, fn) dlsym_##fn = dlsym(h, #fn)
#endif

SYMDECL(SSL_read, int, 3, SSL*, s, void*, buf, int, len);
SYMDECL(SSL_write, int, 3, SSL*, s, const void*, buf, int, len);
SYMDECL(SSLv23_client_method, const SSL_METHOD*, 0);
SYMDECL(TLS_client_method, const SSL_METHOD*, 0);
SYMDECL(OpenSSL_version_num, unsigned long, 0);
SYMDECL(SSL_library_init, int, 0);
SYMDECL(SSL_CTX_set_cipher_list, int, 2, SSL_CTX *, ctx, const char*, str);
SYMDECL(SSL_CTX_new, SSL_CTX*, 1, const SSL_METHOD *, meth);
SYMDECL(SSL_CTX_ctrl, long, 4, SSL_CTX *, ctx, int, cmd, long, larg, void*, parg);
SYMDECL(SSL_new, SSL*, 1, SSL_CTX*, s);
SYMDECL(SSL_connect, int, 1, SSL*, s);
SYMDECL(SSL_shutdown, int, 1, SSL*, s);
SYMDECL(SSL_get_fd, int, 1, const SSL*, s);
SYMDECL(SSL_set_fd, int, 2, SSL*, s, int, fd);
SYMDECL(SSL_get_error, int, 2, const SSL*, s, int, ret_code);
SYMDECL(SSL_ctrl, long, 4, SSL*, ssl, int, cmd, long, larg, void*, parg);
SYMDECL(SSL_pending, int, 1, const SSL*, s);

SYMDECLVOID(SSL_free, 1, SSL*, s);
SYMDECLVOID(SSL_CTX_free, 1, SSL_CTX *, ctx);

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

SYMDECLVOID(AES_cbc_encrypt, 6, const unsigned char*, in, unsigned char*, out, size_t, length, const AES_KEY*, key, unsigned char*, ivec, const int, enc);
SYMDECLVOID(RAND_seed, 2, const void*, buf, int, num);
SYMDECLVOID(RSA_free, 1, RSA*, r);
SYMDECLVOID(ERR_clear_error, 0);
SYMDECLVOID(ERR_remove_state, 1, unsigned long, pid);

#if WIN
static void *dlopen(const char *filename, int flag) {
	SetLastError(0);
	return LoadLibraryA((LPCTSTR)filename);
}

static void dlclose(void *handle) {
	FreeLibrary(handle);
}

static void *dlsym(void *handle, const char *symbol) {
	void *a;
	SetLastError(0);
	return (void *)GetProcAddress(handle, symbol);
}
#endif

static void *dlopen_try(char **filenames, int flag) {
	void *handle;
	for (handle = NULL; !handle && *filenames; filenames++) handle = dlopen(*filenames, flag);
	return handle;
}

static int return_true(void) {
	return true;
}

bool load_ssl_symbols(void) {
	CRYPThandle = dlopen_try(LIBCRYPTO, RTLD_NOW);
	SSLhandle = dlopen_try(LIBSSL, RTLD_NOW);

	SYMLOAD(SSLhandle, SSL_CTX_new);
	SYMLOAD(SSLhandle, SSL_CTX_ctrl);
	SYMLOAD(SSLhandle, SSL_CTX_set_cipher_list);
	SYMLOAD(SSLhandle, SSL_CTX_free);
	SYMLOAD(SSLhandle, SSL_ctrl);
	SYMLOAD(SSLhandle, SSL_free);
	SYMLOAD(SSLhandle, SSL_new);
	SYMLOAD(SSLhandle, SSL_connect);
	SYMLOAD(SSLhandle, SSL_get_fd);
	SYMLOAD(SSLhandle, SSL_set_fd);
	SYMLOAD(SSLhandle, SSL_get_error);
	SYMLOAD(SSLhandle, SSL_shutdown);
	SYMLOAD(SSLhandle, SSL_read);
	SYMLOAD(SSLhandle, SSL_write);
	SYMLOAD(SSLhandle, SSL_pending);
	SYMLOAD(SSLhandle, SSLv23_client_method);
	SYMLOAD(SSLhandle, TLS_client_method);
	SYMLOAD(SSLhandle, SSL_library_init);
	SYMLOAD(SSLhandle, OpenSSL_version_num);

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

	// managed deprecated functions
	if (!SYM(SSLv23_client_method)) SYM(SSLv23_client_method) = SYM(TLS_client_method);
	if (!SYM(SSL_library_init)) SYM(SSL_library_init) = &return_true;

	return true;
}

#else
bool load_ssl_symbols(void) {
	return true;
}
#endif

void free_ssl_symbols(void) {
	if (SSLhandle) dlclose(SSLhandle);
	if (CRYPThandle) dlclose(CRYPThandle);
}
