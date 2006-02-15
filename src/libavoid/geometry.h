/*
 * vim: ts=4 sw=4 et tw=0 wm=0
 *
 * libavoid - Fast, Incremental, Object-avoiding Line Router
 * Copyright (C) 2004-2006  Michael Wybrow <mjwybrow@users.sourceforge.net>
 *
 * --------------------------------------------------------------------
 * Much of the code in this module is based on code published with
 * and/or described in "Computational Geometry in C" (Second Edition),
 * Copyright (C) 1998  Joseph O'Rourke <orourke@cs.smith.edu>
 * --------------------------------------------------------------------
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
*/


#ifndef _GEOMETRY_H
#define _GEOMETRY_H

#include "libavoid/geomtypes.h"

namespace Avoid {


extern double dist(const Point& a, const Point& b);
extern bool segmentIntersect(const Point& a, const Point& b,
        const Point& c, const Point& d);
extern bool inPoly(const Polygn& poly, const Point& q);
extern bool inValidRegion(bool IgnoreRegions, const Point& a0,
        const Point& a1, const Point& a2, const Point& b);


// Direction from vector.
// Looks at the position of point c from the directed segment ab and
// returns the following:
//      1   counterclockwise
//      0   collinear
//     -1   clockwise
//
// Based on the code of 'AreaSign'.
//
static inline int vecDir(const Point& a, const Point& b, const Point& c)
{
    double area2 = ((b.x - a.x) * (c.y - a.y)) -
                   ((c.x - a.x) * (b.y - a.y));
    
    if (area2 < -0.001)
    {
        return -1;
    }
    else if (area2 > 0.001)
    {
        return 1;
    }
    return 0;
}


}


#endif
