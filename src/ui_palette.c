#include "ui_palette.h"

static const PaletteParameters palette_table[CHRONO_PALETTE_COUNT] = {
    [PALETTE_GREEN] = {
        .low = {0.18f, 1.00f, 0.60f},
        .mid = {0.18f, 1.00f, 0.60f},
        .high = {0.18f, 1.00f, 0.60f},
        .midpoint = 0.50f,
        .source_mix = 0.12f,
        .mode = PALETTE_MAP_INTENSITY,
        .spatial_mix = 0.0f,
    },
    [PALETTE_AMBER] = {
        .low = {1.00f, 0.54f, 0.12f},
        .mid = {1.00f, 0.54f, 0.12f},
        .high = {1.00f, 0.54f, 0.12f},
        .midpoint = 0.50f,
        .source_mix = 0.12f,
        .mode = PALETTE_MAP_INTENSITY,
        .spatial_mix = 0.0f,
    },
    [PALETTE_ICE] = {
        .low = {0.45f, 0.82f, 1.00f},
        .mid = {0.45f, 0.82f, 1.00f},
        .high = {0.45f, 0.82f, 1.00f},
        .midpoint = 0.50f,
        .source_mix = 0.12f,
        .mode = PALETTE_MAP_INTENSITY,
        .spatial_mix = 0.0f,
    },
    [PALETTE_VECTOR] = {
        .low = {0.03f, 0.54f, 0.18f},
        .mid = {0.16f, 1.00f, 0.48f},
        .high = {0.92f, 1.00f, 0.95f},
        .midpoint = 0.70f,
        .source_mix = 0.03f,
        .mode = PALETTE_MAP_VERTICAL,
        .spatial_mix = 0.38f,
    },
    [PALETTE_AQUA] = {
        .low = {0.01f, 0.24f, 0.42f},
        .mid = {0.02f, 0.90f, 1.00f},
        .high = {0.86f, 1.00f, 1.00f},
        .midpoint = 0.58f,
        .source_mix = 0.03f,
        .mode = PALETTE_MAP_HORIZONTAL,
        .spatial_mix = 0.76f,
    },
    [PALETTE_SUNSET] = {
        .low = {0.72f, 0.03f, 0.01f},
        .mid = {1.00f, 0.34f, 0.03f},
        .high = {1.00f, 0.94f, 0.62f},
        .midpoint = 0.55f,
        .source_mix = 0.02f,
        .mode = PALETTE_MAP_HORIZONTAL,
        .spatial_mix = 0.78f,
    },
    [PALETTE_PLASMA] = {
        .low = {0.22f, 0.02f, 0.62f},
        .mid = {1.00f, 0.06f, 0.68f},
        .high = {0.46f, 0.94f, 1.00f},
        .midpoint = 0.50f,
        .source_mix = 0.02f,
        .mode = PALETTE_MAP_DIAGONAL,
        .spatial_mix = 0.84f,
    },
    [PALETTE_RADAR] = {
        .low = {0.02f, 0.16f, 0.78f},
        .mid = {0.04f, 1.00f, 0.30f},
        .high = {1.00f, 0.91f, 0.12f},
        .midpoint = 0.56f,
        .source_mix = 0.02f,
        .mode = PALETTE_MAP_RADIAL,
        .spatial_mix = 0.72f,
    },
    [PALETTE_AURORA] = {
        .low = {0.01f, 0.22f, 0.58f},
        .mid = {0.05f, 1.00f, 0.56f},
        .high = {0.72f, 0.16f, 1.00f},
        .midpoint = 0.52f,
        .source_mix = 0.02f,
        .mode = PALETTE_MAP_VERTICAL,
        .spatial_mix = 0.82f,
    },
    [PALETTE_CYBER] = {
        .low = {0.00f, 0.88f, 1.00f},
        .mid = {0.58f, 0.06f, 1.00f},
        .high = {1.00f, 0.04f, 0.26f},
        .midpoint = 0.50f,
        .source_mix = 0.02f,
        .mode = PALETTE_MAP_HORIZONTAL,
        .spatial_mix = 0.88f,
    },
    [PALETTE_INFERNO] = {
        .low = {0.54f, 0.01f, 0.02f},
        .mid = {1.00f, 0.24f, 0.01f},
        .high = {1.00f, 0.94f, 0.18f},
        .midpoint = 0.54f,
        .source_mix = 0.02f,
        .mode = PALETTE_MAP_RADIAL_INVERSE,
        .spatial_mix = 0.84f,
    },
    [PALETTE_GALAXY] = {
        .low = {0.02f, 0.04f, 0.34f},
        .mid = {0.58f, 0.05f, 1.00f},
        .high = {0.04f, 0.90f, 1.00f},
        .midpoint = 0.48f,
        .source_mix = 0.02f,
        .mode = PALETTE_MAP_DIAGONAL_REVERSE,
        .spatial_mix = 0.88f,
    },
    /* Neutral phosphor, blue/gold, rose/cream, olive/lilac, copper/teal. */
    [PALETTE_PAPER] = {{.64f,.68f,.72f}, {.88f,.86f,.80f}, {1,1,.96f}, .5f,.02f, PALETTE_MAP_INTENSITY,0},
    [PALETTE_COBALT] = {{.06f,.16f,.80f}, {.18f,.45f,1}, {1,.76f,.18f}, .54f,.02f, PALETTE_MAP_HORIZONTAL,.88f},
    [PALETTE_ROSE] = {{.60f,.06f,.24f}, {1,.32f,.48f}, {1,.94f,.75f}, .56f,.02f, PALETTE_MAP_VERTICAL,.76f},
    [PALETTE_OLIVE] = {{.28f,.35f,.04f}, {.75f,.85f,.24f}, {.82f,.62f,1}, .56f,.02f, PALETTE_MAP_DIAGONAL,.84f},
    [PALETTE_COPPER] = {{.02f,.52f,.50f}, {.88f,.36f,.13f}, {1,.83f,.55f}, .5f,.02f, PALETTE_MAP_RADIAL,.84f},
};

const PaletteParameters *palette_parameters(Palette palette)
{
    int index = (int)palette;
    if (index < 0 || index >= CHRONO_PALETTE_COUNT)
        index = PALETTE_GREEN;
    return &palette_table[index];
}

const char *palette_name(Palette palette)
{
    static const char *names[CHRONO_PALETTE_COUNT] = {
        "Green", "Amber", "Ice", "Vector", "Aqua", "Sunset", "Plasma", "Radar",
        "Aurora", "Cyber", "Inferno", "Galaxy", "Paper", "Cobalt", "Rose", "Olive", "Copper"
    };
    return names[CLAMP((int)palette, 0, CHRONO_PALETTE_COUNT - 1)];
}
