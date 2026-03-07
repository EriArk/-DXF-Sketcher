
#define VERTEX_FLAG_SELECTED (1u << 0)
#define VERTEX_FLAG_HOVER (1u << 1)
#define VERTEX_FLAG_INACTIVE (1u << 2)
#define VERTEX_FLAG_CONSTRAINT (1u << 3)
#define VERTEX_FLAG_CONSTRUCTION (1u << 4)
#define VERTEX_FLAG_HIGHLIGHT (1u << 5)
#define VERTEX_FLAG_SCREEN (1u << 6)
#define VERTEX_FLAG_LINE_THIN (1u << 7)
#define VERTEX_FLAG_ICON_NO_FLIP (1u << 8)
#define VERTEX_FLAG_LAYER_COLOR_SHIFT 9u
#define VERTEX_FLAG_LAYER_COLOR_MASK (0xFu << VERTEX_FLAG_LAYER_COLOR_SHIFT)
#define VERTEX_FLAG_LINE_WIDE (1u << 13)
#define VERTEX_FLAG_COLOR_MASK (VERTEX_FLAG_SELECTED | VERTEX_FLAG_HOVER | VERTEX_FLAG_INACTIVE | VERTEX_FLAG_CONSTRAINT | VERTEX_FLAG_CONSTRUCTION | VERTEX_FLAG_HIGHLIGHT)

#define FLAG_IS_SET(x, flag) (((x) & (flag)) != 0u)

layout (std140) uniform color_setup
{
    // keep in sync with base_renderer.cpp
	vec3 colors[64];
    uint peeled_picks[8];
};

vec3 get_color(uint flags)
{
    uint layer_color_index = (flags & VERTEX_FLAG_LAYER_COLOR_MASK) >> VERTEX_FLAG_LAYER_COLOR_SHIFT;
    if (layer_color_index == 1u)
        return vec3(229.0, 57.0, 53.0) / 255.0;
    if (layer_color_index == 2u)
        return vec3(251.0, 192.0, 45.0) / 255.0;
    if (layer_color_index == 3u)
        return vec3(67.0, 160.0, 71.0) / 255.0;
    if (layer_color_index == 4u)
        return vec3(0.0, 172.0, 193.0) / 255.0;
    if (layer_color_index == 5u)
        return vec3(30.0, 136.0, 229.0) / 255.0;
    if (layer_color_index == 6u)
        return vec3(142.0, 36.0, 170.0) / 255.0;
    if (layer_color_index == 7u)
        return vec3(17.0, 17.0, 17.0) / 255.0;
    if (layer_color_index == 8u)
        return vec3(245.0, 124.0, 0.0) / 255.0;
    if (layer_color_index == 9u)
        return vec3(109.0, 76.0, 65.0) / 255.0;
    if (layer_color_index == 10u)
        return vec3(94.0, 53.0, 177.0) / 255.0;
    if (layer_color_index == 11u)
        return vec3(84.0, 110.0, 122.0) / 255.0;
    if (layer_color_index == 12u)
        return vec3(158.0, 158.0, 158.0) / 255.0;

    return colors[flags & VERTEX_FLAG_COLOR_MASK];
}

float get_depth_shift(uint flags)
{
    if((flags & (VERTEX_FLAG_SELECTED)) != uint(0))
        return -0.0005;
    return 0.;
}

float get_select_alpha(uint flags)
{
    if((flags & (VERTEX_FLAG_SELECTED)) != uint(0))
        return 1.;
    else
        return 0.;
}

float get_line_alpha(uint flags)
{
    if ((flags & VERTEX_FLAG_LINE_WIDE) != uint(0))
        return 0.24;
    return 1.0;
}

bool test_peel(uint pick)
{
    for(int i = 0; i < peeled_picks.length(); i++)
	{
		if(peeled_picks[i] == 0u)
			return false;
		if(peeled_picks[i] == pick)
			return true;
	}
	return false;
}

struct GlyphInfo
{
  float x;
  float y;
  float w;
  float h;  
};

GlyphInfo unpack_glyph_info(uint bits)
{
    GlyphInfo info;
    info.x = (bits>>22)&uint(0x3ff);
	info.y = (bits>>12)&uint(0x3ff);
	info.w = (bits>>6)&uint(0x3f);
	info.h = (bits>>0)&uint(0x3f);
    return info;
}
