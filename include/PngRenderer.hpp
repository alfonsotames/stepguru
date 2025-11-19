#pragma once

#include "Common.hpp"

#include <TopoDS_Shape.hxx>
#include <string>
#include <vector>

// Render shapes + per-shape RGBA colors to a PNG file using OCCT AIS/V3d.
bool RenderPNG(const std::vector<TopoDS_Shape>& shapes,
               const std::vector<RGBA>&         colors,
               const std::string&               pngFile);
