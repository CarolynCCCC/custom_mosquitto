#ifndef CONFIG_H
#define CONFIG_H
/* ============================================================
 * Platform options
 * ============================================================ */

#ifdef __APPLE__
#  define __DARWIN_C_SOURCE
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__SYMBIAN32__)
#  define HAVE_NETINET_IN_H
#elif defined(__QNX__)
#  define _XOPEN_SOURCE 600
#  define __BSD_VISIBLE 1
#  define HAVE_NETINET_IN_H
#elif defined(_AIX)
#  define HAVE_NETINET_IN_H
#else
#  define _XOPEN_SOURCE 700
#  define _DEFAULT_SOURCE 1
#  define _POSIX_C_SOURCE 200809L
#endif


#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#define OPENSSL_LOAD_CONF

/* ============================================================
 * Compatibility defines
 * ============================================================ */
#if defined(_MSC_VER) && _MSC_VER < 1900
#  define snprintf sprintf_s
#  define EPROTO ECONNABORTED
#  ifndef ECONNABORTED
#    define ECONNABORTED WSAECONNABORTED
#  endif
#  ifndef ENOTCONN
#    define ENOTCONN WSAENOTCONN
#  endif
#  ifndef ECONNREFUSED
#    define ECONNREFUSED WSAECONNREFUSED
#  endif
#endif

#ifdef WIN32
#  define strcasecmp _stricmp
#  define strncasecmp _strnicmp
#  define strtok_r strtok_s
#  define strerror_r(e, b, l) strerror_s(b, l, e)

#  ifdef _MSC_VER
#    include <basetsd.h>
typedef SSIZE_T ssize_t;
#  endif
#endif


#define uthash_malloc(sz) mosquitto_malloc(sz)
#define uthash_free(ptr,sz) mosquitto_free(ptr)


#ifdef WITH_TLS
#  include <openssl/opensslconf.h>
#  if defined(WITH_TLS_PSK) && !defined(OPENSSL_NO_PSK)
#    define FINAL_WITH_TLS_PSK
#  endif
#endif


#ifdef __COVERITY__
#  include <stdint.h>
/* These are "wrong", but we don't use them so it doesn't matter */
#  define _Float32 uint32_t
#  define _Float32x uint32_t
#  define _Float64 uint64_t
#  define _Float64x uint64_t
#  define _Float128 uint64_t
#endif

#define UNUSED(A) (void)(A)

/* Android Bionic libpthread implementation doesn't have pthread_cancel */
#if !defined(ANDROID) && !defined(WIN32)
#  define HAVE_PTHREAD_CANCEL
#endif

#define WS_IS_LWS 1
#define WS_IS_BUILTIN 2

#ifdef WITH_BROKER
#  ifdef __GNUC__
#    define BROKER_EXPORT __attribute__((__used__))
#  else
#    define BROKER_EXPORT
#  endif
#else
#  define BROKER_EXPORT
#endif

#define TOPIC_HIERARCHY_LIMIT 200

#endif
