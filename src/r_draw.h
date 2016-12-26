
#pragma once

#include "r_defs.h"

struct FSWColormap;
struct FLightNode;
struct TriLight;

EXTERN_CVAR(Bool, r_multithreaded);
EXTERN_CVAR(Bool, r_magfilter);
EXTERN_CVAR(Bool, r_minfilter);
EXTERN_CVAR(Bool, r_mipmap);
EXTERN_CVAR(Float, r_lod_bias);
EXTERN_CVAR(Int, r_drawfuzz);
EXTERN_CVAR(Bool, r_drawtrans);
EXTERN_CVAR(Float, transsouls);
EXTERN_CVAR(Bool, r_dynlights);

namespace swrenderer
{
	struct vissprite_t;
	struct visplane_light;

	struct ShadeConstants
	{
		uint16_t light_alpha;
		uint16_t light_red;
		uint16_t light_green;
		uint16_t light_blue;
		uint16_t fade_alpha;
		uint16_t fade_red;
		uint16_t fade_green;
		uint16_t fade_blue;
		uint16_t desaturate;
		bool simple_shade;
	};

	extern double dc_texturemid;
	extern FLightNode *dc_light_list;
	extern visplane_light *ds_light_list;

	namespace drawerargs
	{
		extern int dc_pitch;
		extern lighttable_t *dc_colormap;
		extern FSWColormap *dc_fcolormap;
		extern ShadeConstants dc_shade_constants;
		extern fixed_t dc_light;
		extern int dc_x;
		extern int dc_yl;
		extern int dc_yh;
		extern fixed_t dc_iscale;
		extern fixed_t dc_texturefrac;
		extern uint32_t dc_textureheight;
		extern int dc_color;
		extern uint32_t dc_srccolor;
		extern uint32_t dc_srccolor_bgra;
		extern uint32_t *dc_srcblend;
		extern uint32_t *dc_destblend;
		extern fixed_t dc_srcalpha;
		extern fixed_t dc_destalpha;
		extern const uint8_t *dc_source;
		extern const uint8_t *dc_source2;
		extern uint32_t dc_texturefracx;
		extern uint8_t *dc_translation;
		extern uint8_t *dc_dest;
		extern uint8_t *dc_destorg;
		extern int dc_destheight;
		extern int dc_count;
		extern FVector3 dc_viewpos;
		extern FVector3 dc_viewpos_step;
		extern TriLight *dc_lights;
		extern int dc_num_lights;

		extern bool drawer_needs_pal_input;

		extern uint32_t dc_wall_texturefrac[4];
		extern uint32_t dc_wall_iscale[4];
		extern uint8_t *dc_wall_colormap[4];
		extern fixed_t dc_wall_light[4];
		extern const uint8_t *dc_wall_source[4];
		extern const uint8_t *dc_wall_source2[4];
		extern uint32_t dc_wall_texturefracx[4];
		extern uint32_t dc_wall_sourceheight[4];
		extern int dc_wall_fracbits;

		extern int ds_y;
		extern int ds_x1;
		extern int ds_x2;
		extern lighttable_t * ds_colormap;
		extern FSWColormap *ds_fcolormap;
		extern ShadeConstants ds_shade_constants;
		extern dsfixed_t ds_light;
		extern dsfixed_t ds_xfrac;
		extern dsfixed_t ds_yfrac;
		extern dsfixed_t ds_xstep;
		extern dsfixed_t ds_ystep;
		extern int ds_xbits;
		extern int ds_ybits;
		extern fixed_t ds_alpha;
		extern double ds_lod;
		extern const uint8_t *ds_source;
		extern bool ds_source_mipmapped;
		extern int ds_color;

		extern unsigned int dc_tspans[4][MAXHEIGHT];
		extern unsigned int *dc_ctspan[4];
		extern unsigned int *horizspan[4];
	}

	extern int ylookup[MAXHEIGHT];
	extern uint8_t shadetables[/*NUMCOLORMAPS*16*256*/];
	extern FDynamicColormap ShadeFakeColormap[16];
	extern uint8_t identitymap[256];
	extern FDynamicColormap identitycolormap;

	// Spectre/Invisibility.
	#define FUZZTABLE 50
	extern int fuzzoffset[FUZZTABLE + 1];
	extern int fuzzpos;
	extern int fuzzviewheight;

	extern bool r_swtruecolor;

	void R_InitColumnDrawers();
	void R_InitShadeMaps();
	void R_InitFuzzTable(int fuzzoff);

	bool R_SetPatchStyle(FRenderStyle style, fixed_t alpha, int translation, uint32_t color);
	bool R_SetPatchStyle(FRenderStyle style, float alpha, int translation, uint32_t color);
	void R_FinishSetPatchStyle(); // Call this after finished drawing the current thing, in case its style was STYLE_Shade
	bool R_GetTransMaskDrawers(void(**drawCol1)(), void(**drawCol4)());

	const uint8_t *R_GetColumn(FTexture *tex, int col);
	
	void R_DrawColumn();
	void R_DrawFuzzColumn();
	void R_DrawTranslatedColumn();
	void R_DrawShadedColumn();
	void R_FillColumn();
	void R_FillAddColumn();
	void R_FillAddClampColumn();
	void R_FillSubClampColumn();
	void R_FillRevSubClampColumn();
	void R_DrawAddColumn();
	void R_DrawTlatedAddColumn();
	void R_DrawAddClampColumn();
	void R_DrawAddClampTranslatedColumn();
	void R_DrawSubClampColumn();
	void R_DrawSubClampTranslatedColumn();
	void R_DrawRevSubClampColumn();
	void R_DrawRevSubClampTranslatedColumn();
	void R_DrawSpan();
	void R_DrawSpanMasked();
	void R_DrawSpanTranslucent();
	void R_DrawSpanMaskedTranslucent();
	void R_DrawSpanAddClamp();
	void R_DrawSpanMaskedAddClamp();
	void R_FillSpan();
	void R_DrawTiltedSpan(int y, int x1, int x2, const FVector3 &plane_sz, const FVector3 &plane_su, const FVector3 &plane_sv, bool plane_shade, int planeshade, float planelightfloat, fixed_t pviewx, fixed_t pviewy);
	void R_DrawColoredSpan(int y, int x1, int x2);
	void R_SetupDrawSlab(FSWColormap *base_colormap, float light, int shade);
	void R_DrawSlab(int dx, fixed_t v, int dy, fixed_t vi, const uint8_t *vptr, uint8_t *p);
	void R_DrawFogBoundary(int x1, int x2, short *uclip, short *dclip);
	void R_FillSpan();

	void R_DrawWallCol1();
	void R_DrawWallCol4();
	void R_DrawWallMaskedCol1();
	void R_DrawWallMaskedCol4();
	void R_DrawWallAddCol1();
	void R_DrawWallAddCol4();
	void R_DrawWallAddClampCol1();
	void R_DrawWallAddClampCol4();
	void R_DrawWallSubClampCol1();
	void R_DrawWallSubClampCol4();
	void R_DrawWallRevSubClampCol1();
	void R_DrawWallRevSubClampCol4();

	void R_DrawSingleSkyCol1(uint32_t solid_top, uint32_t solid_bottom);
	void R_DrawSingleSkyCol4(uint32_t solid_top, uint32_t solid_bottom);
	void R_DrawDoubleSkyCol1(uint32_t solid_top, uint32_t solid_bottom);
	void R_DrawDoubleSkyCol4(uint32_t solid_top, uint32_t solid_bottom);

	// Sets dc_colormap and dc_light to their appropriate values depending on the output format (pal vs true color)
	void R_SetColorMapLight(FSWColormap *base_colormap, float light, int shade);
	void R_SetDSColorMapLight(FSWColormap *base_colormap, float light, int shade);
	void R_SetTranslationMap(lighttable_t *translation);

	void R_SetupSpanBits(FTexture *tex);
	void R_SetSpanColormap(FDynamicColormap *colormap, int shade);
	void R_SetSpanSource(FTexture *tex);

	void R_MapTiltedPlane(int y, int x1);
	void R_MapColoredPlane(int y, int x1);
}
