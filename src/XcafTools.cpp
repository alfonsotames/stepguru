#include "XcafTools.hpp"

#include <TDF_Tool.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDataStd_Name.hxx>

#include <STEPCAFControl_Writer.hxx>
#include <Interface_Static.hxx>
#include <IFSelect_ReturnStatus.hxx>


#include <TopoDS_Shape.hxx>

#include <iostream>

// Label path → "0-1-1-2"
std::string LabelPathForFilename(const TDF_Label& lab)
{
    TCollection_AsciiString p;
    TDF_Tool::Entry(lab, p);        // e.g. "0:1:1:2"
    std::string s = p.ToCString();  // std::string copy

    for (char& c : s) {
        if (c == ':') c = '-';
    }
    return s;
}

bool GetEffectiveColor(const TDF_Label&              label,
                       const Handle(XCAFDoc_ShapeTool)& shapeTool,
                       const Handle(XCAFDoc_ColorTool)& colorTool,
                       Quantity_Color& outColor)
{
    if (colorTool.IsNull())
        return false;

    // 1. Instance-level color on this label
    if (colorTool->GetColor(label, XCAFDoc_ColorSurf, outColor) ||
        colorTool->GetColor(label, XCAFDoc_ColorGen,  outColor) ||
        colorTool->GetColor(label, XCAFDoc_ColorCurv, outColor))
    {
        return true;
    }

    // 2. Prototype color
    TDF_Label ref;
    if (shapeTool->GetReferredShape(label, ref)) {
        if (colorTool->GetColor(ref, XCAFDoc_ColorSurf, outColor) ||
            colorTool->GetColor(ref, XCAFDoc_ColorGen,  outColor) ||
            colorTool->GetColor(ref, XCAFDoc_ColorCurv, outColor))
        {
            return true;
        }
    }

    // 3. Shape color: find label corresponding to this shape
    TopoDS_Shape S = shapeTool->GetShape(label);
    if (!S.IsNull()) {
        TDF_Label shapeLab;
        if (shapeTool->Search(S, shapeLab)) {
            if (colorTool->GetColor(shapeLab, XCAFDoc_ColorSurf, outColor) ||
                colorTool->GetColor(shapeLab, XCAFDoc_ColorGen,  outColor) ||
                colorTool->GetColor(shapeLab, XCAFDoc_ColorCurv, outColor))
            {
                return true;
            }
        }
    }

    return false;
}

RGBA ResolveColorRGBA(const TDF_Label&              label,
                      const Handle(XCAFDoc_ShapeTool)& shapeTool,
                      const Handle(XCAFDoc_ColorTool)& colorTool,
                      const RGBA& defaultCol)
{
    Quantity_Color qc;
    if (GetEffectiveColor(label, shapeTool, colorTool, qc)) {
        return RGBA{(float)qc.Red(), (float)qc.Green(), (float)qc.Blue(), 1.0f};
    }
    return defaultCol;
}

bool ExportShapeToSTEP(const TDF_Label&                    compLabel,
                       const Handle(XCAFDoc_ShapeTool)&    /*shapeTool*/,
                       const Handle(XCAFDoc_ColorTool)&    /*colorTool*/,
                       const std::string&                  stepFile)
{
    if (compLabel.IsNull()) {
        std::cerr << "Cannot export: null label\n";
        return false;
    }

    try {
        Interface_Static::SetCVal("write.step.schema", "AP242DIS");
    } catch (...) {
        // ignore if not supported
    }

    STEPCAFControl_Writer writer;
    writer.SetColorMode(Standard_True);
    writer.SetNameMode(Standard_True);

    if (!writer.Transfer(compLabel, STEPControl_AsIs)) {
        std::cerr << "STEPCAF Transfer failed for " << stepFile << "\n";
        return false;
    }

    IFSelect_ReturnStatus wr = writer.Write(stepFile.c_str());
    if (wr != IFSelect_RetDone) {
        std::cerr << "STEPCAF Write failed for " << stepFile << "\n";
        return false;
    }

    std::cout << "📄 Colored STEP saved: " << stepFile << "\n";
    return true;
}

void CollectAssemblyComponentsShallow(
    const Handle(XCAFDoc_ShapeTool)& shapeTool,
    const TDF_LabelSequence&         roots,
    TDF_LabelSequence&               out)
{
    for (Standard_Integer r = 1; r <= roots.Length(); ++r) {
        const TDF_Label& root = roots.Value(r);

        TDF_LabelSequence children;
        shapeTool->GetComponents(root, children, Standard_False); // shallow

        if (children.IsEmpty()) {
            out.Append(root);
        } else {
            for (Standard_Integer i = 1; i <= children.Length(); ++i) {
                out.Append(children.Value(i));
            }
        }
    }
}

void CollectLeafComponentsDeep(
    const Handle(XCAFDoc_ShapeTool)& shapeTool,
    const TDF_LabelSequence&         roots,
    TDF_LabelSequence&               out)
{
    std::set<std::string> seen;

    for (Standard_Integer r=1; r<=roots.Length(); ++r) {
        const TDF_Label& root = roots.Value(r);

        TDF_LabelSequence allComps;
        shapeTool->GetComponents(root, allComps, Standard_True); // deep

        for (Standard_Integer i=1; i<=allComps.Length(); ++i) {
            const TDF_Label& lab = allComps.Value(i);

            TCollection_AsciiString entry;
            TDF_Tool::Entry(lab, entry);
            std::string key = entry.ToCString();
            if (!seen.insert(key).second) {
                continue;
            }

            TDF_LabelSequence children;
            shapeTool->GetComponents(lab, children, Standard_False);
            if (!children.IsEmpty()) {
                continue;
            }

            out.Append(lab);
        }
    }

    if (out.IsEmpty()) {
        for (Standard_Integer r=1; r<=roots.Length(); ++r) {
            out.Append(roots.Value(r));
        }
    }
}

void DumpAssemblyTreeDeep(
    const TDF_Label&                 label,
    const Handle(XCAFDoc_ShapeTool)& shapeTool,
    const Handle(XCAFDoc_ColorTool)& colorTool,
    std::set<std::string>&           visited,
    int                              depth,
    bool                             isLast,
    const std::string&               prefix)
{
    (void)depth;

    if (label.IsNull()) return;

    TCollection_AsciiString entry;
    TDF_Tool::Entry(label, entry);
    std::string key = entry.ToCString();
    if (visited.count(key)) return;
    visited.insert(key);

    TDF_LabelSequence instChildren;
    shapeTool->GetComponents(label, instChildren);

    TDF_Label ref;
    bool isInstance = shapeTool->GetReferredShape(label, ref);

    TDF_LabelSequence protoChildren;
    if (isInstance) {
        shapeTool->GetComponents(ref, protoChildren);
    }

    std::vector<TDF_Label> children;
    children.reserve(instChildren.Length() + protoChildren.Length());
    for (Standard_Integer i=1; i<=instChildren.Length(); ++i)
        children.push_back(instChildren.Value(i));
    for (Standard_Integer i=1; i<=protoChildren.Length(); ++i)
        children.push_back(protoChildren.Value(i));

    bool isLeaf = children.empty();

    std::string branch = isLast ? "└─ " : "├─ ";
    std::string childPrefix = prefix + (isLast ? "   " : "│  ");

    TCollection_AsciiString path;
    TDF_Tool::Entry(label, path);

    TCollection_ExtendedString xName;
    Handle(TDataStd_Name) nAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nAttr))
        xName = nAttr->Get();
    else
        xName = "(unnamed)";
    TCollection_AsciiString asciiName(xName);

    std::string type = isLeaf ? "Part" : "Assembly";

    Quantity_Color qc;
    bool hasColor = GetEffectiveColor(label, shapeTool, colorTool, qc);

    std::cout << prefix << branch
              << "[" << path << "] "
              << type << ": "
              << asciiName.ToCString();

    if (isInstance) {
        TCollection_AsciiString refPath;
        TDF_Tool::Entry(ref, refPath);
        std::cout << " (→ " << refPath << ")";
    }

    if (hasColor) {
        std::cout << "  Color=("
                  << qc.Red() << ", "
                  << qc.Green() << ", "
                  << qc.Blue() << ")";
    }

    std::cout << "\n";

    for (std::size_t i=0; i<children.size(); ++i) {
        bool lastChild = (i == children.size() - 1);
        DumpAssemblyTreeDeep(
            children[i],
            shapeTool,
            colorTool,
            visited,
            depth + 1,
            lastChild,
            childPrefix
        );
    }
}
