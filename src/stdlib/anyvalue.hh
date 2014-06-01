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
#include <shared/type.hh>
#include <stdlib/refptr.hh>
#include <stdlib/rm_object.hh>

namespace rasmus {
namespace stdlib {

/* used as a container class for tuples; each tuple has a number
   of AnyValues which can either be a TInt, TText or TBool
 */
struct AnyValue {
	Type type;
	union {
		int64_t intValue;
		bool boolValue;
		RefPtr<rm_object> objectValue;
	};
	
	AnyValue(): type(TInvalid) {}
	AnyValue(int64_t value): type(TInt), intValue(value) {}
	AnyValue(bool value): type(TBool), boolValue(value) {}
	AnyValue(Type type, RefPtr<rm_object> value): type(type), objectValue(value) {}

	AnyValue(const AnyValue & other): type(other.type) {
		switch(type){
		case TInt:
			intValue = other.intValue;
			break;
		case TBool:
			boolValue = other.boolValue;
			break;
		case TInvalid:
			break;
		default:
			new (&objectValue) RefPtr<rm_object>(other.objectValue);
			break;
		}

	}
	
	AnyValue(AnyValue && other): type(other.type) {
		switch(type){
		case TInt:
			intValue = other.intValue;
			break;
		case TBool:
			boolValue = other.boolValue;
			break;
		case TInvalid:
			break;
		default:
			new (&objectValue) RefPtr<rm_object>(std::move(other.objectValue));
			break;
		}
	}

	AnyValue & operator= (const AnyValue & other){
		this->~AnyValue();
		new (this) AnyValue(other);
		return *this;
	}
	
	AnyValue & operator= ( AnyValue && other){
		this->~AnyValue();
		new (this) AnyValue(std::move(other));
		return *this;
	}
	
	~AnyValue() {
		switch (type) {
		case TInt:
		case TBool:
		case TInvalid:
			break;
		default:
			objectValue.~RefPtr<rm_object>();
		}
	}
};

} //namespace stdlib
} //namespace rasmus