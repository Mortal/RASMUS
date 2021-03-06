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
#ifndef __RM_ABORT_HH__
#define __RM_ABORT_HH__
#include <stdlib/callback.hh>
#include <stdlib/gil.hh>

extern "C" {
  extern volatile bool rm_do_abort;
}


namespace rasmus {
namespace stdlib {

inline void checkAbort() {
	gil.unlock(); //Allow other threads to run if needed
	gil.lock();

	if (rm_do_abort) callback->reportAbort();
}

}
}

#endif //__RM_ABORT_HH__
