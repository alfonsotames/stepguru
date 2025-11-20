#pragma once

#include <string>
#include <TDF_Label.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>

/// High-level JSON exporter: produces a full assembly definition+instance JSON.
namespace JsonExporter {

    bool Export(
        const TDF_Label& rootLabel,
        const Handle(XCAFDoc_ShapeTool)& shapeTool,
        const Handle(XCAFDoc_ColorTool)& colorTool,
        const std::string& outputJson);

}
