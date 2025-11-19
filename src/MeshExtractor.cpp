#include "MeshExtractor.hpp"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS.hxx>
#include <Poly_Triangulation.hxx>
#include <TopLoc_Location.hxx>

#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_UniformDeflection.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <Standard_Failure.hxx>
#include <iostream>

void MeshShape(const TopoDS_Shape& root,
               const RGBA&          shapeColorIn,
               MaterialRegistry&    matReg,
               std::vector<TriBucket>& triBuckets,
               std::vector<EdgeBucket>& edgeBuckets)
{
    if (root.IsNull()) return;

    // Meshing tolerances
    const double linDefl  = 0.01;
    const double angDefl  = 0.10;
    const double edgeDeflBase = linDefl * 8.0;

    // Triangulate once for entire shape
    BRepMesh_IncrementalMesh mesh(root, linDefl, Standard_False, angDefl, Standard_True);
    mesh.Perform();

    // Default gray for shapes that are pure black
    RGBA shapeColor = shapeColorIn;
    if (shapeColor.r == 0.0f && shapeColor.g == 0.0f && shapeColor.b == 0.0f) {
        shapeColor = {0.7f, 0.7f, 0.7f, 1.0f};
    }

    // Edge color derived from brightness
    const float brightness =
        0.299f * shapeColor.r +
        0.587f * shapeColor.g +
        0.114f * shapeColor.b;

    RGBA edgeColor =
        (brightness > 0.5f)
        ? RGBA{0.1f,0.1f,0.1f,1.0f}
        : RGBA{0.9f,0.9f,0.9f,1.0f};

    int shapeMatIdx = matReg.getOrCreate(shapeColor);
    if (shapeMatIdx >= static_cast<int>(triBuckets.size()))
        triBuckets.resize(shapeMatIdx + 1);
    triBuckets[shapeMatIdx].materialIndex = shapeMatIdx;

    int edgeMatIdx = matReg.getOrCreate(edgeColor);
    if (edgeMatIdx >= static_cast<int>(edgeBuckets.size()))
        edgeBuckets.resize(edgeMatIdx + 1);
    edgeBuckets[edgeMatIdx].materialIndex = edgeMatIdx;

    // Faces → triangles
    for (TopExp_Explorer ex(root, TopAbs_FACE); ex.More(); ex.Next()) {
        TopoDS_Face face = TopoDS::Face(ex.Current());
        try {
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull() || tri->NbNodes() < 3 || tri->NbTriangles() < 1) continue;

            TriBucket& b = triBuckets[shapeMatIdx];
            std::size_t base = b.vertices.size();
            int n = tri->NbNodes();
            b.vertices.reserve(b.vertices.size() + static_cast<std::size_t>(n));
            b.normals .reserve(b.normals.size()  + static_cast<std::size_t>(n));

            std::vector<gp_Vec> acc(static_cast<std::size_t>(n), gp_Vec(0,0,0));

            for (int i=1; i<=n; ++i) {
                gp_Pnt p = tri->Node(i).Transformed(loc.Transformation());
                b.vertices.push_back({(float)p.X(), (float)p.Y(), (float)p.Z()});
            }

            bool rev = (face.Orientation() == TopAbs_REVERSED);
            for (int i=1; i<=tri->NbTriangles(); ++i) {
                Poly_Triangle t = tri->Triangle(i);
                int n1, n2, n3;
                t.Get(n1, n2, n3);
                if (rev) std::swap(n2, n3);

                gp_Pnt p1 = tri->Node(n1).Transformed(loc.Transformation());
                gp_Pnt p2 = tri->Node(n2).Transformed(loc.Transformation());
                gp_Pnt p3 = tri->Node(n3).Transformed(loc.Transformation());
                gp_Vec v1(p1, p2), v2(p1, p3);
                gp_Vec nrm = v1.Crossed(v2);
                if (nrm.Magnitude() > 1e-12) {
                    nrm.Normalize();
                }

                acc[static_cast<std::size_t>(n1-1)] += nrm;
                acc[static_cast<std::size_t>(n2-1)] += nrm;
                acc[static_cast<std::size_t>(n3-1)] += nrm;

                b.indices.push_back(static_cast<std::uint32_t>(base + static_cast<std::size_t>(n1-1)));
                b.indices.push_back(static_cast<std::uint32_t>(base + static_cast<std::size_t>(n2-1)));
                b.indices.push_back(static_cast<std::uint32_t>(base + static_cast<std::size_t>(n3-1)));
            }

            for (auto& v : acc) v.Normalize();
            for (const auto& v : acc) {
                b.normals.push_back({(float)v.X(), (float)v.Y(), (float)v.Z()});
            }
        } catch (const Standard_Failure& e) {
            std::cerr << "skip bad face: " << e.GetMessageString() << "\n";
        } catch (...) {
            std::cerr << "skip bad face (unknown error)\n";
        }
    }

    // Edges → polylines
    EdgeBucket& eB = edgeBuckets[edgeMatIdx];
    std::size_t baseEdge = eB.vertices.size();

    for (TopExp_Explorer ex(root, TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Edge e = TopoDS::Edge(ex.Current());
        try {
            BRepAdaptor_Curve c(e);

            Standard_Real len = 10.0;
            try {
                len = GCPnts_AbscissaPoint::Length(c);
            } catch (...) {}

            Standard_Real defl = edgeDeflBase;
            if (len < 5.0)       defl *= 0.25;
            else if (len < 50.0) defl *= 0.5;

            GCPnts_UniformDeflection s(c, defl);
            if (!s.IsDone() || s.NbPoints() < 2) continue;

            for (int i=1; i<=s.NbPoints(); ++i) {
                gp_Pnt p = s.Value(i);
                eB.vertices.push_back({(float)p.X(), (float)p.Y(), (float)p.Z()});
            }
            for (int i=0; i<s.NbPoints()-1; ++i) {
                eB.indices.push_back(static_cast<std::uint32_t>(baseEdge + i));
                eB.indices.push_back(static_cast<std::uint32_t>(baseEdge + i + 1));
            }
            baseEdge += static_cast<std::size_t>(s.NbPoints());
        } catch (const Standard_Failure& e) {
            std::cerr << "skip bad edge: " << e.GetMessageString() << "\n";
        } catch (...) {
            std::cerr << "skip bad edge (unknown error)\n";
        }
    }
}
