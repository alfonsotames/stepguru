#include "Exporter.hpp"

#include "Common.hpp"
#include "XcafTools.hpp"
#include "MeshExtractor.hpp"
#include "GlbBuilder.hpp"
#include "PngRenderer.hpp"
#include "JsonExporter.hpp"

#include <iostream>
#include <thread>
#include <filesystem>
#include <unordered_map>
#include <cstring>

#include <BRepMesh_IncrementalMesh.hxx>
#include <STEPCAFControl_Reader.hxx>

struct CachedMesh {
    std::vector<TriBucket> triBuckets;
    std::vector<EdgeBucket> edgeBuckets;
    std::vector<RGBA>       materials;
};

int Exporter::run(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: step2glb input.step [--outdir DIR] [--stats] [--validate]\n";
        return 1;
    }

    std::cout << "STEP → GLB exporter with assembly + per-component outputs (+ colored STEP per component)\n";

    int hwThreads = std::thread::hardware_concurrency();
    if (hwThreads < 2) hwThreads = 2;

    // OCCT’s own parallel meshing (safe, internal)
    BRepMesh_IncrementalMesh::SetParallelDefault(Standard_True);
    std::cout << "OCCT parallel meshing enabled, hardware threads: "
              << hwThreads << "\n";

    Options opt = parseArgs(argc, argv);
    if (opt.input.empty()) {
        std::cerr << "No input STEP file.\n";
        return 1;
    }

    return exportAssemblyAndComponents(opt) ? 0 : 1;
}

Exporter::Options Exporter::parseArgs(int argc, char* argv[])
{
    Options o;
    o.input = argv[1];

    std::filesystem::path inPath(o.input);
    o.outDir = inPath.has_parent_path()
             ? (inPath.parent_path().string() + "/")
             : std::string();

    for (int i=2; i<argc; ++i) {
        if (!std::strcmp(argv[i], "--stats")) {
            o.printStats = true;
        } else if (!std::strcmp(argv[i], "--validate")) {
            o.validate = true;
        } else if (!std::strcmp(argv[i], "--outdir") && i+1<argc) {
            o.outDir = argv[i+1];
            if (!o.outDir.empty() &&
                o.outDir.back() != '/' &&
                o.outDir.back() != '\\')
            {
                o.outDir.push_back('/');
            }
            ++i;
        }
    }
    return o;
}

bool Exporter::exportAssemblyAndComponents(const Options& opt)
{
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) doc;
    app->NewDocument("MDTV-XCAF", doc);

    STEPCAFControl_Reader reader;
    if (reader.ReadFile(opt.input.c_str()) != IFSelect_RetDone) {
        std::cerr << "❌ Cannot read STEP file: " << opt.input << "\n";
        return false;
    }
    reader.SetColorMode(true);
    reader.Transfer(doc);

    Handle(XCAFDoc_ShapeTool) shapeTool =
        XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    Handle(XCAFDoc_ColorTool) colorTool =
        XCAFDoc_DocumentTool::ColorTool(doc->Main());

    TDF_LabelSequence roots;
    shapeTool->GetFreeShapes(roots);
    if (roots.IsEmpty()) {
        std::cerr << "No shapes found in STEP.\n";
        return false;
    }

    // Tree dump
    std::cout << "\n================ ASSEMBLY TREE DUMP ================\n";
    {
        std::set<std::string> visitedDump;
        for (Standard_Integer r=1; r<=roots.Length(); ++r) {
            bool isLastRoot = (r == roots.Length());
            DumpAssemblyTreeDeep(roots.Value(r), shapeTool, colorTool,
                                 visitedDump, 0, isLastRoot, "");
        }
    }
    std::cout << "====================================================\n\n";


    //----------------- Json Tree dump -----------------------------
    std::cout << "\n JSON → Export\n";
    {
        TDF_LabelSequence roots;
        shapeTool->GetFreeShapes(roots);

        if (roots.Length() == 0) {
            std::cerr << "ERROR: No free shapes in STEP document.\n";
        } else {
            TDF_Label root = roots.First();
            std::string jsonOut = opt.outDir  + "assembly.json";
            std::cout << "\n File: " << jsonOut << std::endl;
            if (!JsonExporter::Export(root, shapeTool, colorTool, jsonOut)) {
                std::cerr << "ERROR: Failed to write JSON assembly file\n";
            }
        }
    }

    //-----------------------------------------------------------


    // Assembly components (shallow)
    TDF_LabelSequence assemblyComps;
    CollectAssemblyComponentsShallow(shapeTool, roots, assemblyComps);

    std::vector<TopoDS_Shape> assemblyShapes;
    std::vector<RGBA>         assemblyColors;
    assemblyShapes.reserve(assemblyComps.Length());
    assemblyColors.reserve(assemblyComps.Length());

    RGBA defaultGray{0.7f,0.7f,0.7f,1.0f};

    for (Standard_Integer i=1; i<=assemblyComps.Length(); ++i) {
        const TDF_Label lab = assemblyComps.Value(i);
        TopoDS_Shape s = shapeTool->GetShape(lab);
        if (s.IsNull()) continue;

        assemblyShapes.push_back(s);
        assemblyColors.push_back(ResolveColorRGBA(lab, shapeTool, colorTool, defaultGray));
    }

    if (assemblyShapes.empty()) {
        std::cerr << "❌ No components found for assembly.\n";
        return false;
    }

    std::cout << "Assembly has " << assemblyShapes.size()
              << " top-level component(s).\n";

    // Leaf components (deep)
    TDF_LabelSequence leafComps;
    CollectLeafComponentsDeep(shapeTool, roots, leafComps);
    if (leafComps.Length() == 0) {
        std::cerr << "❌ No leaf components found.\n";
        return false;
    }
    std::cout << "Found " << leafComps.Length()
              << " leaf component instance(s) for per-part export.\n";

    std::string rootPath = LabelPathForFilename(roots.Value(1));

    // ───────────────────────────────── Assembly GLB + PNG ────────────────────────────────
    {
        MaterialRegistry matRegAssembly;
        std::vector<TriBucket>  triBucketsAsm;
        std::vector<EdgeBucket> edgeBucketsAsm;

        // IMPORTANT: use shared MaterialRegistry so each part keeps its color
        for (std::size_t i=0; i<assemblyShapes.size(); ++i) {
            MeshShape(assemblyShapes[i], assemblyColors[i],
                      matRegAssembly, triBucketsAsm, edgeBucketsAsm);
        }

        GlbBuilder builder;
        builder.addBuckets(triBucketsAsm, edgeBucketsAsm, matRegAssembly.materials());
        ExportStats stats;

        if (assemblyShapes.size() == 1) {
            std::string glbName  = opt.outDir + "out_"   + rootPath + "_1.glb";
            std::string pngName  = opt.outDir + "image_" + rootPath + "_1.png";
            std::string stepName = opt.outDir + "out_"   + rootPath + "_1.step";

            std::cout << "Single component assembly → exporting "
                      << glbName << " and " << pngName << "\n";

            builder.writeGlb(glbName, opt.printStats, stats);
            RenderPNG({assemblyShapes[0]}, {assemblyColors[0]}, pngName);
            ExportShapeToSTEP(roots.Value(1), shapeTool, colorTool, stepName);
        } else {
            std::string glbName  = opt.outDir + "out_"   + rootPath + "_1.glb";
            std::string pngName  = opt.outDir + "image_" + rootPath + "_1.png";

            builder.writeGlb(glbName, opt.printStats, stats);
            RenderPNG(assemblyShapes, assemblyColors, pngName);
        }
    }

    // ────────────────────────────── Per-component GLB / PNG / STEP ──────────────────────
    std::unordered_map<std::string, CachedMesh> meshCache;

    for (Standard_Integer i=1; i<=leafComps.Length(); ++i) {
        const TDF_Label instLab = leafComps.Value(i);
        TopoDS_Shape s = shapeTool->GetShape(instLab);
        if (s.IsNull()) continue;

        RGBA col = ResolveColorRGBA(instLab, shapeTool, colorTool, defaultGray);

        TDF_Label refLab;
        bool isInstance = shapeTool->GetReferredShape(instLab, refLab);
        TDF_Label namingLab = isInstance ? refLab : instLab;
        std::string p = LabelPathForFilename(namingLab);

        std::string gname = opt.outDir + "out_"   + p + "_1.glb";
        std::string pname = opt.outDir + "image_" + p + "_1.png";
        std::string sname = opt.outDir + "out_"   + p + "_1.step";

        CachedMesh localMesh;
        bool fromCache = false;

        auto it = meshCache.find(p);
        if (it != meshCache.end()) {
            localMesh = it->second;
            fromCache = true;
        }

        if (!fromCache) {
            MaterialRegistry localReg;
            std::vector<TriBucket>  triBuckets;
            std::vector<EdgeBucket> edgeBuckets;

            MeshShape(s, col, localReg, triBuckets, edgeBuckets);

            localMesh.triBuckets = std::move(triBuckets);
            localMesh.edgeBuckets = std::move(edgeBuckets);
            localMesh.materials   = localReg.materials();

            meshCache.emplace(p, localMesh);
        }

        std::cout << "\n--- Exporting component (filename from "
                  << (isInstance ? "referred" : "instance")
                  << " label) " << p << " ---\n";

        GlbBuilder builder;
        builder.addBuckets(localMesh.triBuckets,
                           localMesh.edgeBuckets,
                           localMesh.materials);
        ExportStats stats;
        builder.writeGlb(gname, opt.printStats, stats);

        RenderPNG({s}, {col}, pname);
        ExportShapeToSTEP(instLab, shapeTool, colorTool, sname);
    }

    return true;
}
