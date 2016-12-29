/*
**  Triangle drawers
**  Copyright (c) 2016 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
*/

#include <stddef.h>
#include "templates.h"
#include "doomdef.h"
#include "i_system.h"
#include "w_wad.h"
#include "r_local.h"
#include "v_video.h"
#include "doomstat.h"
#include "st_stuff.h"
#include "g_game.h"
#include "g_level.h"
#include "r_data/r_translate.h"
#include "v_palette.h"
#include "r_data/colormaps.h"
#include "r_poly_triangle.h"
#include "r_draw_rgba.h"

CVAR(Bool, r_debug_trisetup, false, 0);

int PolyTriangleDrawer::viewport_x;
int PolyTriangleDrawer::viewport_y;
int PolyTriangleDrawer::viewport_width;
int PolyTriangleDrawer::viewport_height;
int PolyTriangleDrawer::dest_pitch;
int PolyTriangleDrawer::dest_width;
int PolyTriangleDrawer::dest_height;
uint8_t *PolyTriangleDrawer::dest;
bool PolyTriangleDrawer::dest_bgra;
bool PolyTriangleDrawer::mirror;

void PolyTriangleDrawer::set_viewport(int x, int y, int width, int height, DCanvas *canvas)
{
	dest = (uint8_t*)canvas->GetBuffer();
	dest_width = canvas->GetWidth();
	dest_height = canvas->GetHeight();
	dest_pitch = canvas->GetPitch();
	dest_bgra = canvas->IsBgra();

	int offsetx = clamp(x, 0, dest_width);
	int offsety = clamp(y, 0, dest_height);
	int pixelsize = dest_bgra ? 4 : 1;

	viewport_x = x - offsetx;
	viewport_y = y - offsety;
	viewport_width = width;
	viewport_height = height;

	dest += (offsetx + offsety * dest_pitch) * pixelsize;
	dest_width = clamp(viewport_x + viewport_width, 0, dest_width - offsetx);
	dest_height = clamp(viewport_y + viewport_height, 0, dest_height - offsety);

	mirror = false;
}

void PolyTriangleDrawer::toggle_mirror()
{
	mirror = !mirror;
}

void PolyTriangleDrawer::draw(const PolyDrawArgs &args)
{
	DrawerCommandQueue::QueueCommand<DrawPolyTrianglesCommand>(args, mirror);
}

void PolyTriangleDrawer::draw_arrays(const PolyDrawArgs &drawargs, WorkerThreadData *thread)
{
	if (drawargs.vcount < 3)
		return;

	auto llvm = Drawers::Instance();
	
	PolyDrawFuncPtr drawfuncs[4];
	int num_drawfuncs = 0;
	
	drawfuncs[num_drawfuncs++] = drawargs.subsectorTest ? &ScreenTriangle::SetupSubsector : &ScreenTriangle::SetupNormal;

	if (!r_debug_trisetup) // For profiling how much time is spent in setup vs drawal
	{
		int bmode = (int)drawargs.blendmode;
		if (drawargs.writeColor && drawargs.texturePixels)
			drawfuncs[num_drawfuncs++] = dest_bgra ? llvm->TriDraw32[bmode] : llvm->TriDraw8[bmode];
		else if (drawargs.writeColor)
			drawfuncs[num_drawfuncs++] = dest_bgra ? llvm->TriFill32[bmode] : llvm->TriFill8[bmode];
	}

	if (drawargs.writeStencil)
		drawfuncs[num_drawfuncs++] = &ScreenTriangle::StencilWrite;

	if (drawargs.writeSubsector)
		drawfuncs[num_drawfuncs++] = &ScreenTriangle::SubsectorWrite;

	TriDrawTriangleArgs args;
	args.dest = dest;
	args.pitch = dest_pitch;
	args.clipleft = 0;
	args.clipright = dest_width;
	args.cliptop = 0;
	args.clipbottom = dest_height;
	args.texturePixels = drawargs.texturePixels;
	args.textureWidth = drawargs.textureWidth;
	args.textureHeight = drawargs.textureHeight;
	args.translation = drawargs.translation;
	args.uniforms = &drawargs.uniforms;
	args.stencilTestValue = drawargs.stenciltestvalue;
	args.stencilWriteValue = drawargs.stencilwritevalue;
	args.stencilPitch = PolyStencilBuffer::Instance()->BlockWidth();
	args.stencilValues = PolyStencilBuffer::Instance()->Values();
	args.stencilMasks = PolyStencilBuffer::Instance()->Masks();
	args.subsectorGBuffer = PolySubsectorGBuffer::Instance()->Values();
	args.colormaps = drawargs.colormaps;
	args.RGB256k = RGB256k.All;
	args.BaseColors = (const uint8_t *)GPalette.BaseColors;

	bool ccw = drawargs.ccw;
	const TriVertex *vinput = drawargs.vinput;
	int vcount = drawargs.vcount;

	ShadedTriVertex vert[3];
	if (drawargs.mode == TriangleDrawMode::Normal)
	{
		for (int i = 0; i < vcount / 3; i++)
		{
			for (int j = 0; j < 3; j++)
				vert[j] = shade_vertex(*drawargs.objectToClip, drawargs.clipPlane, *(vinput++));
			draw_shaded_triangle(vert, ccw, &args, thread, drawfuncs, num_drawfuncs);
		}
	}
	else if (drawargs.mode == TriangleDrawMode::Fan)
	{
		vert[0] = shade_vertex(*drawargs.objectToClip, drawargs.clipPlane, *(vinput++));
		vert[1] = shade_vertex(*drawargs.objectToClip, drawargs.clipPlane, *(vinput++));
		for (int i = 2; i < vcount; i++)
		{
			vert[2] = shade_vertex(*drawargs.objectToClip, drawargs.clipPlane, *(vinput++));
			draw_shaded_triangle(vert, ccw, &args, thread, drawfuncs, num_drawfuncs);
			vert[1] = vert[2];
		}
	}
	else // TriangleDrawMode::Strip
	{
		vert[0] = shade_vertex(*drawargs.objectToClip, drawargs.clipPlane, *(vinput++));
		vert[1] = shade_vertex(*drawargs.objectToClip, drawargs.clipPlane, *(vinput++));
		for (int i = 2; i < vcount; i++)
		{
			vert[2] = shade_vertex(*drawargs.objectToClip, drawargs.clipPlane, *(vinput++));
			draw_shaded_triangle(vert, ccw, &args, thread, drawfuncs, num_drawfuncs);
			vert[0] = vert[1];
			vert[1] = vert[2];
			ccw = !ccw;
		}
	}
}

ShadedTriVertex PolyTriangleDrawer::shade_vertex(const TriMatrix &objectToClip, const float *clipPlane, const TriVertex &v)
{
	// Apply transform to get clip coordinates:
	ShadedTriVertex sv = objectToClip * v;

	// Calculate gl_ClipDistance[0]
	sv.clipDistance0 = v.x * clipPlane[0] + v.y * clipPlane[1] + v.z * clipPlane[2] + v.w * clipPlane[3];

	return sv;
}

void PolyTriangleDrawer::draw_shaded_triangle(const ShadedTriVertex *vert, bool ccw, TriDrawTriangleArgs *args, WorkerThreadData *thread, PolyDrawFuncPtr *drawfuncs, int num_drawfuncs)
{
	// Cull, clip and generate additional vertices as needed
	TriVertex clippedvert[max_additional_vertices];
	int numclipvert;
	clipedge(vert, clippedvert, numclipvert);

	// Map to 2D viewport:
	for (int j = 0; j < numclipvert; j++)
	{
		auto &v = clippedvert[j];

		// Calculate normalized device coordinates:
		v.w = 1.0f / v.w;
		v.x *= v.w;
		v.y *= v.w;
		v.z *= v.w;

		// Apply viewport scale to get screen coordinates:
		v.x = viewport_x + viewport_width * (1.0f + v.x) * 0.5f;
		v.y = viewport_y + viewport_height * (1.0f - v.y) * 0.5f;
	}

	// Keep varyings in -128 to 128 range if possible
	if (numclipvert > 0)
	{
		for (int j = 0; j < TriVertex::NumVarying; j++)
		{
			float newOrigin = floorf(clippedvert[0].varying[j] * 0.1f) * 10.0f;
			for (int i = 0; i < numclipvert; i++)
			{
				clippedvert[i].varying[j] -= newOrigin;
			}
		}
	}

	// Draw screen triangles
	if (ccw)
	{
		for (int i = numclipvert; i > 1; i--)
		{
			args->v1 = &clippedvert[numclipvert - 1];
			args->v2 = &clippedvert[i - 1];
			args->v3 = &clippedvert[i - 2];
			
			for (int j = 0; j < num_drawfuncs; j++)
				drawfuncs[j](args, thread);
		}
	}
	else
	{
		for (int i = 2; i < numclipvert; i++)
		{
			args->v1 = &clippedvert[0];
			args->v2 = &clippedvert[i - 1];
			args->v3 = &clippedvert[i];
			
			for (int j = 0; j < num_drawfuncs; j++)
				drawfuncs[j](args, thread);
		}
	}
}

bool PolyTriangleDrawer::cullhalfspace(float clipdistance1, float clipdistance2, float &t1, float &t2)
{
	if (clipdistance1 < 0.0f && clipdistance2 < 0.0f)
		return true;

	if (clipdistance1 < 0.0f)
		t1 = MAX(-clipdistance1 / (clipdistance2 - clipdistance1), 0.0f);
	else
		t1 = 0.0f;

	if (clipdistance2 < 0.0f)
		t2 = MIN(1.0f + clipdistance2 / (clipdistance1 - clipdistance2), 1.0f);
	else
		t2 = 1.0f;

	return false;
}

void PolyTriangleDrawer::clipedge(const ShadedTriVertex *verts, TriVertex *clippedvert, int &numclipvert)
{
	// Clip and cull so that the following is true for all vertices:
	// -v.w <= v.x <= v.w
	// -v.w <= v.y <= v.w
	// -v.w <= v.z <= v.w
	
	// use barycentric weights while clipping vertices
	float weights[max_additional_vertices * 3 * 2];
	for (int i = 0; i < 3; i++)
	{
		weights[i * 3 + 0] = 0.0f;
		weights[i * 3 + 1] = 0.0f;
		weights[i * 3 + 2] = 0.0f;
		weights[i * 3 + i] = 1.0f;
	}
	
	// halfspace clip distances
	static const int numclipdistances = 7;
	float clipdistance[numclipdistances * 3];
	for (int i = 0; i < 3; i++)
	{
		const auto &v = verts[i];
		clipdistance[i * numclipdistances + 0] = v.x + v.w;
		clipdistance[i * numclipdistances + 1] = v.w - v.x;
		clipdistance[i * numclipdistances + 2] = v.y + v.w;
		clipdistance[i * numclipdistances + 3] = v.w - v.y;
		clipdistance[i * numclipdistances + 4] = v.z + v.w;
		clipdistance[i * numclipdistances + 5] = v.w - v.z;
		clipdistance[i * numclipdistances + 6] = v.clipDistance0;
	}
	
	// Clip against each halfspace
	float *input = weights;
	float *output = weights + max_additional_vertices * 3;
	int inputverts = 3;
	int outputverts = 0;
	for (int p = 0; p < numclipdistances; p++)
	{
		// Clip each edge
		outputverts = 0;
		for (int i = 0; i < inputverts; i++)
		{
			int j = (i + 1) % inputverts;
			float clipdistance1 =
				clipdistance[0 * numclipdistances + p] * input[i * 3 + 0] +
				clipdistance[1 * numclipdistances + p] * input[i * 3 + 1] +
				clipdistance[2 * numclipdistances + p] * input[i * 3 + 2];

			float clipdistance2 =
				clipdistance[0 * numclipdistances + p] * input[j * 3 + 0] +
				clipdistance[1 * numclipdistances + p] * input[j * 3 + 1] +
				clipdistance[2 * numclipdistances + p] * input[j * 3 + 2];
				
			float t1, t2;
			if (!cullhalfspace(clipdistance1, clipdistance2, t1, t2) && outputverts + 1 < max_additional_vertices)
			{
				// add t1 vertex
				for (int k = 0; k < 3; k++)
					output[outputverts * 3 + k] = input[i * 3 + k] * (1.0f - t1) + input[j * 3 + k] * t1;
				outputverts++;
			
				if (t2 != 1.0f && t2 > t1)
				{
					// add t2 vertex
					for (int k = 0; k < 3; k++)
						output[outputverts * 3 + k] = input[i * 3 + k] * (1.0f - t2) + input[j * 3 + k] * t2;
					outputverts++;
				}
			}
		}
		std::swap(input, output);
		std::swap(inputverts, outputverts);
		if (inputverts == 0)
			break;
	}
	
	// Convert barycentric weights to actual vertices
	numclipvert = inputverts;
	for (int i = 0; i < numclipvert; i++)
	{
		auto &v = clippedvert[i];
		memset(&v, 0, sizeof(TriVertex));
		for (int w = 0; w < 3; w++)
		{
			float weight = input[i * 3 + w];
			v.x += verts[w].x * weight;
			v.y += verts[w].y * weight;
			v.z += verts[w].z * weight;
			v.w += verts[w].w * weight;
			for (int iv = 0; iv < TriVertex::NumVarying; iv++)
				v.varying[iv] += verts[w].varying[iv] * weight;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////

DrawPolyTrianglesCommand::DrawPolyTrianglesCommand(const PolyDrawArgs &args, bool mirror)
	: args(args)
{
	if (mirror)
		this->args.ccw = !this->args.ccw;
}

void DrawPolyTrianglesCommand::Execute(DrawerThread *thread)
{
	WorkerThreadData thread_data;
	thread_data.core = thread->core;
	thread_data.num_cores = thread->num_cores;
	thread_data.pass_start_y = thread->pass_start_y;
	thread_data.pass_end_y = thread->pass_end_y;
	thread_data.FullSpans = thread->FullSpansBuffer.data();
	thread_data.PartialBlocks = thread->PartialBlocksBuffer.data();

	PolyTriangleDrawer::draw_arrays(args, &thread_data);
}

FString DrawPolyTrianglesCommand::DebugInfo()
{
	FString blendmodestr;
	switch (args.blendmode)
	{
	default: blendmodestr = "Unknown"; break;
	case TriBlendMode::Copy: blendmodestr = "Copy"; break;
	case TriBlendMode::AlphaBlend: blendmodestr = "AlphaBlend"; break;
	case TriBlendMode::AddSolid: blendmodestr = "AddSolid"; break;
	case TriBlendMode::Add: blendmodestr = "Add"; break;
	case TriBlendMode::Sub: blendmodestr = "Sub"; break;
	case TriBlendMode::RevSub: blendmodestr = "RevSub"; break;
	case TriBlendMode::Stencil: blendmodestr = "Stencil"; break;
	case TriBlendMode::Shaded: blendmodestr = "Shaded"; break;
	case TriBlendMode::TranslateCopy: blendmodestr = "TranslateCopy"; break;
	case TriBlendMode::TranslateAlphaBlend: blendmodestr = "TranslateAlphaBlend"; break;
	case TriBlendMode::TranslateAdd: blendmodestr = "TranslateAdd"; break;
	case TriBlendMode::TranslateSub: blendmodestr = "TranslateSub"; break;
	case TriBlendMode::TranslateRevSub: blendmodestr = "TranslateRevSub"; break;
	case TriBlendMode::AddSrcColorOneMinusSrcColor: blendmodestr = "AddSrcColorOneMinusSrcColor"; break;
	}

	FString info;
	info.Format("DrawPolyTriangles: blend mode = %s, color = %d, light = %d, textureWidth = %d, textureHeight = %d, texture = %s, translation = %s, colormaps = %s",
		blendmodestr.GetChars(), args.uniforms.color, args.uniforms.light, args.textureWidth, args.textureHeight,
		args.texturePixels ? "ptr" : "null", args.translation ? "ptr" : "null", args.colormaps ? "ptr" : "null");
	return info;
}

/////////////////////////////////////////////////////////////////////////////

TriMatrix TriMatrix::null()
{
	TriMatrix m;
	memset(m.matrix, 0, sizeof(m.matrix));
	return m;
}

TriMatrix TriMatrix::identity()
{
	TriMatrix m = null();
	m.matrix[0] = 1.0f;
	m.matrix[5] = 1.0f;
	m.matrix[10] = 1.0f;
	m.matrix[15] = 1.0f;
	return m;
}

TriMatrix TriMatrix::translate(float x, float y, float z)
{
	TriMatrix m = identity();
	m.matrix[0 + 3 * 4] = x;
	m.matrix[1 + 3 * 4] = y;
	m.matrix[2 + 3 * 4] = z;
	return m;
}

TriMatrix TriMatrix::scale(float x, float y, float z)
{
	TriMatrix m = null();
	m.matrix[0 + 0 * 4] = x;
	m.matrix[1 + 1 * 4] = y;
	m.matrix[2 + 2 * 4] = z;
	m.matrix[3 + 3 * 4] = 1;
	return m;
}

TriMatrix TriMatrix::rotate(float angle, float x, float y, float z)
{
	float c = cosf(angle);
	float s = sinf(angle);
	TriMatrix m = null();
	m.matrix[0 + 0 * 4] = (x*x*(1.0f - c) + c);
	m.matrix[0 + 1 * 4] = (x*y*(1.0f - c) - z*s);
	m.matrix[0 + 2 * 4] = (x*z*(1.0f - c) + y*s);
	m.matrix[1 + 0 * 4] = (y*x*(1.0f - c) + z*s);
	m.matrix[1 + 1 * 4] = (y*y*(1.0f - c) + c);
	m.matrix[1 + 2 * 4] = (y*z*(1.0f - c) - x*s);
	m.matrix[2 + 0 * 4] = (x*z*(1.0f - c) - y*s);
	m.matrix[2 + 1 * 4] = (y*z*(1.0f - c) + x*s);
	m.matrix[2 + 2 * 4] = (z*z*(1.0f - c) + c);
	m.matrix[3 + 3 * 4] = 1.0f;
	return m;
}

TriMatrix TriMatrix::swapYZ()
{
	TriMatrix m = null();
	m.matrix[0 + 0 * 4] = 1.0f;
	m.matrix[1 + 2 * 4] = 1.0f;
	m.matrix[2 + 1 * 4] = -1.0f;
	m.matrix[3 + 3 * 4] = 1.0f;
	return m;
}

TriMatrix TriMatrix::perspective(float fovy, float aspect, float z_near, float z_far)
{
	float f = (float)(1.0 / tan(fovy * M_PI / 360.0));
	TriMatrix m = null();
	m.matrix[0 + 0 * 4] = f / aspect;
	m.matrix[1 + 1 * 4] = f;
	m.matrix[2 + 2 * 4] = (z_far + z_near) / (z_near - z_far);
	m.matrix[2 + 3 * 4] = (2.0f * z_far * z_near) / (z_near - z_far);
	m.matrix[3 + 2 * 4] = -1.0f;
	return m;
}

TriMatrix TriMatrix::frustum(float left, float right, float bottom, float top, float near, float far)
{
	float a = (right + left) / (right - left);
	float b = (top + bottom) / (top - bottom);
	float c = -(far + near) / (far - near);
	float d = -(2.0f * far) / (far - near);
	TriMatrix m = null();
	m.matrix[0 + 0 * 4] = 2.0f * near / (right - left);
	m.matrix[1 + 1 * 4] = 2.0f * near / (top - bottom);
	m.matrix[0 + 2 * 4] = a;
	m.matrix[1 + 2 * 4] = b;
	m.matrix[2 + 2 * 4] = c;
	m.matrix[2 + 3 * 4] = d;
	m.matrix[3 + 2 * 4] = -1;
	return m;
}

TriMatrix TriMatrix::worldToView()
{
	TriMatrix m = null();
	m.matrix[0 + 0 * 4] = (float)ViewSin;
	m.matrix[0 + 1 * 4] = (float)-ViewCos;
	m.matrix[1 + 2 * 4] = 1.0f;
	m.matrix[2 + 0 * 4] = (float)-ViewCos;
	m.matrix[2 + 1 * 4] = (float)-ViewSin;
	m.matrix[3 + 3 * 4] = 1.0f;
	return m * translate((float)-ViewPos.X, (float)-ViewPos.Y, (float)-ViewPos.Z);
}

TriMatrix TriMatrix::viewToClip()
{
	float near = 5.0f;
	float far = 65536.0f;
	float width = (float)(FocalTangent * near);
	float top = (float)(swrenderer::CenterY / swrenderer::InvZtoScale * near);
	float bottom = (float)(top - viewheight / swrenderer::InvZtoScale * near);
	return frustum(-width, width, bottom, top, near, far);
}

TriMatrix TriMatrix::operator*(const TriMatrix &mult) const
{
	TriMatrix result;
	for (int x = 0; x < 4; x++)
	{
		for (int y = 0; y < 4; y++)
		{
			result.matrix[x + y * 4] =
				matrix[0 * 4 + x] * mult.matrix[y * 4 + 0] +
				matrix[1 * 4 + x] * mult.matrix[y * 4 + 1] +
				matrix[2 * 4 + x] * mult.matrix[y * 4 + 2] +
				matrix[3 * 4 + x] * mult.matrix[y * 4 + 3];
		}
	}
	return result;
}

ShadedTriVertex TriMatrix::operator*(TriVertex v) const
{
	float vx = matrix[0 * 4 + 0] * v.x + matrix[1 * 4 + 0] * v.y + matrix[2 * 4 + 0] * v.z + matrix[3 * 4 + 0] * v.w;
	float vy = matrix[0 * 4 + 1] * v.x + matrix[1 * 4 + 1] * v.y + matrix[2 * 4 + 1] * v.z + matrix[3 * 4 + 1] * v.w;
	float vz = matrix[0 * 4 + 2] * v.x + matrix[1 * 4 + 2] * v.y + matrix[2 * 4 + 2] * v.z + matrix[3 * 4 + 2] * v.w;
	float vw = matrix[0 * 4 + 3] * v.x + matrix[1 * 4 + 3] * v.y + matrix[2 * 4 + 3] * v.z + matrix[3 * 4 + 3] * v.w;
	ShadedTriVertex sv;
	sv.x = vx;
	sv.y = vy;
	sv.z = vz;
	sv.w = vw;
	for (int i = 0; i < TriVertex::NumVarying; i++)
		sv.varying[i] = v.varying[i];
	return sv;
}

/////////////////////////////////////////////////////////////////////////////

namespace
{
	int NextBufferVertex = 0;
}

TriVertex *PolyVertexBuffer::GetVertices(int count)
{
	enum { VertexBufferSize = 256 * 1024 };
	static TriVertex Vertex[VertexBufferSize];

	if (NextBufferVertex + count > VertexBufferSize)
		return nullptr;
	TriVertex *v = Vertex + NextBufferVertex;
	NextBufferVertex += count;
	return v;
}

void PolyVertexBuffer::Clear()
{
	NextBufferVertex = 0;
}

/////////////////////////////////////////////////////////////////////////////

void ScreenTriangle::SetupNormal(const TriDrawTriangleArgs *args, WorkerThreadData *thread)
{
	const TriVertex &v1 = *args->v1;
	const TriVertex &v2 = *args->v2;
	const TriVertex &v3 = *args->v3;
	int clipright = args->clipright;
	int clipbottom = args->clipbottom;
	
	int stencilPitch = args->stencilPitch;
	uint8_t * RESTRICT stencilValues = args->stencilValues;
	uint32_t * RESTRICT stencilMasks = args->stencilMasks;
	uint8_t stencilTestValue = args->stencilTestValue;
	
	TriFullSpan * RESTRICT span = thread->FullSpans;
	TriPartialBlock * RESTRICT partial = thread->PartialBlocks;
	
	// 28.4 fixed-point coordinates
	const int Y1 = (int)round(16.0f * v1.y);
	const int Y2 = (int)round(16.0f * v2.y);
	const int Y3 = (int)round(16.0f * v3.y);
	
	const int X1 = (int)round(16.0f * v1.x);
	const int X2 = (int)round(16.0f * v2.x);
	const int X3 = (int)round(16.0f * v3.x);
	
	// Deltas
	const int DX12 = X1 - X2;
	const int DX23 = X2 - X3;
	const int DX31 = X3 - X1;
	
	const int DY12 = Y1 - Y2;
	const int DY23 = Y2 - Y3;
	const int DY31 = Y3 - Y1;
	
	// Fixed-point deltas
	const int FDX12 = DX12 << 4;
	const int FDX23 = DX23 << 4;
	const int FDX31 = DX31 << 4;
	
	const int FDY12 = DY12 << 4;
	const int FDY23 = DY23 << 4;
	const int FDY31 = DY31 << 4;
	
	// Bounding rectangle
	int minx = MAX((MIN(MIN(X1, X2), X3) + 0xF) >> 4, 0);
	int maxx = MIN((MAX(MAX(X1, X2), X3) + 0xF) >> 4, clipright - 1);
	int miny = MAX((MIN(MIN(Y1, Y2), Y3) + 0xF) >> 4, 0);
	int maxy = MIN((MAX(MAX(Y1, Y2), Y3) + 0xF) >> 4, clipbottom - 1);
	if (minx >= maxx || miny >= maxy)
	{
		thread->NumFullSpans = 0;
		thread->NumPartialBlocks = 0;
		return;
	}
	
	// Block size, standard 8x8 (must be power of two)
	const int q = 8;
	
	// Start in corner of 8x8 block
	minx &= ~(q - 1);
	miny &= ~(q - 1);
	
	// Half-edge constants
	int C1 = DY12 * X1 - DX12 * Y1;
	int C2 = DY23 * X2 - DX23 * Y2;
	int C3 = DY31 * X3 - DX31 * Y3;
	
	// Correct for fill convention
	if (DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
	if (DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
	if (DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;
	
	// First block line for this thread
	int core = thread->core;
	int num_cores = thread->num_cores;
	int core_skip = (num_cores - ((miny / q) - core) % num_cores) % num_cores;
	miny += core_skip * q;

	thread->StartX = minx;
	thread->StartY = miny;
	span->Length = 0;

	// Loop through blocks
	for (int y = miny; y < maxy; y += q * num_cores)
	{
		for (int x = minx; x < maxx; x += q)
		{
			// Corners of block
			int x0 = x << 4;
			int x1 = (x + q - 1) << 4;
			int y0 = y << 4;
			int y1 = (y + q - 1) << 4;
			
			// Evaluate half-space functions
			bool a00 = C1 + DX12 * y0 - DY12 * x0 > 0;
			bool a10 = C1 + DX12 * y0 - DY12 * x1 > 0;
			bool a01 = C1 + DX12 * y1 - DY12 * x0 > 0;
			bool a11 = C1 + DX12 * y1 - DY12 * x1 > 0;
			int a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);
			
			bool b00 = C2 + DX23 * y0 - DY23 * x0 > 0;
			bool b10 = C2 + DX23 * y0 - DY23 * x1 > 0;
			bool b01 = C2 + DX23 * y1 - DY23 * x0 > 0;
			bool b11 = C2 + DX23 * y1 - DY23 * x1 > 0;
			int b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);
			
			bool c00 = C3 + DX31 * y0 - DY31 * x0 > 0;
			bool c10 = C3 + DX31 * y0 - DY31 * x1 > 0;
			bool c01 = C3 + DX31 * y1 - DY31 * x0 > 0;
			bool c11 = C3 + DX31 * y1 - DY31 * x1 > 0;
			int c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);
			
			// Stencil test the whole block, if possible
			int block = x / 8 + y / 8 * stencilPitch;
			uint8_t *stencilBlock = &stencilValues[block * 64];
			uint32_t *stencilBlockMask = &stencilMasks[block];
			bool blockIsSingleStencil = ((*stencilBlockMask) & 0xffffff00) == 0xffffff00;
			bool skipBlock = blockIsSingleStencil && ((*stencilBlockMask) & 0xff) != stencilTestValue;

			// Skip block when outside an edge
			if (a == 0 || b == 0 || c == 0 || skipBlock)
			{
				if (span->Length != 0)
				{
					span++;
					span->Length = 0;
				}
				continue;
			}

			// Accept whole block when totally covered
			if (a == 0xf && b == 0xf && c == 0xf && x + q <= clipright && y + q <= clipbottom && blockIsSingleStencil)
			{
				if (span->Length != 0)
				{
					span->Length++;
				}
				else
				{
					span->X = x;
					span->Y = y;
					span->Length = 1;
				}
			}
			else // Partially covered block
			{
				x0 = x << 4;
				x1 = (x + q - 1) << 4;
				int CY1 = C1 + DX12 * y0 - DY12 * x0;
				int CY2 = C2 + DX23 * y0 - DY23 * x0;
				int CY3 = C3 + DX31 * y0 - DY31 * x0;

				uint32_t mask0 = 0;
				uint32_t mask1 = 0;

				for (int iy = 0; iy < 4; iy++)
				{
					int CX1 = CY1;
					int CX2 = CY2;
					int CX3 = CY3;

					for (int ix = 0; ix < q; ix++)
					{
						bool passStencilTest = blockIsSingleStencil || stencilBlock[ix + iy * q] == stencilTestValue;
						bool covered = (CX1 > 0 && CX2 > 0 && CX3 > 0 && (x + ix) < clipright && (y + iy) < clipbottom && passStencilTest);
						mask0 <<= 1;
						mask0 |= (uint32_t)covered;

						CX1 -= FDY12;
						CX2 -= FDY23;
						CX3 -= FDY31;
					}

					CY1 += FDX12;
					CY2 += FDX23;
					CY3 += FDX31;
				}

				for (int iy = 4; iy < q; iy++)
				{
					int CX1 = CY1;
					int CX2 = CY2;
					int CX3 = CY3;

					for (int ix = 0; ix < q; ix++)
					{
						bool passStencilTest = blockIsSingleStencil || stencilBlock[ix + iy * q] == stencilTestValue;
						bool covered = (CX1 > 0 && CX2 > 0 && CX3 > 0 && (x + ix) < clipright && (y + iy) < clipbottom && passStencilTest);
						mask1 <<= 1;
						mask1 |= (uint32_t)covered;

						CX1 -= FDY12;
						CX2 -= FDY23;
						CX3 -= FDY31;
					}

					CY1 += FDX12;
					CY2 += FDX23;
					CY3 += FDX31;
				}

				if (mask0 != 0xffffffff || mask1 != 0xffffffff)
				{
					if (span->Length > 0)
					{
						span++;
						span->Length = 0;
					}

					if (mask0 == 0 && mask1 == 0)
						continue;

					partial->X = x;
					partial->Y = y;
					partial->Mask0 = mask0;
					partial->Mask1 = mask1;
					partial++;
				}
				else if (span->Length != 0)
				{
					span->Length++;
				}
				else
				{
					span->X = x;
					span->Y = y;
					span->Length = 1;
				}
			}
		}
		
		if (span->Length != 0)
		{
			span++;
			span->Length = 0;
		}
	}
	
	thread->NumFullSpans = (int)(span - thread->FullSpans);
	thread->NumPartialBlocks = (int)(partial - thread->PartialBlocks);
}

void ScreenTriangle::SetupSubsector(const TriDrawTriangleArgs *args, WorkerThreadData *thread)
{
	const TriVertex &v1 = *args->v1;
	const TriVertex &v2 = *args->v2;
	const TriVertex &v3 = *args->v3;
	int clipright = args->clipright;
	int clipbottom = args->clipbottom;

	int stencilPitch = args->stencilPitch;
	uint8_t * RESTRICT stencilValues = args->stencilValues;
	uint32_t * RESTRICT stencilMasks = args->stencilMasks;
	uint8_t stencilTestValue = args->stencilTestValue;

	uint32_t * RESTRICT subsectorGBuffer = args->subsectorGBuffer;
	uint32_t subsectorDepth = args->uniforms->subsectorDepth;
	int32_t pitch = args->pitch;

	TriFullSpan * RESTRICT span = thread->FullSpans;
	TriPartialBlock * RESTRICT partial = thread->PartialBlocks;

	// 28.4 fixed-point coordinates
	const int Y1 = (int)round(16.0f * v1.y);
	const int Y2 = (int)round(16.0f * v2.y);
	const int Y3 = (int)round(16.0f * v3.y);

	const int X1 = (int)round(16.0f * v1.x);
	const int X2 = (int)round(16.0f * v2.x);
	const int X3 = (int)round(16.0f * v3.x);

	// Deltas
	const int DX12 = X1 - X2;
	const int DX23 = X2 - X3;
	const int DX31 = X3 - X1;

	const int DY12 = Y1 - Y2;
	const int DY23 = Y2 - Y3;
	const int DY31 = Y3 - Y1;

	// Fixed-point deltas
	const int FDX12 = DX12 << 4;
	const int FDX23 = DX23 << 4;
	const int FDX31 = DX31 << 4;

	const int FDY12 = DY12 << 4;
	const int FDY23 = DY23 << 4;
	const int FDY31 = DY31 << 4;

	// Bounding rectangle
	int minx = MAX((MIN(MIN(X1, X2), X3) + 0xF) >> 4, 0);
	int maxx = MIN((MAX(MAX(X1, X2), X3) + 0xF) >> 4, clipright - 1);
	int miny = MAX((MIN(MIN(Y1, Y2), Y3) + 0xF) >> 4, 0);
	int maxy = MIN((MAX(MAX(Y1, Y2), Y3) + 0xF) >> 4, clipbottom - 1);
	if (minx >= maxx || miny >= maxy)
	{
		thread->NumFullSpans = 0;
		thread->NumPartialBlocks = 0;
		return;
	}

	// Block size, standard 8x8 (must be power of two)
	const int q = 8;

	// Start in corner of 8x8 block
	minx &= ~(q - 1);
	miny &= ~(q - 1);

	// Half-edge constants
	int C1 = DY12 * X1 - DX12 * Y1;
	int C2 = DY23 * X2 - DX23 * Y2;
	int C3 = DY31 * X3 - DX31 * Y3;

	// Correct for fill convention
	if (DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
	if (DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
	if (DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;

	// First block line for this thread
	int core = thread->core;
	int num_cores = thread->num_cores;
	int core_skip = (num_cores - ((miny / q) - core) % num_cores) % num_cores;
	miny += core_skip * q;

	thread->StartX = minx;
	thread->StartY = miny;
	span->Length = 0;

	// Loop through blocks
	for (int y = miny; y < maxy; y += q * num_cores)
	{
		for (int x = minx; x < maxx; x += q)
		{
			// Corners of block
			int x0 = x << 4;
			int x1 = (x + q - 1) << 4;
			int y0 = y << 4;
			int y1 = (y + q - 1) << 4;

			// Evaluate half-space functions
			bool a00 = C1 + DX12 * y0 - DY12 * x0 > 0;
			bool a10 = C1 + DX12 * y0 - DY12 * x1 > 0;
			bool a01 = C1 + DX12 * y1 - DY12 * x0 > 0;
			bool a11 = C1 + DX12 * y1 - DY12 * x1 > 0;
			int a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);

			bool b00 = C2 + DX23 * y0 - DY23 * x0 > 0;
			bool b10 = C2 + DX23 * y0 - DY23 * x1 > 0;
			bool b01 = C2 + DX23 * y1 - DY23 * x0 > 0;
			bool b11 = C2 + DX23 * y1 - DY23 * x1 > 0;
			int b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);

			bool c00 = C3 + DX31 * y0 - DY31 * x0 > 0;
			bool c10 = C3 + DX31 * y0 - DY31 * x1 > 0;
			bool c01 = C3 + DX31 * y1 - DY31 * x0 > 0;
			bool c11 = C3 + DX31 * y1 - DY31 * x1 > 0;
			int c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);

			// Stencil test the whole block, if possible
			int block = x / 8 + y / 8 * stencilPitch;
			uint8_t *stencilBlock = &stencilValues[block * 64];
			uint32_t *stencilBlockMask = &stencilMasks[block];
			bool blockIsSingleStencil = ((*stencilBlockMask) & 0xffffff00) == 0xffffff00;
			bool skipBlock = blockIsSingleStencil && ((*stencilBlockMask) & 0xff) < stencilTestValue;

			// Skip block when outside an edge
			if (a == 0 || b == 0 || c == 0 || skipBlock)
			{
				if (span->Length != 0)
				{
					span++;
					span->Length = 0;
				}
				continue;
			}

			// Accept whole block when totally covered
			if (a == 0xf && b == 0xf && c == 0xf && x + q <= clipright && y + q <= clipbottom && blockIsSingleStencil)
			{
				// Totally covered block still needs a subsector coverage test:

				uint32_t *subsector = subsectorGBuffer + x + y * pitch;

				uint32_t mask0 = 0;
				uint32_t mask1 = 0;

				for (int iy = 0; iy < 4; iy++)
				{
					for (int ix = 0; ix < q; ix++)
					{
						bool covered = subsector[ix] >= subsectorDepth;
						mask0 <<= 1;
						mask0 |= (uint32_t)covered;
					}
					subsector += pitch;
				}

				for (int iy = 4; iy < q; iy++)
				{
					for (int ix = 0; ix < q; ix++)
					{
						bool covered = subsector[ix] >= subsectorDepth;
						mask1 <<= 1;
						mask1 |= (uint32_t)covered;
					}
					subsector += pitch;
				}

				if (mask0 != 0xffffffff || mask1 != 0xffffffff)
				{
					if (span->Length > 0)
					{
						span++;
						span->Length = 0;
					}

					if (mask0 == 0 && mask1 == 0)
						continue;

					partial->X = x;
					partial->Y = y;
					partial->Mask0 = mask0;
					partial->Mask1 = mask1;
					partial++;
				}
				else if (span->Length != 0)
				{
					span->Length++;
				}
				else
				{
					span->X = x;
					span->Y = y;
					span->Length = 1;
				}
			}
			else // Partially covered block
			{
				x0 = x << 4;
				x1 = (x + q - 1) << 4;
				int CY1 = C1 + DX12 * y0 - DY12 * x0;
				int CY2 = C2 + DX23 * y0 - DY23 * x0;
				int CY3 = C3 + DX31 * y0 - DY31 * x0;

				uint32_t *subsector = subsectorGBuffer + x + y * pitch;

				uint32_t mask0 = 0;
				uint32_t mask1 = 0;

				for (int iy = 0; iy < 4; iy++)
				{
					int CX1 = CY1;
					int CX2 = CY2;
					int CX3 = CY3;

					for (int ix = 0; ix < q; ix++)
					{
						bool passStencilTest = blockIsSingleStencil || stencilBlock[ix + iy * q] >= stencilTestValue;
						bool covered = (CX1 > 0 && CX2 > 0 && CX3 > 0 && (x + ix) < clipright && (y + iy) < clipbottom && passStencilTest && subsector[ix] >= subsectorDepth);
						mask0 <<= 1;
						mask0 |= (uint32_t)covered;

						CX1 -= FDY12;
						CX2 -= FDY23;
						CX3 -= FDY31;
					}

					CY1 += FDX12;
					CY2 += FDX23;
					CY3 += FDX31;
					subsector += pitch;
				}

				for (int iy = 4; iy < q; iy++)
				{
					int CX1 = CY1;
					int CX2 = CY2;
					int CX3 = CY3;

					for (int ix = 0; ix < q; ix++)
					{
						bool passStencilTest = blockIsSingleStencil || stencilBlock[ix + iy * q] >= stencilTestValue;
						bool covered = (CX1 > 0 && CX2 > 0 && CX3 > 0 && (x + ix) < clipright && (y + iy) < clipbottom && passStencilTest && subsector[ix] >= subsectorDepth);
						mask1 <<= 1;
						mask1 |= (uint32_t)covered;

						CX1 -= FDY12;
						CX2 -= FDY23;
						CX3 -= FDY31;
					}

					CY1 += FDX12;
					CY2 += FDX23;
					CY3 += FDX31;
					subsector += pitch;
				}

				if (mask0 != 0xffffffff || mask1 != 0xffffffff)
				{
					if (span->Length > 0)
					{
						span++;
						span->Length = 0;
					}

					if (mask0 == 0 && mask1 == 0)
						continue;

					partial->X = x;
					partial->Y = y;
					partial->Mask0 = mask0;
					partial->Mask1 = mask1;
					partial++;
				}
				else if (span->Length != 0)
				{
					span->Length++;
				}
				else
				{
					span->X = x;
					span->Y = y;
					span->Length = 1;
				}
			}
		}

		if (span->Length != 0)
		{
			span++;
			span->Length = 0;
		}
	}

	thread->NumFullSpans = (int)(span - thread->FullSpans);
	thread->NumPartialBlocks = (int)(partial - thread->PartialBlocks);
}

void ScreenTriangle::StencilWrite(const TriDrawTriangleArgs *args, WorkerThreadData *thread)
{
	uint8_t * RESTRICT stencilValues = args->stencilValues;
	uint32_t * RESTRICT stencilMasks = args->stencilMasks;
	uint32_t stencilWriteValue = args->stencilWriteValue;
	uint32_t stencilPitch = args->stencilPitch;

	int numSpans = thread->NumFullSpans;
	auto fullSpans = thread->FullSpans;
	int numBlocks = thread->NumPartialBlocks;
	auto partialBlocks = thread->PartialBlocks;

	for (int i = 0; i < numSpans; i++)
	{
		const auto &span = fullSpans[i];
		
		int block = span.X / 8 + span.Y / 8 * stencilPitch;
		uint8_t *stencilBlock = &stencilValues[block * 64];
		uint32_t *stencilBlockMask = &stencilMasks[block];
		
		int width = span.Length;
		for (int x = 0; x < width; x++)
			stencilBlockMask[x] = 0xffffff00 | stencilWriteValue;
	}
	
	for (int i = 0; i < numBlocks; i++)
	{
		const auto &block = partialBlocks[i];
		
		uint32_t mask0 = block.Mask0;
		uint32_t mask1 = block.Mask1;
		
		int sblock = block.X / 8 + block.Y / 8 * stencilPitch;
		uint8_t *stencilBlock = &stencilValues[sblock * 64];
		uint32_t *stencilBlockMask = &stencilMasks[sblock];
		
		bool isSingleValue = ((*stencilBlockMask) & 0xffffff00) == 0xffffff00;
		if (isSingleValue)
		{
			uint8_t value = (*stencilBlockMask) & 0xff;
			for (int v = 0; v < 64; v++)
				stencilBlock[v] = value;
			*stencilBlockMask = 0;
		}
		
		int count = 0;
		for (int v = 0; v < 32; v++)
		{
			if ((mask0 & (1 << 31)) || stencilBlock[v] == stencilWriteValue)
			{
				stencilBlock[v] = stencilWriteValue;
				count++;
			}
			mask0 <<= 1;
		}
		for (int v = 32; v < 64; v++)
		{
			if ((mask1 & (1 << 31)) || stencilBlock[v] == stencilWriteValue)
			{
				stencilBlock[v] = stencilWriteValue;
				count++;
			}
			mask1 <<= 1;
		}
		
		if (count == 64)
			*stencilBlockMask = 0xffffff00 | stencilWriteValue;
	}
}

void ScreenTriangle::SubsectorWrite(const TriDrawTriangleArgs *args, WorkerThreadData *thread)
{
	uint32_t * RESTRICT subsectorGBuffer = args->subsectorGBuffer;
	uint32_t subsectorDepth = args->uniforms->subsectorDepth;
	int pitch = args->pitch;

	int numSpans = thread->NumFullSpans;
	auto fullSpans = thread->FullSpans;
	int numBlocks = thread->NumPartialBlocks;
	auto partialBlocks = thread->PartialBlocks;

	for (int i = 0; i < numSpans; i++)
	{
		const auto &span = fullSpans[i];

		uint32_t *subsector = subsectorGBuffer + span.X + span.Y * pitch;
		int width = span.Length * 8;
		int height = 8;
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
				subsector[x] = subsectorDepth;
			subsector += pitch;
		}
	}
	
	for (int i = 0; i < numBlocks; i++)
	{
		const auto &block = partialBlocks[i];

		uint32_t *subsector = subsectorGBuffer + block.X + block.Y * pitch;
		uint32_t mask0 = block.Mask0;
		uint32_t mask1 = block.Mask1;
		for (int y = 0; y < 4; y++)
		{
			for (int x = 0; x < 8; x++)
			{
				if (mask0 & (1 << 31))
					subsector[x] = subsectorDepth;
				mask0 <<= 1;
			}
			subsector += pitch;
		}
		for (int y = 4; y < 8; y++)
		{
			for (int x = 0; x < 8; x++)
			{
				if (mask1 & (1 << 31))
					subsector[x] = subsectorDepth;
				mask1 <<= 1;
			}
			subsector += pitch;
		}
	}
}

#if 0
float ScreenTriangle::FindGradientX(float x0, float y0, float x1, float y1, float x2, float y2, float c0, float c1, float c2)
{
	float top = (c1 - c2) * (y0 - y2) - (c0 - c2) * (y1 - y2);
	float bottom = (x1 - x2) * (y0 - y2) - (x0 - x2) * (y1 - y2);
	return top / bottom;
}

float ScreenTriangle::FindGradientY(float x0, float y0, float x1, float y1, float x2, float y2, float c0, float c1, float c2)
{
	float top = (c1 - c2) * (x0 - x2) - (c0 - c2) * (x1 - x2);
	float bottom = (x0 - x2) * (y1 - y2) - (x1 - x2) * (y0 - y2);
	return top / bottom;
}

void ScreenTriangle::Draw(const TriDrawTriangleArgs *args, WorkerThreadData *thread)
{
	int numSpans = thread->NumFullSpans;
	auto fullSpans = thread->FullSpans;
	int numBlocks = thread->NumPartialBlocks;
	auto partialBlocks = thread->PartialBlocks;
	int startX = thread->StartX;
	int startY = thread->StartY;

	// Calculate gradients
	const TriVertex &v1 = *args->v1;
	const TriVertex &v2 = *args->v2;
	const TriVertex &v3 = *args->v3;
	ScreenTriangleStepVariables gradientX;
	ScreenTriangleStepVariables gradientY;
	ScreenTriangleStepVariables start;
	gradientX.W = FindGradientX(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v1.w, v2.w, v3.w);
	gradientY.W = FindGradientY(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v1.w, v2.w, v3.w);
	start.W = v1.w + gradientX.W * (startX - v1.x) + gradientY.W * (startY - v1.y);
	for (int i = 0; i < TriVertex::NumVarying; i++)
	{
		gradientX.Varying[i] = FindGradientX(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v1.varying[i] * v1.w, v2.varying[i] * v2.w, v3.varying[i] * v3.w);
		gradientY.Varying[i] = FindGradientY(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v1.varying[i] * v1.w, v2.varying[i] * v2.w, v3.varying[i] * v3.w);
		start.Varying[i] = v1.varying[i] * v1.w + gradientX.Varying[i] * (startX - v1.x) + gradientY.Varying[i] * (startY - v1.y);
	}

	const uint32_t * RESTRICT texPixels = (const uint32_t *)args->texturePixels;
	uint32_t texWidth = args->textureWidth;
	uint32_t texHeight = args->textureHeight;

	uint32_t * RESTRICT destOrg = (uint32_t*)args->dest;
	uint32_t * RESTRICT subsectorGBuffer = (uint32_t*)args->subsectorGBuffer;
	int pitch = args->pitch;

	uint32_t subsectorDepth = args->uniforms->subsectorDepth;

	uint32_t light = args->uniforms->light;
	float shade = (64.0f - (light * 255 / 256 + 12.0f) * 32.0f / 128.0f) / 32.0f;
	float globVis = 1706.0f;

	for (int i = 0; i < numSpans; i++)
	{
		const auto &span = fullSpans[i];

		uint32_t *dest = destOrg + span.X + span.Y * pitch;
		uint32_t *subsector = subsectorGBuffer + span.X + span.Y * pitch;
		int width = span.Length;
		int height = 8;

		ScreenTriangleStepVariables blockPosY;
		blockPosY.W = start.W + gradientX.W * (span.X - startX) + gradientY.W * (span.Y - startY);
		for (int j = 0; j < TriVertex::NumVarying; j++)
			blockPosY.Varying[j] = start.Varying[j] + gradientX.Varying[j] * (span.X - startX) + gradientY.Varying[j] * (span.Y - startY);

		for (int y = 0; y < height; y++)
		{
			ScreenTriangleStepVariables blockPosX = blockPosY;

			float rcpW = 0x01000000 / blockPosX.W;
			int32_t varyingPos[TriVertex::NumVarying];
			for (int j = 0; j < TriVertex::NumVarying; j++)
				varyingPos[j] = (int32_t)(blockPosX.Varying[j] * rcpW);
			int lightpos = 256 - (int)(clamp(shade - MIN(24.0f, globVis * blockPosX.W) / 32.0f, 0.0f, 31.0f / 32.0f) * 256.0f);

			for (int x = 0; x < width; x++)
			{
				blockPosX.W += gradientX.W * 8;
				for (int j = 0; j < TriVertex::NumVarying; j++)
					blockPosX.Varying[j] += gradientX.Varying[j] * 8;

				rcpW = 0x01000000 / blockPosX.W;
				int32_t varyingStep[TriVertex::NumVarying];
				for (int j = 0; j < TriVertex::NumVarying; j++)
				{
					int32_t nextPos = (int32_t)(blockPosX.Varying[j] * rcpW);
					varyingStep[j] = (nextPos - varyingPos[j]) / 8;
				}

				int lightnext = 256 - (int)(clamp(shade - MIN(24.0f, globVis * blockPosX.W) / 32.0f, 0.0f, 31.0f / 32.0f) * 256.0f);
				int lightstep = (lightnext - lightpos) / 8;

				for (int ix = 0; ix < 8; ix++)
				{
					int texelX = ((((uint32_t)varyingPos[0] << 8) >> 16) * texWidth) >> 16;
					int texelY = ((((uint32_t)varyingPos[1] << 8) >> 16) * texHeight) >> 16;
					uint32_t fg = texPixels[texelX * texHeight + texelY];

					uint32_t r = RPART(fg);
					uint32_t g = GPART(fg);
					uint32_t b = BPART(fg);
					r = r * lightpos / 256;
					g = g * lightpos / 256;
					b = b * lightpos / 256;
					fg = 0xff000000 | (r << 16) | (g << 8) | b;

					dest[x * 8 + ix] = fg;
					subsector[x * 8 + ix] = subsectorDepth;

					for (int j = 0; j < TriVertex::NumVarying; j++)
						varyingPos[j] += varyingStep[j];
					lightpos += lightstep;
				}
			}
		
			blockPosY.W += gradientY.W;
			for (int j = 0; j < TriVertex::NumVarying; j++)
				blockPosY.Varying[j] += gradientY.Varying[j];

			dest += pitch;
			subsector += pitch;
		}
	}
	
	for (int i = 0; i < numBlocks; i++)
	{
		const auto &block = partialBlocks[i];

		ScreenTriangleStepVariables blockPosY;
		blockPosY.W = start.W + gradientX.W * (block.X - startX) + gradientY.W * (block.Y - startY);
		for (int j = 0; j < TriVertex::NumVarying; j++)
			blockPosY.Varying[j] = start.Varying[j] + gradientX.Varying[j] * (block.X - startX) + gradientY.Varying[j] * (block.Y - startY);

		uint32_t *dest = destOrg + block.X + block.Y * pitch;
		uint32_t *subsector = subsectorGBuffer + block.X + block.Y * pitch;
		uint32_t mask0 = block.Mask0;
		uint32_t mask1 = block.Mask1;
		for (int y = 0; y < 4; y++)
		{
			ScreenTriangleStepVariables blockPosX = blockPosY;

			float rcpW = 0x01000000 / blockPosX.W;
			int32_t varyingPos[TriVertex::NumVarying];
			for (int j = 0; j < TriVertex::NumVarying; j++)
				varyingPos[j] = (int32_t)(blockPosX.Varying[j] * rcpW);

			int lightpos = 256 - (int)(clamp(shade - MIN(24.0f, globVis * blockPosX.W) / 32.0f, 0.0f, 31.0f / 32.0f) * 256.0f);

			blockPosX.W += gradientX.W * 8;
			for (int j = 0; j < TriVertex::NumVarying; j++)
				blockPosX.Varying[j] += gradientX.Varying[j] * 8;

			rcpW = 0x01000000 / blockPosX.W;
			int32_t varyingStep[TriVertex::NumVarying];
			for (int j = 0; j < TriVertex::NumVarying; j++)
			{
				int32_t nextPos = (int32_t)(blockPosX.Varying[j] * rcpW);
				varyingStep[j] = (nextPos - varyingPos[j]) / 8;
			}

			int lightnext = 256 - (int)(clamp(shade - MIN(24.0f, globVis * blockPosX.W) / 32.0f, 0.0f, 31.0f / 32.0f) * 256.0f);
			int lightstep = (lightnext - lightpos) / 8;

			for (int x = 0; x < 8; x++)
			{
				if (mask0 & (1 << 31))
				{
					int texelX = ((((uint32_t)varyingPos[0] << 8) >> 16) * texWidth) >> 16;
					int texelY = ((((uint32_t)varyingPos[1] << 8) >> 16) * texHeight) >> 16;
					uint32_t fg = texPixels[texelX * texHeight + texelY];

					uint32_t r = RPART(fg);
					uint32_t g = GPART(fg);
					uint32_t b = BPART(fg);
					r = r * lightpos / 256;
					g = g * lightpos / 256;
					b = b * lightpos / 256;
					fg = 0xff000000 | (r << 16) | (g << 8) | b;

					dest[x] = fg;
					subsector[x] = subsectorDepth;
				}
				mask0 <<= 1;

				for (int j = 0; j < TriVertex::NumVarying; j++)
					varyingPos[j] += varyingStep[j];
				lightpos += lightstep;
			}

			blockPosY.W += gradientY.W;
			for (int j = 0; j < TriVertex::NumVarying; j++)
				blockPosY.Varying[j] += gradientY.Varying[j];

			dest += pitch;
			subsector += pitch;
		}
		for (int y = 4; y < 8; y++)
		{
			ScreenTriangleStepVariables blockPosX = blockPosY;

			float rcpW = 0x01000000 / blockPosX.W;
			int32_t varyingPos[TriVertex::NumVarying];
			for (int j = 0; j < TriVertex::NumVarying; j++)
				varyingPos[j] = (int32_t)(blockPosX.Varying[j] * rcpW);

			int lightpos = 256 - (int)(clamp(shade - MIN(24.0f, globVis * blockPosX.W) / 32.0f, 0.0f, 31.0f / 32.0f) * 256.0f);

			blockPosX.W += gradientX.W * 8;
			for (int j = 0; j < TriVertex::NumVarying; j++)
				blockPosX.Varying[j] += gradientX.Varying[j] * 8;

			rcpW = 0x01000000 / blockPosX.W;
			int32_t varyingStep[TriVertex::NumVarying];
			for (int j = 0; j < TriVertex::NumVarying; j++)
			{
				int32_t nextPos = (int32_t)(blockPosX.Varying[j] * rcpW);
				varyingStep[j] = (nextPos - varyingPos[j]) / 8;
			}

			int lightnext = 256 - (int)(clamp(shade - MIN(24.0f, globVis * blockPosX.W) / 32.0f, 0.0f, 31.0f / 32.0f) * 256.0f);
			int lightstep = (lightnext - lightpos) / 8;

			for (int x = 0; x < 8; x++)
			{
				if (mask1 & (1 << 31))
				{
					int texelX = ((((uint32_t)varyingPos[0] << 8) >> 16) * texWidth) >> 16;
					int texelY = ((((uint32_t)varyingPos[1] << 8) >> 16) * texHeight) >> 16;
					uint32_t fg = texPixels[texelX * texHeight + texelY];

					uint32_t r = RPART(fg);
					uint32_t g = GPART(fg);
					uint32_t b = BPART(fg);
					r = r * lightpos / 256;
					g = g * lightpos / 256;
					b = b * lightpos / 256;
					fg = 0xff000000 | (r << 16) | (g << 8) | b;

					dest[x] = fg;
					subsector[x] = subsectorDepth;
				}
				mask1 <<= 1;

				for (int j = 0; j < TriVertex::NumVarying; j++)
					varyingPos[j] += varyingStep[j];
				lightpos += lightstep;
			}

			blockPosY.W += gradientY.W;
			for (int j = 0; j < TriVertex::NumVarying; j++)
				blockPosY.Varying[j] += gradientY.Varying[j];

			dest += pitch;
			subsector += pitch;
		}
	}
}
#endif
