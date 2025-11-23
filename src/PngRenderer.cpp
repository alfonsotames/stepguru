#include "PngRenderer.hpp"

#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <Xw_Window.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <V3d_Viewer.hxx>
#include <V3d_View.hxx>
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <Image_AlienPixMap.hxx>
#include <TCollection_AsciiString.hxx>
#include <Graphic3d_Vec2.hxx>
#include <Graphic3d_RenderingParams.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Aspect_TypeOfLine.hxx>

#include <iostream>

/*
    Note that in Linux this will need to install:
    
    $ sudo apt-get install xvfb

    And then run the server:

    $ Xvfb :99 -screen 0 1024x768x24 &
    $ export DISPLAY=:99

*/
bool RenderPNG(const std::vector<TopoDS_Shape>& shapes,
               const std::vector<RGBA>&         colors,
               const std::string&               pngFile)
{
    std::cout << "Rendering PNG with OpenCascade for " << pngFile << " ...\n";

    if (shapes.empty()) {
        std::cerr << "❌ No shape available for rendering.\n";
        return false;
    }

    try {
        Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
        Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display, Standard_True);

        driver->ChangeOptions().buffersNoSwap = Standard_True;
        driver->ChangeOptions().swapInterval  = 0;

        Handle(V3d_Viewer) viewer = new V3d_Viewer(driver);
        viewer->SetDefaultViewProj(V3d_XposYnegZpos);
        viewer->SetDefaultShadingModel(Graphic3d_TypeOfShadingModel_Pbr);
        viewer->SetDefaultVisualization(V3d_ZBUFFER);

        Handle(AIS_InteractiveContext) ctx = new AIS_InteractiveContext(viewer);

        Graphic3d_Vec2i winSize(512, 512);

        Handle(Xw_Window) window = new Xw_Window(display, "Offscreen", 0, 0,
                                                 winSize.x(), winSize.y());
        window->SetVirtual(true);

        Handle(V3d_View) view = new V3d_View(viewer);
        view->SetWindow(window);

        Graphic3d_RenderingParams& params = view->ChangeRenderingParams();
        params.IsAntialiasingEnabled  = Standard_True;
        params.NbMsaaSamples          = 16;
        params.RenderResolutionScale  = 4.0f;
        params.IsShadowEnabled        = Standard_True;

        viewer->SetDefaultLights();
        viewer->SetLightOn();
        view->SetBackgroundColor(Quantity_NOC_WHITE);
        view->SetProj(V3d_XposYnegZpos);

        RGBA defaultGray{0.7f,0.7f,0.7f,1.0f};

        for (std::size_t i=0; i<shapes.size(); ++i) {
            const TopoDS_Shape& s = shapes[i];
            if (s.IsNull()) continue;

            RGBA col = (i < colors.size()) ? colors[i] : defaultGray;
            if (col.r == 0.0f && col.g == 0.0f && col.b == 0.0f)
                col = defaultGray;

            Quantity_Color qc(col.r, col.g, col.b, Quantity_TOC_RGB);
            float brightnessPNG =
                0.299f*col.r + 0.587f*col.g + 0.114f*col.b;
            Quantity_Color edgeColor = (brightnessPNG > 0.5f)
                ? Quantity_Color(0.1, 0.1, 0.1, Quantity_TOC_RGB)
                : Quantity_Color(0.9, 0.9, 0.9, Quantity_TOC_RGB);

            Handle(AIS_Shape) aisShape = new AIS_Shape(s);
            aisShape->SetColor(qc);
            aisShape->Attributes()->SetShadingAspect(new Prs3d_ShadingAspect());
            aisShape->Attributes()->ShadingAspect()->SetColor(qc);

            Handle(Prs3d_Drawer) drawer = aisShape->Attributes();
            drawer->SetFaceBoundaryDraw(Standard_True);
            drawer->SetWireAspect(new Prs3d_LineAspect(edgeColor, Aspect_TOL_SOLID, 1.0));
            drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(edgeColor, Aspect_TOL_SOLID, 1.0));
            drawer->SetLineAspect(new Prs3d_LineAspect(edgeColor, Aspect_TOL_SOLID, 1.0));

            ctx->Display(aisShape, Standard_False);
            ctx->SetDisplayMode(aisShape, AIS_Shaded, Standard_False);
            ctx->IsoOnTriangulation(Standard_True, aisShape);
        }

        ctx->UpdateCurrentViewer();
        view->FitAll();
        view->ZFitAll();
        view->Redraw();

        Image_AlienPixMap pixmap;
        pixmap.InitZero(Image_Format_RGB, winSize.x(), winSize.y());

        TCollection_AsciiString pngName(pngFile.c_str());
        if (view->ToPixMap(pixmap, winSize.x(), winSize.y(),
                           Graphic3d_BT_RGB, Standard_False))
        {
            if (pixmap.Save(pngName.ToCString())) {
                std::cout << "🖼️  Anti-aliased PNG saved as "
                          << pngName.ToCString() << std::endl;
                return true;
            } else {
                std::cerr << "❌ Failed to save PNG file.\n";
            }
        } else {
            std::cerr << "❌ Failed to render scene to pixmap.\n";
        }
    } catch (const Standard_Failure& e) {
        std::cerr << "Rendering error: " << e.GetMessageString() << std::endl;
    } catch (...) {
        std::cerr << "Rendering error: unknown exception\n";
    }

    return false;
}
