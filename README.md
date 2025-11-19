# step2gltf
Utility for exploring ISO 10303 STEP files  (AP203 and AP 214) and convert to GLTF 2.0 / PNG using OpenCascade. Supports single part and assemblies dumping a nice assembly tree.

## Dependencies

OpenCascade 7.9.2



### Compiling step2gltf

To compile from source on OSX:
```
make
```

## Rendering gLTF

Open https://gltf-viewer.donmccurdy.com and upload result file.

## GLTF 2.0 Specification

https://github.com/KhronosGroup/glTF/blob/master/README.md

# Implementation Details

## Meshing algorithm

The algorithm of shape triangulation is provided by the functionality of BRepMesh_IncrementalMesh class, which adds a triangulation of the shape to its topological data structure. This triangulation is used to visualize the shape in shaded mode.

![Deflection parameters of BRepMesh_IncrementalMesh algorithm](https://www.opencascade.com/doc/occt-7.1.0/overview/html/modeling_algos_image056.png)

Linear deflection limits the distance between triangles and the face interior.

![Linear deflection](https://www.opencascade.com/doc/occt-7.1.0/overview/html/modeling_algos_image057.png)

Note that if a given value of linear deflection is less than shape tolerance then the algorithm will skip this value and will take into account the shape tolerance.

The application should provide deflection parameters to compute a satisfactory mesh. Angular deflection is relatively simple and allows using a default value (12-20 degrees). Linear deflection has an absolute meaning and the application should provide the correct value for its models. Giving small values may result in a too huge mesh (consuming a lot of memory, which results in a long computation time and slow rendering) while big values result in an ugly mesh.

For an application working in dimensions known in advance it can be reasonable to use the absolute linear deflection for all models. This provides meshes according to metrics and precision used in the application (for example, it it is known that the model will be stored in meters, 0.004 m is enough for most tasks).

Source: https://www.opencascade.com/doc/occt-7.1.0/overview/html/occt_user_guides__modeling_algos.html#occt_modalg_11_2
