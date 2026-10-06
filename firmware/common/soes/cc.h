// GCC/Clang compiler contract for SOES, shared by native tests and TI ARM Clang.
// No OS headers: only little-endian targets are currently validated by Joshua.
#pragma once
#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Joshua's SOES port currently requires a little-endian target"
#endif
#define EC_LITTLE_ENDIAN
#define CC_PACKED_BEGIN
#define CC_PACKED_END
#define CC_PACKED __attribute__((packed))
#define CC_ALIGNED(n) __attribute__((aligned(n)))
#define CC_ASSERT(x) assert(x)
#ifdef __cplusplus
#define CC_STATIC_ASSERT(x, msg) static_assert(x, msg)
#else
#define CC_STATIC_ASSERT(x, msg) _Static_assert(x, msg)
#endif
#define CC_DEPRECATED __attribute__((deprecated))
#define CC_SWAP16(x) __builtin_bswap16(x)
#define CC_SWAP32(x) __builtin_bswap32(x)
#define htoes(x) ((uint16_t)(x))
#define htoel(x) ((uint32_t)(x))
#define etohs(x) htoes(x)
#define etohl(x) htoel(x)
#define CC_ATOMIC_SET(v, x) __atomic_store_n(&(v), x, __ATOMIC_SEQ_CST)
#define CC_ATOMIC_GET(v) __atomic_load_n(&(v), __ATOMIC_SEQ_CST)
#define CC_ATOMIC_ADD(v, x) __atomic_add_fetch(&(v), x, __ATOMIC_SEQ_CST)
#define CC_ATOMIC_SUB(v, x) __atomic_sub_fetch(&(v), x, __ATOMIC_SEQ_CST)
#define CC_ATOMIC_AND(v, x) __atomic_and_fetch(&(v), x, __ATOMIC_SEQ_CST)
#define CC_ATOMIC_OR(v, x) __atomic_or_fetch(&(v), x, __ATOMIC_SEQ_CST)
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#define DPRINT(...)
