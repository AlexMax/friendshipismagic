/*
**  SetupTriangle code generation
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

#pragma once

#include "drawercodegen.h"

struct SSASetupVertex
{
	SSAFloat x, y, z, w;
};

class SetupTriangleCodegen : public DrawerCodegen
{
public:
	void Generate(bool subsectorTest, SSAValue args, SSAValue thread_data);

private:
	void LoadArgs(SSAValue args, SSAValue thread_data);
	SSASetupVertex LoadTriVertex(SSAValue v);
	void Setup();
	SSAInt FloatTo28_4(SSAFloat v);
	void LoopBlockY();
	void LoopBlockX();
	void LoopFullBlock();
	void LoopPartialBlock(bool isSingleStencilValue);

	void SetStencilBlock(SSAInt block);
	void StencilClear(SSAUByte value);
	SSAUByte StencilGetSingle();
	SSABool StencilIsSingleValue();

	bool subsectorTest;

	SSAStack<SSAInt> stack_C1, stack_C2, stack_C3;
	SSAStack<SSAInt> stack_y;
	SSAStack<SSAIntPtr> stack_subsectorGBuffer;
	SSAStack<SSAInt> stack_x;
	SSAStack<SSAUBytePtr> stack_buffer;
	SSAStack<SSAInt> stack_iy, stack_ix;
	SSAStack<SSAInt> stack_CY1, stack_CY2, stack_CY3;
	SSAStack<SSAInt> stack_CX1, stack_CX2, stack_CX3;
	//SSAStack<SSABool> stack_stencilblock_restored;
	//SSAStack<SSAUByte> stack_stencilblock_lastval;

	SSAIntPtr subsectorGBuffer;
	SSAInt pitch;
	SSASetupVertex v1;
	SSASetupVertex v2;
	SSASetupVertex v3;
	SSAInt clipright;
	SSAInt clipbottom;

	SSAUBytePtr stencilValues;
	SSAIntPtr stencilMasks;
	SSAInt stencilPitch;
	SSAUByte stencilTestValue;
	SSAUByte stencilWriteValue;

	SSAWorkerThread thread;

	// Block size, standard 8x8 (must be power of two)
	const int q = 8;

	SSAInt Y1, Y2, Y3;
	SSAInt X1, X2, X3;
	SSAInt DX12, DX23, DX31;
	SSAInt DY12, DY23, DY31;
	SSAInt FDX12, FDX23, FDX31;
	SSAInt FDY12, FDY23, FDY31;
	SSAInt minx, maxx, miny, maxy;
	SSAInt C1, C2, C3;

	SSAInt x, y;
	SSAInt x0, x1, y0, y1;

	SSAUBytePtr StencilBlock;
	SSAIntPtr StencilBlockMask;
};
