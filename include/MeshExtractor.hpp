#pragma once

#include "Common.hpp"

#include <TopoDS_Shape.hxx>

// Triangulate a shape + extract edges, accumulating into
// triBuckets / edgeBuckets using MaterialRegistry for colors.
void MeshShape(const TopoDS_Shape& root,
               const RGBA&          shapeColor,
               MaterialRegistry&    matReg,
               std::vector<TriBucket>& triBuckets,
               std::vector<EdgeBucket>& edgeBuckets);
