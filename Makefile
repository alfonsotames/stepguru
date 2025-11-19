# ---------------------------------------------------------
# Cross-platform Makefile for Linux + macOS (Intel/ARM)
# Builds the 'stepguru' program using OpenCascade.
# ---------------------------------------------------------

# Project
TARGET = stepguru

SRC_DIR   = src
INC_DIR   = include
BUILD_DIR = build

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

CXX      = g++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -I$(INC_DIR)

# ---------------------------------------------------------
# Detect platform
# ---------------------------------------------------------
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Darwin)
    # ============================================
    # macOS SETTINGS
    # ============================================
    # Homebrew OpenCascade installation
    OCCT_PREFIX := $(shell brew --prefix opencascade)
    OCCT_INC    := $(OCCT_PREFIX)/include/opencascade
    OCCT_LIBDIR := $(OCCT_PREFIX)/lib

    CXXFLAGS += -I$(OCCT_INC)
    LDFLAGS  += -L$(OCCT_LIBDIR)

    # macOS OpenGL + Cocoa frameworks
    FRAMEWORKS = -framework OpenGL -framework Cocoa -framework CoreGraphics

    # Required OCCT libs
    OCCT_LIBS = \
        -lTKernel -lTKMath -lTKBRep -lTKGeomBase -lTKGeomAlgo -lTKShHealing \
        -lTKTopAlgo -lTKPrim -lTKG3d -lTKG2d \
        -lTKMesh -lTKService \
        -lTKOpenGl -lTKV3d -lTKAIS \
        -lTKXCAF -lTKCAF -lTKXDESTEP -lTKSTEP -lTKSTEPAttr \
        -lTKSTEP209 -lTKSTEPBase

    LDLIBS = $(OCCT_LIBS) $(FRAMEWORKS)

else
    # ============================================
    # Linux SETTINGS
    # ============================================
    OCCT_INC    = /usr/local/include/opencascade
    OCCT_LIBDIR = /usr/local/lib

    CXXFLAGS += -I$(OCCT_INC)
    LDFLAGS  += -L$(OCCT_LIBDIR)

    # Linux OpenGL + X11
    LDLIBS = \
    -lTKernel \
    -lTKMath \
    -lTKBRep \
    -lTKG3d \
    -lTKG2d \
    -lTKPrim \
    -lTKShHealing \
    -lTKMesh \
    -lTKXSBase \
    -lTKDESTEP \
    -lTKXCAF \
    -lTKLCAF \
    -lTKCAF \
    -lTKDEGLTF \
    -lTKTopAlgo \
    -lTKGeomBase \
    -lTKV3d \
    -lTKService \
    -lTKOpenGl \
    -lTKCDF \
    -lpthread \
    -lGLU

endif

# ---------------------------------------------------------
# Build rules
# ---------------------------------------------------------

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $(TARGET)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
