#pragma once

#include "Common.hpp"

#include <XCAFApp_Application.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <Quantity_Color.hxx>
#include <set>
#include <string>

// Label path → safe filename ("0:1:1:2" → "0-1-1-2")
std::string LabelPathForFilename(const TDF_Label& lab);

// Effective XDE color resolution
bool GetEffectiveColor(const TDF_Label&              label,
                       const Handle(XCAFDoc_ShapeTool)& shapeTool,
                       const Handle(XCAFDoc_ColorTool)& colorTool,
                       Quantity_Color& outColor);

// Resolve RGBA color for a label, fallback to defaultCol if none
RGBA ResolveColorRGBA(const TDF_Label&              label,
                      const Handle(XCAFDoc_ShapeTool)& shapeTool,
                      const Handle(XCAFDoc_ColorTool)& colorTool,
                      const RGBA& defaultCol);

// Export single label as colored STEP
bool ExportShapeToSTEP(const TDF_Label&                    compLabel,
                       const Handle(XCAFDoc_ShapeTool)&    shapeTool,
                       const Handle(XCAFDoc_ColorTool)&    colorTool,
                       const std::string&                  stepFile);

// Collect components for assembly export (1 level below roots)
void CollectAssemblyComponentsShallow(
    const Handle(XCAFDoc_ShapeTool)& shapeTool,
    const TDF_LabelSequence&         roots,
    TDF_LabelSequence&               out);

// Collect leaf components (deep)
void CollectLeafComponentsDeep(
    const Handle(XCAFDoc_ShapeTool)& shapeTool,
    const TDF_LabelSequence&         roots,
    TDF_LabelSequence&               out);

// Pretty tree dump (full depth, instance + prototype)
void DumpAssemblyTreeDeep(
    const TDF_Label&                label,
    const Handle(XCAFDoc_ShapeTool)& shapeTool,
    const Handle(XCAFDoc_ColorTool)& colorTool,
    std::set<std::string>&          visited,
    int                              depth   = 0,
    bool                             isLast  = true,
    const std::string&               prefix  = "");
