#pragma once
#include "app_types.h"

typedef enum {
    PALETTE_MAP_INTENSITY = 1,
    PALETTE_MAP_HORIZONTAL = 2,
    PALETTE_MAP_VERTICAL = 3,
    PALETTE_MAP_DIAGONAL = 4,
    PALETTE_MAP_RADIAL = 5,
    PALETTE_MAP_RADIAL_INVERSE = 6,
    PALETTE_MAP_DIAGONAL_REVERSE = 7,
} PaletteMappingMode;

typedef struct {
    float low[3];
    float mid[3];
    float high[3];
    float midpoint;
    float source_mix;
    int mode;
    float spatial_mix;
} PaletteParameters;


const PaletteParameters *palette_parameters(Palette palette);
const char *palette_name(Palette palette);
