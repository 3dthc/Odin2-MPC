/* odin2-mpc: rand() for the audio code without glibc's lock. rand() takes a process-wide lock on every call, and Odin
 * calls it per sample (analog drift, noise, LFO noise); with TWO CORES both processors' threads fought over it. Same
 * range (0..RAND_MAX), one state per thread (xorshift32), seeded with odin_srand() like srand(). */
#pragma once
#include <cstdint>
#include <cstdlib>

inline uint32_t &odin_rand_state() {
	static thread_local uint32_t s = 2463534242u;
	return s;
}
inline void odin_srand(unsigned seed) { odin_rand_state() = seed ? seed * 2654435761u + 1u : 2463534242u; }
inline int odin_rand() {
	uint32_t &x = odin_rand_state();
	x ^= x << 13, x ^= x >> 17, x ^= x << 5;
	return (int)(x % ((uint32_t)RAND_MAX + 1u));
}
