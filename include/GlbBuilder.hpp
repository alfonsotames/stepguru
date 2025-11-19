#pragma once

#include "Common.hpp"
#include <string>
#include <vector>

class GlbBuilder {
public:
    GlbBuilder() = default;

    // Append buckets + materials (can be called multiple times)
    void addBuckets(const std::vector<TriBucket>& tris,
                    const std::vector<EdgeBucket>& edges,
                    const std::vector<RGBA>& materials);

    // Build and write GLB. Fills outStats and prints stats if requested.
    bool writeGlb(const std::string& filename,
                  bool printStats,
                  ExportStats& outStats);

private:
    std::vector<TriBucket> m_triBuckets;
    std::vector<EdgeBucket> m_edgeBuckets;
    std::vector<RGBA>      m_materials;
};
