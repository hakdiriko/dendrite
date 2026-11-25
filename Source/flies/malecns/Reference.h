// How hard each neuron works on ordinary music (generated: tools/rig/reference.py). See Source/rig/Reference.h.
#pragma once

#include "FlyData.h"

namespace dendrite::flydata
{
extern const uint16_t referenceCentiHz[neurons]; // mean rate on six reference sounds, in hundredths of a Hz
} // namespace dendrite::flydata
