/*
**  SSA int32 pointer
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

#include "ssa_float.h"
#include "ssa_int.h"
#include "ssa_vec4i.h"

namespace llvm { class Value; }
namespace llvm { class Type; }

class SSABool;

class SSAIntPtr
{
public:
	SSAIntPtr();
	explicit SSAIntPtr(llvm::Value *v);
	static SSAIntPtr from_llvm(llvm::Value *v) { return SSAIntPtr(v); }
	static llvm::Type *llvm_type();
	SSAIntPtr operator[](SSAInt index) const;
	SSAIntPtr operator[](int index) const { return (*this)[SSAInt(index)]; }
	SSAInt load(bool constantScopeDomain) const;
	SSAVec4i load_vec4i(bool constantScopeDomain) const;
	SSAVec4i load_unaligned_vec4i(bool constantScopeDomain) const;
	void store(const SSAInt &new_value);
	void store_vec4i(const SSAVec4i &new_value);
	void store_unaligned_vec4i(const SSAVec4i &new_value);
	void store_masked_vec4i(const SSAVec4i &new_value, SSABool mask[4]);

	llvm::Value *v;
};
