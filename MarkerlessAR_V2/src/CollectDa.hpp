#pragma once

#include <cstdint>

// Enable with either -DcollectDa=1 or -DCOLLECTDA=1.
#if !defined(COLLECTDA)
#  if defined(collectDa)
#    define COLLECTDA (collectDa)
#  else
#    define COLLECTDA 0
#  endif
#endif

namespace collectda
{
#if COLLECTDA
void init();
void shutdown();

void onFrameArrival();
void onFrameProcessedUs(std::uint64_t us);
void onPatternFound(bool found);

// Records a "preprocess" duration for a frame.
// In the DMA/PL pipeline, this is used for end-to-end PL latency.
void onPreprocessUs(std::uint64_t us);
#else
inline void init() {}
inline void shutdown() {}
inline void onFrameArrival() {}
inline void onFrameProcessedUs(std::uint64_t) {}
inline void onPatternFound(bool) {}
inline void onPreprocessUs(std::uint64_t) {}
#endif
} // namespace collectda

