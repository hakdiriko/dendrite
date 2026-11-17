// A brainless FlyData for the teach page's WebAssembly build: the page plays the fly's body by
// hand and never runs its brain, so the 11 MB of wiring stays out of the module. Same names and
// sizes as the generated Source/dsp/FlyData.cpp, every value zero.
#include "dsp/FlyData.h"

namespace dendrite::flydata
{
const uint32_t rowStart[neurons + 1] {};
const uint16_t target[synapses] {};
const int16_t weight[synapses] {};
const uint16_t senseStart[senses + smells + 1] {};
const uint16_t senseNeuron[1] {};
const uint16_t escapeNeuron[escapeCount] {}, legNeuron[legCount] {}, leftNeuron[leftCount] {}, rightNeuron[rightCount] {};
const uint16_t kenyon[kenyonCount] {}, mbon[mbonCount] {}, dan[danCount] {}, ppl1[ppl1Count] {};
const int8_t mbonSign[mbonCount] {};
const uint32_t plastic[plasticCount] {};
const uint16_t plasticKenyon[plasticCount] {};
const uint8_t plasticMbon[plasticCount] {};
const uint16_t dopamineDan[dopamineCount] {};
const uint8_t dopamineMbon[dopamineCount] {};
const float dopamineShare[dopamineCount] {};
const uint16_t neuronX[neurons] {}, neuronY[neurons] {};
const uint8_t backdrop[backdropWidth * backdropHeight] {};
const Anchor anchors[anchorCount] {};
} // namespace dendrite::flydata
