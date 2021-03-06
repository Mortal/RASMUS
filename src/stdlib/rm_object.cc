// -*- mode: c++; tab-width: 4; indent-tabs-mode: t; eval: (progn (c-set-style "stroustrup") (c-set-offset 'innamespace 0) (c-set-offset 'inextern-lang 0)); -*-
// vi:set ts=4 sts=4 sw=4 noet :
// Copyright 2014 The pyRASMUS development team
// 
// This file is part of pyRASMUS.
// 
// pyRASMUS is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
// 
// pyRASMUS is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
// License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with pyRASMUS.  If not, see <http://www.gnu.org/licenses/>
#include <stdlib/rm_object.hh>
#include <stdlib/function.hh>
#include <stdlib/text.hh>
#include <stdlib/relation.hh>
#include <stdlib/callback.hh>
#include <stdlib/ile.hh>
#include <shared/type.hh>
#include <iostream>
#include <sstream>
#include <limits>

namespace rasmus {
namespace stdlib {

size_t objectCount=0;
bool debugAllocations=false;

void reportAllocation(rm_object * ptr) {
	if (!callback) return;
	std::stringstream ss;
	ss << "alloc " << ptr << " of type " << ptr->type;
	callback->reportMessage(ss.str());
}


void reportDeallocation(rm_object * ptr) {
	if (!callback) return;
	std::stringstream ss;
	ss << "dealloc " << ptr << " of type " << ptr->type;
	callback->reportMessage(ss.str());
}



} //namespace stdlib
} //namespace rasmus

extern "C" {
using namespace rasmus::stdlib;

void rm_free(rm_object * o) {
	registerDeallocation(o);
	switch (o->type) {
	case LType::canonicalText:
		static_cast<CanonicalText*>(o)->~CanonicalText();
		operator delete(reinterpret_cast<void *>(o));
		break;
	case LType::concatText:
		static_cast<ConcatText*>(o)->~ConcatText();
		operator delete(reinterpret_cast<void *>(o));
		break;
	case LType::substrText:
		static_cast<SubstrText*>(o)->~SubstrText();
		operator delete(reinterpret_cast<void *>(o));
		break;
	case LType::smallText:
		static_cast<SmallText*>(o)->~SmallText();
		operator delete(reinterpret_cast<void *>(o));
		break;
	case LType::schema:
		delete static_cast<Schema*>(o);
		break;
	case LType::relation:
		delete static_cast<Relation*>(o);
		break;
	case LType::tuple:
		delete static_cast<Tuple*>(o);
		break;
	case LType::function: 
	{		
		function_object * fo = static_cast<function_object*>(o);
		fo->dtor(fo);
		operator delete(reinterpret_cast<void *>(fo));
		break;
	}
	default:
		ILE("Unhandled type", o->type);
	}
}

/**
 * Create a new function
 */
rm_object * rm_createFunction(uint32_t size) {
	void * m=operator new(size);
	function_object * o = static_cast<function_object*>(m);
	new(o) function_object();
	registerAllocation(o);
	return o;
}

int64_t rm_length(rm_object * obj) {
	switch (obj->type) {
	case LType::undefText:
		return std::numeric_limits<int64_t>::min();
	case LType::concatText:
	case LType::substrText:
	case LType::smallText:
	case LType::canonicalText:
		return static_cast<TextBase*>(obj)->length;
	case LType::schema:
		return static_cast<Schema*>(obj)->attributes.size();
	case LType::tuple:
		return static_cast<Tuple*>(obj)->values.size();
	case LType::relation:
		return static_cast<Relation*>(obj)->tuples.size();
	default:
		ILE("Unhandled type", obj->type);
	}
}


} //extern "C"

