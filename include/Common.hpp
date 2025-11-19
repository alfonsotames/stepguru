#pragma once

#include <vector>
#include <array>
#include <cstdint>
#include <cmath>
#include <map>
#include <string>
#include <iostream>
#include <iomanip>

// Basic 3D types
struct Vertex {
    float x, y, z;
};

struct Normal {
    float x, y, z;
};

struct RGBA {
    float r, g, b, a;
};

// Triangle bucket (per material)
struct TriBucket {
    std::vector<Vertex>   vertices;
    std::vector<Normal>   normals;
    std::vector<uint32_t> indices;
    int materialIndex = -1;
};

// Edge bucket (per material)
struct EdgeBucket {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    int materialIndex = -1;
};

// 4-byte padding helper
inline std::size_t pad4(std::size_t n) {
    return (n + 3) & ~std::size_t(3);
}

// Min/max for glTF accessors (POSITION, NORMAL)
template <typename Vec3>
std::array<float, 6> calcMinMax(const std::vector<Vec3>& v, bool isNormal = false) {
    if (v.empty()) {
        return {0,0,0,0,0,0};
    }

    if (isNormal) {
        // Valid range for unit vectors
        return {-1.f,-1.f,-1.f, 1.f,1.f,1.f};
    }

    float minX = v[0].x, minY = v[0].y, minZ = v[0].z;
    float maxX = v[0].x, maxY = v[0].y, maxZ = v[0].z;
    for (const auto& p : v) {
        if (p.x < minX) minX = p.x;
        if (p.y < minY) minY = p.y;
        if (p.z < minZ) minZ = p.z;
        if (p.x > maxX) maxX = p.x;
        if (p.y > maxY) maxY = p.y;
        if (p.z > maxZ) maxZ = p.z;
    }
    auto snap = [](float x) {
        return (std::fabs(x) < 1e-9f) ? 0.0f : x;
    };
    return {snap(minX), snap(minY), snap(minZ),
            snap(maxX), snap(maxY), snap(maxZ)};
}

// Stats (for GLB export)
struct ExportStats {
    std::size_t vertices   = 0;
    std::size_t triangles  = 0;
    std::size_t lines      = 0;
    std::size_t materials  = 0;
    std::size_t primitives = 0;
    std::size_t bufferBytes= 0;
    std::size_t jsonBytes  = 0;
    std::size_t totalBytes = 0;
    double      elapsedSec = 0.0;

    void print(const std::string& tag = "") const {
        std::cout << "\n--- Export Statistics " << tag << " ---\n"
                  << "Vertices:   " << vertices   << "\n"
                  << "Triangles:  " << triangles  << "\n"
                  << "Edges:      " << lines      << "\n"
                  << "Materials:  " << materials  << "\n"
                  << "Primitives: " << primitives << "\n"
                  << "BIN size:   " << bufferBytes/1024.0 << " KB\n"
                  << "JSON size:  " << jsonBytes/1024.0  << " KB\n"
                  << "Total GLB:  " << totalBytes/1024.0 << " KB\n"
                  << "Elapsed:    " << std::fixed << std::setprecision(2)
                  << elapsedSec << " seconds\n"
                  << "--------------------------\n\n";
    }
};

// Simple material registry: RGBA → index
struct MaterialRegistry {
    std::map<std::uint32_t, int> lut;
    std::vector<RGBA> mats;

    static std::uint32_t pack(const RGBA& c) {
        auto clamp01 = [](float v) {
            if (v < 0.0f) return 0.0f;
            if (v > 1.0f) return 1.0f;
            return v;
        };
        std::uint8_t r = static_cast<std::uint8_t>(std::lround(clamp01(c.r) * 255.0f));
        std::uint8_t g = static_cast<std::uint8_t>(std::lround(clamp01(c.g) * 255.0f));
        std::uint8_t b = static_cast<std::uint8_t>(std::lround(clamp01(c.b) * 255.0f));
        std::uint8_t a = static_cast<std::uint8_t>(std::lround(clamp01(c.a) * 255.0f));
        return (std::uint32_t(r) << 24)
             | (std::uint32_t(g) << 16)
             | (std::uint32_t(b) << 8)
             |  std::uint32_t(a);
    }

    int getOrCreate(const RGBA& c) {
        std::uint32_t k = pack(c);
        auto it = lut.find(k);
        if (it != lut.end()) {
            return it->second;
        }
        int idx = static_cast<int>(mats.size());
        mats.push_back(c);
        lut.emplace(k, idx);
        return idx;
    }

    const std::vector<RGBA>& materials() const {
        return mats;
    }
};
