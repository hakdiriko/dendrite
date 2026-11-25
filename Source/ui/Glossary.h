// The reference (3.6): every word the window uses, and every cell type in the fly, as entries a
// hand can open. A term is written by hand; a cell type's entry is built from the fly itself (how
// many there are, what they release, whom they hear and whom they tell, by synapse counts), with
// the hand-written words added where the type has some. Everything numeric is real.
#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace dendrite::ui::glossary
{
struct Entry
{
    juce::String key;        // what search and links find it by (lower case)
    juce::String head;       // the headword
    juce::String kind;       // a short line under it: what sort of thing it is
    juce::String body;       // what it is, in the world
    juce::String inDendrite; // what it does here
    juce::StringArray see;   // other entries' keys
    juce::StringArray words; // log words that belong to it
    int population = -1;     // rig::Recording::Population, or -1
    int method = -1;         // rig::Recording::Method, or -1
    juce::String typeName;   // a cell type it stands for ("" none)
};

const std::vector<Entry>& terms();
const Entry* term (const juce::String& key); // by key or headword, any case

// the fly's cell types: how many, and the type of each neuron (indices into identities::typeName)
int typeCount();
int typeOf (int neuron);
juce::String typeNameOf (int typeIndex);
int typeIndex (const juce::String& name); // -1 if the fly has none of that name
const std::vector<uint16_t>& neuronsOfType (int typeIndex);
// the strongest partner types by synapses: inputs (toward it) or outputs (from it), most first
juce::StringArray partners (int typeIndex, bool inputs, int count);
juce::StringArray partners (const std::vector<uint16_t>& neurons, bool inputs, int count); // any set: a population's

// one line of search: what it shows and what picking it opens
struct Hit
{
    juce::String label, detail;
    juce::String termKey;  // a term ("" none)
    int type = -1;         // a cell type (-1 none)
    int score = 0;
};
std::vector<Hit> search (const juce::String& query, int most);
} // namespace dendrite::ui::glossary
