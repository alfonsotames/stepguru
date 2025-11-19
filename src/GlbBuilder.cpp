#include "GlbBuilder.hpp"

#include <fstream>
#include <sstream>
#include <cstring>
#include <chrono>

void GlbBuilder::addBuckets(const std::vector<TriBucket>& tris,
                            const std::vector<EdgeBucket>& edges,
                            const std::vector<RGBA>& materials)
{
    // Append materials, remapping indices
    int matBase = static_cast<int>(m_materials.size());
    m_materials.insert(m_materials.end(), materials.begin(), materials.end());

    // Triangles
    for (const auto& b : tris) {
        if (b.vertices.empty()) continue;
        TriBucket dst = b;
        if (dst.materialIndex >= 0) {
            dst.materialIndex += matBase;
        }
        m_triBuckets.push_back(std::move(dst));
    }

    // Edges
    for (const auto& e : edges) {
        if (e.vertices.empty()) continue;
        EdgeBucket dst = e;
        if (dst.materialIndex >= 0) {
            dst.materialIndex += matBase;
        }
        m_edgeBuckets.push_back(std::move(dst));
    }
}

bool GlbBuilder::writeGlb(const std::string& filename,
                          bool printStats,
                          ExportStats& outStats)
{
    if (m_triBuckets.empty() && m_edgeBuckets.empty()) {
        std::cerr << "[GlbBuilder] No geometry to write for " << filename << "\n";
        return false;
    }

    auto tStart = std::chrono::high_resolution_clock::now();

    std::vector<std::uint8_t> bin;
    bin.reserve(1 << 20);

    struct BufferView {
        std::uint32_t buffer;
        std::uint32_t byteOffset;
        std::uint32_t byteLength;
        int           target;
    };
    struct Accessor {
        int bufferView;
        int componentType;
        std::uint32_t count;
        std::string type;
        std::array<float,6> bounds;
        bool hasBounds;
    };
    struct Primitive {
        int posAcc;
        int nrmAcc;
        int idxAcc;
        int material;
        int mode; // 4 = TRIANGLES, 1 = LINES
    };

    std::vector<BufferView> bufferViews;
    std::vector<Accessor>   accessors;
    std::vector<Primitive>  primitives;

    auto appendBin = [&](const void* data, std::size_t bytes) {
        std::size_t off = bin.size();
        bin.resize(off + bytes);
        std::memcpy(bin.data() + off, data, bytes);
        return off;
    };

    // Triangles
    for (const auto& b : m_triBuckets) {
        if (b.vertices.empty() || b.indices.empty()) continue;

        std::size_t posOff = appendBin(b.vertices.data(),
                                       b.vertices.size() * sizeof(Vertex));
        bufferViews.push_back({0, static_cast<std::uint32_t>(posOff),
                               static_cast<std::uint32_t>(b.vertices.size() * sizeof(Vertex)),
                               34962});
        int posBV = static_cast<int>(bufferViews.size() - 1);

        std::size_t nrmOff = appendBin(b.normals.data(),
                                       b.normals.size() * sizeof(Normal));
        bufferViews.push_back({0, static_cast<std::uint32_t>(nrmOff),
                               static_cast<std::uint32_t>(b.normals.size() * sizeof(Normal)),
                               34962});
        int nrmBV = static_cast<int>(bufferViews.size() - 1);

        std::size_t idxOff = appendBin(b.indices.data(),
                                       b.indices.size() * sizeof(std::uint32_t));
        bufferViews.push_back({0, static_cast<std::uint32_t>(idxOff),
                               static_cast<std::uint32_t>(b.indices.size() * sizeof(std::uint32_t)),
                               34963});
        int idxBV = static_cast<int>(bufferViews.size() - 1);

        auto vb = calcMinMax(b.vertices, false);
        auto nb = calcMinMax(b.normals,  true);

        accessors.push_back({posBV, 5126, static_cast<std::uint32_t>(b.vertices.size()), "VEC3", vb, true});
        int posAcc = static_cast<int>(accessors.size() - 1);

        accessors.push_back({nrmBV, 5126, static_cast<std::uint32_t>(b.normals.size()),  "VEC3", nb, true});
        int nrmAcc = static_cast<int>(accessors.size() - 1);

        accessors.push_back({idxBV, 5125, static_cast<std::uint32_t>(b.indices.size()),  "SCALAR",
                             {0,0,0,0,0,0}, false});
        int idxAcc = static_cast<int>(accessors.size() - 1);

        int matIndex = (b.materialIndex >= 0 && b.materialIndex < static_cast<int>(m_materials.size()))
                     ? b.materialIndex
                     : 0;

        primitives.push_back({posAcc, nrmAcc, idxAcc, matIndex, 4});
    }

    // Lines
    for (const auto& e : m_edgeBuckets) {
        if (e.vertices.empty() || e.indices.empty()) continue;

        std::size_t posOff = appendBin(e.vertices.data(),
                                       e.vertices.size() * sizeof(Vertex));
        bufferViews.push_back({0, static_cast<std::uint32_t>(posOff),
                               static_cast<std::uint32_t>(e.vertices.size() * sizeof(Vertex)),
                               34962});
        int posBV = static_cast<int>(bufferViews.size() - 1);

        std::size_t idxOff = appendBin(e.indices.data(),
                                       e.indices.size() * sizeof(std::uint32_t));
        bufferViews.push_back({0, static_cast<std::uint32_t>(idxOff),
                               static_cast<std::uint32_t>(e.indices.size() * sizeof(std::uint32_t)),
                               34963});
        int idxBV = static_cast<int>(bufferViews.size() - 1);

        auto vb = calcMinMax(e.vertices, false);

        accessors.push_back({posBV, 5126, static_cast<std::uint32_t>(e.vertices.size()), "VEC3", vb, true});
        int posAcc = static_cast<int>(accessors.size() - 1);

        accessors.push_back({idxBV, 5125, static_cast<std::uint32_t>(e.indices.size()), "SCALAR",
                             {0,0,0,0,0,0}, false});
        int idxAcc = static_cast<int>(accessors.size() - 1);

        int matIndex = (e.materialIndex >= 0 && e.materialIndex < static_cast<int>(m_materials.size()))
                     ? e.materialIndex
                     : 0;

        primitives.push_back({posAcc, -1, idxAcc, matIndex, 1});
    }

    // 4-byte align BIN
    bin.resize(pad4(bin.size()), 0);

    // Build JSON
    std::ostringstream json;
    json << "{\n";
    json << "  \"asset\": {\"version\": \"2.0\", \"generator\": \"step2glb\"},\n";
    json << "  \"scene\": 0,\n";
    json << "  \"scenes\": [{\"nodes\": [0]}],\n";
    json << "  \"nodes\": [{\"mesh\": 0}],\n";

    // Materials
    json << "  \"materials\": [\n";
    for (std::size_t i=0; i<m_materials.size(); ++i) {
        const auto& m = m_materials[i];
        json << "    {\"pbrMetallicRoughness\": {"
             << "\"baseColorFactor\": ["
             << m.r << "," << m.g << "," << m.b << "," << m.a
             << "], \"metallicFactor\": 0.0, \"roughnessFactor\": 1.0}, "
             << "\"doubleSided\": true}";
        if (i + 1 < m_materials.size()) json << ",";
        json << "\n";
    }
    json << "  ],\n";

    // Mesh
    json << "  \"meshes\": [\n";
    json << "    {\"primitives\": [\n";
    for (std::size_t i=0; i<primitives.size(); ++i) {
        const auto& p = primitives[i];
        json << "      {\"attributes\": {\"POSITION\": " << p.posAcc;
        if (p.nrmAcc >= 0) json << ", \"NORMAL\": " << p.nrmAcc;
        json << "}, \"indices\": " << p.idxAcc
             << ", \"material\": " << p.material
             << ", \"mode\": " << p.mode << "}";
        if (i + 1 < primitives.size()) json << ",";
        json << "\n";
    }
    json << "    ]}\n";
    json << "  ],\n";

    // Buffers
    json << "  \"buffers\": [ { \"byteLength\": " << bin.size() << " } ],\n";

    // BufferViews
    json << "  \"bufferViews\": [\n";
    for (std::size_t i=0; i<bufferViews.size(); ++i) {
        const auto& bv = bufferViews[i];
        json << "    {\"buffer\": 0, \"byteOffset\": " << bv.byteOffset
             << ", \"byteLength\": " << bv.byteLength
             << ", \"target\": " << bv.target << "}";
        if (i + 1 < bufferViews.size()) json << ",";
        json << "\n";
    }
    json << "  ],\n";

    // Accessors
    json << "  \"accessors\": [\n";
    for (std::size_t i=0; i<accessors.size(); ++i) {
        const auto& a = accessors[i];
        json << "    {\"bufferView\": " << a.bufferView
             << ", \"componentType\": " << a.componentType
             << ", \"count\": " << a.count
             << ", \"type\": \"" << a.type << "\"";
        if (a.hasBounds) {
            json << ", \"min\": ["
                 << a.bounds[0] << "," << a.bounds[1] << "," << a.bounds[2] << "]";
            json << ", \"max\": ["
                 << a.bounds[3] << "," << a.bounds[4] << "," << a.bounds[5] << "]";
        }
        json << "}";
        if (i + 1 < accessors.size()) json << ",";
        json << "\n";
    }
    json << "  ]\n";
    json << "}\n";

    std::string jsonStr = json.str();
    std::size_t jsonLenPadded = pad4(jsonStr.size());
    jsonStr.resize(jsonLenPadded, ' ');

    std::uint32_t totalLen = 12 + 8 + static_cast<std::uint32_t>(jsonLenPadded)
                             + 8 + static_cast<std::uint32_t>(bin.size());

    // Write GLB file
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot open output file: " << filename << "\n";
        return false;
    }

    const std::uint32_t magic   = 0x46546C67; // "glTF"
    const std::uint32_t version = 2;

    out.write(reinterpret_cast<const char*>(&magic),   4);
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&totalLen),4);

    const std::uint32_t jsonChunkLen  = static_cast<std::uint32_t>(jsonLenPadded);
    const std::uint32_t jsonChunkType = 0x4E4F534A; // "JSON"
    out.write(reinterpret_cast<const char*>(&jsonChunkLen), 4);
    out.write(reinterpret_cast<const char*>(&jsonChunkType),4);
    out.write(jsonStr.data(), jsonLenPadded);

    const std::uint32_t binChunkLen  = static_cast<std::uint32_t>(bin.size());
    const std::uint32_t binChunkType = 0x004E4942; // "BIN\0"
    out.write(reinterpret_cast<const char*>(&binChunkLen), 4);
    out.write(reinterpret_cast<const char*>(&binChunkType),4);
    if (binChunkLen) {
        out.write(reinterpret_cast<const char*>(bin.data()), bin.size());
    }

    // Stats
    ExportStats st;
    for (const auto& b : m_triBuckets) {
        st.vertices  += b.vertices.size();
        st.triangles += b.indices.size() / 3;
    }
    for (const auto& e : m_edgeBuckets) {
        st.lines += e.indices.size() / 2;
    }
    st.materials   = m_materials.size();
    st.primitives  = primitives.size();
    st.bufferBytes = bin.size();
    st.jsonBytes   = jsonStr.size();
    st.totalBytes  = totalLen;
    auto tEnd      = std::chrono::high_resolution_clock::now();
    st.elapsedSec  = std::chrono::duration<double>(tEnd - tStart).count();

    if (printStats) {
        st.print(filename);
    }

    outStats = st;
    std::cout << "✅ Export complete: " << filename << "\n";
    return true;
}
