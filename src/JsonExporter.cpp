#include "JsonExporter.hpp"
#include "XcafTools.hpp"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/filewritestream.h>

#include <TopoDS_Shape.hxx>
#include <TopLoc_Location.hxx>
#include <TDF_Tool.hxx>
#include <TDataStd_Name.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>

#include <set>

using namespace rapidjson;

//------------------------------------------------------------
// Utility: Label entry "0:1:2:3" → "0-1-2-3"
//------------------------------------------------------------
static std::string LabelId(const TDF_Label& lab)
{
    TCollection_AsciiString s;
    TDF_Tool::Entry(lab, s);
    std::string id = s.ToCString();
    for (char &c : id)
        if (c == ':') c = '-';
    return id;
}

//------------------------------------------------------------
// Shape type to string (complete enum for OCCT 7.9.2)
//------------------------------------------------------------
static const char* ShapeTypeString(TopAbs_ShapeEnum t)
{
    switch (t)
    {
        case TopAbs_COMPOUND:   return "COMPOUND";
        case TopAbs_COMPSOLID:  return "COMPSOLID";
        case TopAbs_SOLID:      return "SOLID";
        case TopAbs_SHELL:      return "SHELL";
        case TopAbs_FACE:       return "FACE";
        case TopAbs_WIRE:       return "WIRE";
        case TopAbs_EDGE:       return "EDGE";
        case TopAbs_VERTEX:     return "VERTEX";
        case TopAbs_SHAPE:      return "SHAPE";
    }
    return "UNKNOWN";
}

//------------------------------------------------------------
// Add gp_Trsf as a 4×4 matrix (OCCT 7.9.2 compliant)
//------------------------------------------------------------
static void AddTransform(Value& parent, const gp_Trsf& T, Document::AllocatorType& alloc)
{
    Value arr(kArrayType);

    gp_Mat R = T.VectorialPart();     // 3×3 rotation
    gp_XYZ t = T.TranslationPart();   // translation

    arr.PushBack(R(1,1), alloc); arr.PushBack(R(1,2), alloc);
    arr.PushBack(R(1,3), alloc); arr.PushBack(t.X(),   alloc);

    arr.PushBack(R(2,1), alloc); arr.PushBack(R(2,2), alloc);
    arr.PushBack(R(2,3), alloc); arr.PushBack(t.Y(),   alloc);

    arr.PushBack(R(3,1), alloc); arr.PushBack(R(3,2), alloc);
    arr.PushBack(R(3,3), alloc); arr.PushBack(t.Z(),   alloc);

    // Last row
    arr.PushBack(0.0, alloc);
    arr.PushBack(0.0, alloc);
    arr.PushBack(0.0, alloc);
    arr.PushBack(1.0, alloc);

    parent.AddMember("transform", arr, alloc);
}

//------------------------------------------------------------
// Build a definition (unique geometry)
//------------------------------------------------------------
static void BuildDefinition(
    const TDF_Label& defLabel,
    const Handle(XCAFDoc_ShapeTool)& shapeTool,
    const Handle(XCAFDoc_ColorTool)& colorTool,
    Value& out,
    Document::AllocatorType& alloc)
{
    out.SetObject();

    std::string id = LabelId(defLabel);
    out.AddMember("id", Value(id.c_str(), alloc), alloc);

    // Name from TDataStd_Name (ExtendedString → AsciiString)
    Handle(TDataStd_Name) nameAttr;
    if (defLabel.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        TCollection_AsciiString ascii(nameAttr->Get());
        out.AddMember("name", Value(ascii.ToCString(), alloc), alloc);
    } else {
        out.AddMember("name", "Unnamed", alloc);
    }

    // Shape + type
    TopoDS_Shape shape = shapeTool->GetShape(defLabel);
    out.AddMember("shapeType", Value(ShapeTypeString(shape.ShapeType()), alloc), alloc);

    // Color
    Quantity_Color col;
    if (GetEffectiveColor(defLabel, shapeTool, colorTool, col)) {
        Value c(kArrayType);
        c.PushBack(col.Red(),   alloc);
        c.PushBack(col.Green(), alloc);
        c.PushBack(col.Blue(),  alloc);
        out.AddMember("color", c, alloc);
    } else {
        Value c(kArrayType);
        c.PushBack(0.8, alloc);
        c.PushBack(0.8, alloc);
        c.PushBack(0.8, alloc);
        out.AddMember("color", c, alloc);
    }
}

//------------------------------------------------------------
// Build instance tree — **corrected logic**
//------------------------------------------------------------
static void BuildInstance(
    const TDF_Label& inst,
    const Handle(XCAFDoc_ShapeTool)& shapeTool,
    const Handle(XCAFDoc_ColorTool)& colorTool,
    std::set<std::string>& emittedDefs,
    Value& outInst,
    Document::AllocatorType& alloc,
    Value& defsArray)
{
    outInst.SetObject();

    // Instance ID
    std::string instId = LabelId(inst);
    outInst.AddMember("id", Value(instId.c_str(), alloc), alloc);

    // Resolve definition label
    TDF_Label defLabel;
    if (!XCAFDoc_ShapeTool::GetReferredShape(inst, defLabel))
        defLabel = inst;   // free-shape root case

    std::string defId = LabelId(defLabel);
    outInst.AddMember("definitionId", Value(defId.c_str(), alloc), alloc);

    bool isInstance = !defLabel.IsEqual(inst);
    outInst.AddMember("isInstance", isInstance, alloc);

    // Transform
    TopLoc_Location L = shapeTool->GetLocation(inst);
    if (!L.IsIdentity())
        AddTransform(outInst, L.Transformation(), alloc);

    // Emit definition once
    if (!emittedDefs.count(defId))
    {
        Value defObj(kObjectType);
        BuildDefinition(defLabel, shapeTool, colorTool, defObj, alloc);
        defsArray.PushBack(defObj, alloc);
        emittedDefs.insert(defId);
    }

    //--------------------------------------------------------
    // **CRITICAL FIX: expand children using the DEFINITION**
    //--------------------------------------------------------
    Value children(kArrayType);

    if (shapeTool->IsAssembly(defLabel))
    {
        TDF_LabelSequence seq;
        shapeTool->GetComponents(defLabel, seq);

        for (int i = 1; i <= seq.Length(); ++i)
        {
            TDF_Label childInst = seq.Value(i);

            Value child(kObjectType);
            BuildInstance(childInst, shapeTool, colorTool,
                          emittedDefs, child, alloc, defsArray);
            children.PushBack(child, alloc);
        }
    }

    outInst.AddMember("children", children, alloc);
}

//============================================================
// Public API — JsonExporter::Export()
//============================================================
namespace JsonExporter {

bool Export(
    const TDF_Label& rootLabel,
    const Handle(XCAFDoc_ShapeTool)& shapeTool,
    const Handle(XCAFDoc_ColorTool)& colorTool,
    const std::string& outputJson)
{
    Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    Value defs(kArrayType);
    Value root(kObjectType);
    std::set<std::string> emitted;

    BuildInstance(rootLabel, shapeTool, colorTool, emitted, root, alloc, defs);

    doc.AddMember("definitions", defs, alloc);
    doc.AddMember("root", root, alloc);

    FILE* f = fopen(outputJson.c_str(), "w");
    if (!f) return false;

    char buff[65536];
    FileWriteStream fs(f, buff, sizeof(buff));
    PrettyWriter<FileWriteStream> writer(fs);
    writer.SetIndent(' ', 4);

    doc.Accept(writer);
    fclose(f);

    return true;
}

} // namespace JsonExporter
