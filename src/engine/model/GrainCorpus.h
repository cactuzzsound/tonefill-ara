#pragma once

#include <vector>

namespace tonefill::engine::model
{
// Feature vector used for grain-to-grain similarity (smooth concatenation, anti-repeat).
struct GrainFeatures
{
    float rms      = 0.0f;
    float centroid = 0.0f;
    float flatness = 0.0f;
    float lowBand  = 0.0f;
    float midBand  = 0.0f;
    float highBand = 0.0f;
};

// A single windowed texture grain (residual-domain, tonal/transient removed by default).
struct Grain
{
    std::vector<float> samples;   // windowed time-domain grain
    GrainFeatures      features;
};

// Textural movement source for a single channel. Immutable once built.
// Potentially large: memory is bounded by GrainCorpusBuilder (decimate if exceeded).
struct GrainCorpus
{
    std::vector<Grain> grains;
    int                grainSizeSamples = 0;
    int                hopSamples       = 0;
    int                usableCount      = 0; // grains passing quality gate

    // Tonal-removed time-domain material the granular synth draws grains from (V1 uses this
    // flat buffer directly; the `grains`/`adjacency` lists are for the later kNN path).
    std::vector<float> sourceResidual;

    // Optional precomputed nearest-neighbour adjacency (V1: brute force, may stay empty).
    std::vector<std::vector<int>> adjacency;
};
} // namespace tonefill::engine::model
