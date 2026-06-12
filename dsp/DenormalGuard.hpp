#pragma once
// DenormalGuard.hpp — arm flush-to-zero (FTZ) on the audio thread.
// (Same pattern as nxstrip-dpf/dsp/DenormalGuard.hpp.)
//
// Denormal floats incur a per-operation penalty on the Cortex-A53 FPU. After
// a trigger the siren's analog states all decay geometrically toward zero
// (C5 discharge, the power de-click ramp, the output HP/LP one-poles), so the
// engine dwells in the denormal range for tens of seconds after every use and
// pays that penalty on every sample. Flushing denormals to zero removes the
// penalty at no audible cost. FPCR is a per-thread register, so we arm it
// once on whichever thread first runs audio.
//
// On the host (x86 dev/test builds) this is a no-op: the offline tests stay
// bit-comparable with the reference renders.

#include <cstdint>

namespace ftz {

inline void enable() noexcept
{
#if defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ull << 24);   // FPCR.FZ — flush denormals (inputs & results) to zero
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif
}

// Arms FTZ exactly once per thread. Call at the top of the audio callback;
// after the first call the branch is essentially free.
inline void armOnce() noexcept
{
    static thread_local bool armed = false;
    if (!armed) { enable(); armed = true; }
}

} // namespace ftz
