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
void init();
void shutdown();

void onFrameArrival();
void onFrameProcessedUs(std::uint64_t us);
void onPatternFound(bool found);

// Measures time spent on grayscale + Gaussian blur for a frame.
// Called from PatternDetector::findPattern.
void onPreprocessUs(std::uint64_t us);
} // namespace collectda

