/* ELXAR-Two shim: stands in for the STM32 HAL header. The sound engine only needs the integer types and a few
 * Cortex-M helper operations, written here as plain portable C. No hardware access. */
#ifndef ELXAR_SHIM_STM32F7XX_HAL_H
#define ELXAR_SHIM_STM32F7XX_HAL_H
#include <stdint.h>
#include <stddef.h>

static inline int32_t elxar_ssat(int32_t v, int bits) {
    const int32_t mx = (int32_t)((1u << (bits - 1)) - 1u), mn = -mx - 1;
    return v > mx ? mx : (v < mn ? mn : v);
}
static inline uint32_t elxar_usat(int32_t v, int bits) {
    const int32_t mx = bits >= 31 ? INT32_MAX : (int32_t)((1u << bits) - 1u);
    return (uint32_t)(v > mx ? mx : (v < 0 ? 0 : v));
}
#define __SSAT(ARG1, ARG2) elxar_ssat((int32_t)(ARG1), (ARG2))
#define __USAT(ARG1, ARG2) elxar_usat((int32_t)(ARG1), (ARG2))
static inline int32_t elxar_sat16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
static inline uint32_t __QADD16(uint32_t a, uint32_t b) {
    const int32_t lo = elxar_sat16((int32_t)(int16_t)a + (int16_t)b);
    const int32_t hi = elxar_sat16((int32_t)(int16_t)(a >> 16) + (int16_t)(b >> 16));
    return ((uint32_t)(uint16_t)hi << 16) | (uint16_t)lo;
}
static inline uint32_t __QSUB16(uint32_t a, uint32_t b) {
    const int32_t lo = elxar_sat16((int32_t)(int16_t)a - (int16_t)b);
    const int32_t hi = elxar_sat16((int32_t)(int16_t)(a >> 16) - (int16_t)(b >> 16));
    return ((uint32_t)(uint16_t)hi << 16) | (uint16_t)lo;
}
static inline int32_t __QADD(int32_t a, int32_t b) {
    const int64_t r = (int64_t)a + b; return r > INT32_MAX ? INT32_MAX : (r < INT32_MIN ? INT32_MIN : (int32_t)r);
}
static inline int32_t __QSUB(int32_t a, int32_t b) {
    const int64_t r = (int64_t)a - b; return r > INT32_MAX ? INT32_MAX : (r < INT32_MIN ? INT32_MIN : (int32_t)r);
}
static inline uint32_t elxar_pkhbt(uint32_t a, uint32_t b, int sh) { return (a & 0xFFFFu) | ((b << sh) & 0xFFFF0000u); }
#define __PKHBT(ARG1, ARG2, ARG3) elxar_pkhbt((uint32_t)(int32_t)(ARG1), (uint32_t)(int32_t)(ARG2), (ARG3))
static inline uint8_t elxar_clz(uint32_t v) { return v ? (uint8_t)__builtin_clz(v) : 32u; }
#define __CLZ(value) elxar_clz((uint32_t)(value))

/* Number conversion like the ARM chip in the hardware: a decimal number that is negative becomes 0 and one that is
 * too large becomes the largest value. Intel processors do something different (wrap around), which would make
 * FM and pulse-width sounds differ between Intel and Apple silicon Macs. Used where the firmware relies on it. */
static inline uint32_t elxar_f2u(float f) {
    if (!(f > 0.0f)) return 0u;
    if (f >= 4294967296.0f) return 0xFFFFFFFFu;
    return (uint32_t)f;
}
#define ELXAR_F2U(x) elxar_f2u((float)(x))

/* the hardware random number generator: replaced by src/engine/elxar_random.c */
typedef struct { int unused; } RNG_HandleTypeDef;

#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE __attribute__((always_inline)) static inline
#endif
#ifndef __IO
#define __IO volatile
#endif
#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif
#endif
