// How deep each neuron sits (generated: tools/rig/depth.py). See Source/rig/Depth.h.
#pragma once

#include "FlyData.h"

namespace dendrite::flydata
{
constexpr float depthMicrons = 972.1f; // the cut's box, front to back
extern const uint16_t neuronZ[neurons];         // 0..65535 across depthMicrons, 0 at the front
} // namespace dendrite::flydata
