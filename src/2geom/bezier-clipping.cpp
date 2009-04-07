/*
 * Implement the Bezier clipping algorithm for finding
 * Bezier curve intersection points and collinear normals
 *
 * Authors:
 *      Marco Cecchetti <mrcekets at gmail.com>
 *
 * Copyright 2008  authors
 *
 * This library is free software; you can redistribute it and/or
 * modify it either under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (the "LGPL") or, at your option, under the terms of the Mozilla
 * Public License Version 1.1 (the "MPL"). If you do not alter this
 * notice, a recipient may use your version of this file under either
 * the MPL or the LGPL.
 *
 * You should have received a copy of the LGPL along with this library
 * in the file COPYING-LGPL-2.1; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * You should have received a copy of the MPL along with this library
 * in the file COPYING-MPL-1.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY
 * OF ANY KIND, either express or implied. See the LGPL or the MPL for
 * the specific language governing rights and limitations.
 */




#include <2geom/basic-intersection.h>
#include <2geom/choose.h>
#include <2geom/point.h>
#include <2geom/interval.h>
#include <2geom/bezier.h>
//#include <2geom/convex-cover.h>
#include <2geom/numeric/matrix.h>

#include <cassert>
#include <vector>
#include <algorithm>
#include <utility>
//#include <iomanip>




#define VERBOSE 0
#define CHECK 0


namespace Geom {

namespace detail { namespace bezier_clipping {

////////////////////////////////////////////////////////////////////////////////
// for debugging
//

inline
void print(std::vector<Point> const& cp, const char* msg = "")
{
    std::cerr << msg << std::endl;
    for (size_t i = 0; i < cp.size(); ++i)
        std::cerr << i << " : " << cp[i] << std::endl;
}

template< class charT >
inline
std::basic_ostream<charT> &
operator<< (std::basic_ostream<charT> & os, const Interval & I)
{
    os << "[" << I.min() << ", " << I.max() << "]";
    return os;
}

inline
double angle (std::vector<Point> const& A)
{
    size_t n = A.size() -1;
    double a = std::atan2(A[n][Y] - A[0][Y], A[n][X] - A[0][X]);
    return (180 * a / M_PI);
}

inline
size_t get_precision(Interval const& I)
{
    double d = I.extent();
    double e = 0.1, p = 10;
    int n = 0;
    while (n < 16 && d < e)
    {
        p *= 10;
        e = 1/p;
        ++n;
    }
    return n;
}

inline
void range_assertion(int k, int m, int n, const char* msg)
{
    if ( k < m || k > n)
    {
        std::cerr << "range assertion failed: \n"
                  << msg << std::endl
                  << "value: " << k
                  << "  range: " << m << ", " << n << std::endl;
        assert (k >= m && k <= n);
    }
}


////////////////////////////////////////////////////////////////////////////////
//  convex hull

/*
 * return true in case the oriented polyline p0, p1, p2 is a right turn
 */
inline
bool is_a_right_turn (Point const& p0, Point const& p1, Point const& p2)
{
    if (p1 == p2) return false;
    Point q1 = p1 - p0;
    Point q2 = p2 - p0;
    if (q1 == -q2) return false;
    return (cross (q1, q2) < 0);
}

/*
 * return true if p < q wrt the lexicographyc order induced by the coordinates
 */
struct lex_less
{
    bool operator() (Point const& p, Point const& q)
    {
      return ((p[X] < q[X]) || (p[X] == q[X] && p[Y] < q[Y]));
    }
};

/*
 * return true if p > q wrt the lexicographyc order induced by the coordinates
 */
struct lex_greater
{
    bool operator() (Point const& p, Point const& q)
    {
        return ((p[X] > q[X]) || (p[X] == q[X] && p[Y] > q[Y]));
    }
};

/*
 * Compute the convex hull of a set of points.
 * The implementation is based on the Andrew's scan algorithm
 * note: in the Bezier clipping for collinear normals it seems
 * to be more stable wrt the Graham's scan algorithm and in general
 * a bit quikier
 */
void convex_hull (std::vector<Point> & P)
{
    size_t n = P.size();
    if (n < 2)  return;
    std::sort(P.begin(), P.end(), lex_less());
    if (n < 4) return;
    // upper hull
    size_t u = 2;
    for (size_t i = 2; i < n; ++i)
    {
        while (u > 1 && !is_a_right_turn(P[u-2], P[u-1], P[i]))
        {
            --u;
        }
        std::swap(P[u], P[i]);
        ++u;
    }
    std::sort(P.begin() + u, P.end(), lex_greater());
    std::rotate(P.begin(), P.begin() + 1, P.end());
    // lower hull
    size_t l = u;
    size_t k = u - 1;
    for (size_t i = l; i < n; ++i)
    {
        while (l > k && !is_a_right_turn(P[l-2], P[l-1], P[i]))
        {
            --l;
        }
        std::swap(P[l], P[i]);
        ++l;
    }
    P.resize(l);
}


////////////////////////////////////////////////////////////////////////////////
//  numerical routines

/*
 * Compute the binomial coefficient (n, k)
 */
inline
double binomial(unsigned int n, unsigned int k)
{
    return choose<double>(n, k);
}

/*
 * Compute the determinant of the 2x2 matrix with column the point P1, P2
 */
inline
double det(Point const& P1, Point const& P2)
{
    return P1[X]*P2[Y] - P1[Y]*P2[X];
}

/*
 * Solve the linear system [P1,P2] * P = Q
 * in case there isn't exactly one solution the routine returns false
 */
inline
bool solve(Point & P, Point const& P1, Point const& P2, Point const& Q)
{
    double d = det(P1, P2);
    if (d == 0)  return false;
    d = 1 / d;
    P[X] = det(Q, P2) * d;
    P[Y] = det(P1, Q) * d;
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// interval routines

/*
 * Map the sub-interval I in [0,1] into the interval J and assign it to J
 */
inline
void map_to(Interval & J, Interval const& I)
{
    double length = J.extent();
    J[1] = I.max() * length + J[0];
    J[0] = I.min() * length + J[0];
}

/*
 * The interval [1,0] is used to represent the empty interval, this routine
 * is just an helper function for creating such an interval
 */
inline
Interval make_empty_interval()
{
    Interval I(0);
    I[0] = 1;
    return I;
}


////////////////////////////////////////////////////////////////////////////////
// bezier curve routines

/*
 * Return true if all the Bezier curve control points are near,
 * false otherwise
 */
inline
bool is_constant(std::vector<Point> const& A, double precision = EPSILON)
{
    for (unsigned int i = 1; i < A.size(); ++i)
    {
        if(!are_near(A[i], A[0], precision))
            return false;
    }
    return true;
}

/*
 * Compute the hodograph of the bezier curve B and return it in D
 */
inline
void derivative(std::vector<Point> & D, std::vector<Point> const& B)
{
    D.clear();
    size_t sz = B.size();
    if (sz == 0) return;
    if (sz == 1)
    {
        D.resize(1, Point(0,0));
        return;
    }
    size_t n = sz-1;
    D.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        D.push_back(n*(B[i+1] - B[i]));
    }
}

/*
 * Compute the hodograph of the Bezier curve B rotated of 90 degree
 * and return it in D; we have N(t) orthogonal to B(t) for any t
 */
inline
void normal(std::vector<Point> & N, std::vector<Point> const& B)
{
    derivative(N,B);
    for (size_t i = 0; i < N.size(); ++i)
    {
        N[i] = rot90(N[i]);
    }
}

/*
 *  Compute the portion of the Bezier curve "B" wrt the interval [0,t]
 */
inline
void left_portion(Coord t, std::vector<Point> & B)
{
    size_t n = B.size();
    for (size_t i = 1; i < n; ++i)
    {
        for (size_t j = n-1; j > i-1 ; --j)
        {
            B[j] = lerp(t, B[j-1], B[j]);
        }
    }
}

/*
 *  Compute the portion of the Bezier curve "B" wrt the interval [t,1]
 */
inline
void right_portion(Coord t, std::vector<Point> & B)
{
    size_t n = B.size();
    for (size_t i = 1; i < n; ++i)
    {
        for (size_t j = 0; j < n-i; ++j)
        {
            B[j] = lerp(t, B[j], B[j+1]);
        }
    }
}

/*
 *  Compute the portion of the Bezier curve "B" wrt the interval "I"
 */
inline
void portion (std::vector<Point> & B , Interval const& I)
{
    if (I.min() == 0)
    {
        if (I.max() == 1)  return;
        left_portion(I.max(), B);
        return;
    }
    right_portion(I.min(), B);
    if (I.max() == 1)  return;
    double t = I.extent() / (1 - I.min());
    left_portion(t, B);
}


////////////////////////////////////////////////////////////////////////////////
// tags

struct intersection_point_tag;
struct collinear_normal_tag;
template <typename Tag>
void clip(Interval & dom,
          std::vector<Point> const& A,
          std::vector<Point> const& B);
template <typename Tag>
void iterate(std::vector<Interval>& domsA,
             std::vector<Interval>& domsB,
             std::vector<Point> const& A,
             std::vector<Point> const& B,
             Interval const& domA,
             Interval const& domB,
             double precision );


////////////////////////////////////////////////////////////////////////////////
// intersection

/*
 *  Make up an orientation line using the control points c[i] and c[j]
 *  the line is returned in the output parameter "l" in the form of a 3 element
 *  vector : l[0] * x + l[1] * y + l[2] == 0; the line is normalized.
 */
inline
void orientation_line (std::vector<double> & l,
                       std::vector<Point> const& c,
                       size_t i, size_t j)
{
    l[0] = c[j][Y] - c[i][Y];
    l[1] = c[i][X] - c[j][X];
    l[2] = cross(c[i], c[j]);
    double length = std::sqrt(l[0] * l[0] + l[1] * l[1]);
    assert (length != 0);
    l[0] /= length;
    l[1] /= length;
    l[2] /= length;
}

/*
 * Pick up an orientation line for the Bezier curve "c" and return it in
 * the output parameter "l"
 */
inline
void pick_orientation_line (std::vector<double> & l,
                            std::vector<Point> const& c)
{
    size_t i = c.size();
    while (--i > 0 && are_near(c[0], c[i]))
    {}
    if (i == 0)
    {
        // this should never happen because when a new curve portion is created
        // we check that it is not constant;
        // however this requires that the precision used in the is_constant
        // routine has to be the same used here in the are_near test
        assert(i != 0);
    }
    orientation_line(l, c, 0, i);
    //std::cerr << "i = " << i << std::endl;
}

/*
 *  Make up an orientation line for constant bezier curve;
 *  the orientation line is made up orthogonal to the other curve base line;
 *  the line is returned in the output parameter "l" in the form of a 3 element
 *  vector : l[0] * x + l[1] * y + l[2] == 0; the line is normalized.
 */
inline
void orthogonal_orientation_line (std::vector<double> & l,
                                  std::vector<Point> const& c,
                                  Point const& p)
{
    if (is_constant(c))
    {
        // this should never happen
        assert(!is_constant(c));
    }
    std::vector<Point> ol(2);
    ol[0] = p;
    ol[1] = (c.back() - c.front()).cw() + p;
    orientation_line(l, ol, 0, 1);
}

/*
 *  Compute the signed distance of the point "P" from the normalized line l
 */
inline
double distance (Point const& P, std::vector<double> const& l)
{
    return l[X] * P[X] + l[Y] * P[Y] + l[2];
}

/*
 * Compute the min and max distance of the control points of the Bezier
 * curve "c" from the normalized orientation line "l".
 * This bounds are returned through the output Interval parameter"bound".
 */
inline
void fat_line_bounds (Interval& bound,
                      std::vector<Point> const& c,
                      std::vector<double> const& l)
{
    bound[0] = 0;
    bound[1] = 0;
    double d;
    for (size_t i = 0; i < c.size(); ++i)
    {
        d = distance(c[i], l);
        if (bound[0] > d)  bound[0] = d;
        if (bound[1] < d)  bound[1] = d;
    }
}

/*
 * return the x component of the intersection point between the line
 * passing through points p1, p2 and the line Y = "y"
 */
inline
double intersect (Point const& p1, Point const& p2, double y)
{
    // we are sure that p2[Y] != p1[Y] because this routine is called
    // only when the lower or the upper bound is crossed
    double dy = (p2[Y] - p1[Y]);
    double s = (y - p1[Y]) / dy;
    return (p2[X]-p1[X])*s + p1[X];
}

/*
 * Clip the Bezier curve "B" wrt the fat line defined by the orientation
 * line "l" and the interval range "bound", the new parameter interval for
 * the clipped curve is returned through the output parameter "dom"
 */
void clip_interval (Interval& dom,
                    std::vector<Point> const& B,
                    std::vector<double> const& l,
                    Interval const& bound)
{
    double n = B.size() - 1;  // number of sub-intervals
    std::vector<Point> D;     // distance curve control points
    D.reserve (B.size());
    double d;
    for (size_t i = 0; i < B.size(); ++i)
    {
        d = distance (B[i], l);
        D.push_back (Point(i/n, d));
    }
    //print(D);

    convex_hull(D);
    std::vector<Point> & p = D;
    //print(p);

    bool plower, phigher;
    bool clower, chigher;
    double t, tmin = 1, tmax = 0;
//    std::cerr << "bound : " << bound << std::endl;

    plower = (p[0][Y] < bound.min());
    phigher = (p[0][Y] > bound.max());
    if (!(plower || phigher))  // inside the fat line
    {
        if (tmin > p[0][X])  tmin = p[0][X];
        if (tmax < p[0][X])  tmax = p[0][X];
//        std::cerr << "0 : inside " << p[0]
//                  << " : tmin = " << tmin << ", tmax = " << tmax << std::endl;
    }

    for (size_t i = 1; i < p.size(); ++i)
    {
        clower = (p[i][Y] < bound.min());
        chigher = (p[i][Y] > bound.max());
        if (!(clower || chigher))  // inside the fat line
        {
            if (tmin > p[i][X])  tmin = p[i][X];
            if (tmax < p[i][X])  tmax = p[i][X];
//            std::cerr << i << " : inside " << p[i]
//                      << " : tmin = " << tmin << ", tmax = " << tmax
//                      << std::endl;
        }
        if (clower != plower)  // cross the lower bound
        {
            t = intersect(p[i-1], p[i], bound.min());
            if (tmin > t)  tmin = t;
            if (tmax < t)  tmax = t;
            plower = clower;
//            std::cerr << i << " : lower " << p[i]
//                      << " : tmin = " << tmin << ", tmax = " << tmax
//                      << std::endl;
        }
        if (chigher != phigher)  // cross the upper bound
        {
            t = intersect(p[i-1], p[i], bound.max());
            if (tmin > t)  tmin = t;
            if (tmax < t)  tmax = t;
            phigher = chigher;
//            std::cerr << i << " : higher " << p[i]
//                      << " : tmin = " << tmin << ", tmax = " << tmax
//                      << std::endl;
        }
    }

    // we have to test the closing segment for intersection
    size_t last = p.size() - 1;
    clower = (p[0][Y] < bound.min());
    chigher = (p[0][Y] > bound.max());
    if (clower != plower)  // cross the lower bound
    {
        t = intersect(p[last], p[0], bound.min());
        if (tmin > t)  tmin = t;
        if (tmax < t)  tmax = t;
//        std::cerr << "0 : lower " << p[0]
//                  << " : tmin = " << tmin << ", tmax = " << tmax << std::endl;
    }
    if (chigher != phigher)  // cross the upper bound
    {
        t = intersect(p[last], p[0], bound.max());
        if (tmin > t)  tmin = t;
        if (tmax < t)  tmax = t;
//        std::cerr << "0 : higher " << p[0]
//                  << " : tmin = " << tmin << ", tmax = " << tmax << std::endl;
    }

    dom[0] = tmin;
    dom[1] = tmax;
}

/*
 *  Clip the Bezier curve "B" wrt the Bezier curve "A" for individuating
 *  intersection points the new parameter interval for the clipped curve
 *  is returned through the output parameter "dom"
 */
template <>
inline
void clip<intersection_point_tag> (Interval & dom,
                                   std::vector<Point> const& A,
                                   std::vector<Point> const& B)
{
    std::vector<double> bl(3);
    Interval bound;
    if (is_constant(A))
    {
        Point M = middle_point(A.front(), A.back());
        orthogonal_orientation_line(bl, B, M);
    }
    else
    {
        pick_orientation_line(bl, A);
    }
    fat_line_bounds(bound, A, bl);
    clip_interval(dom, B, bl, bound);
}


///////////////////////////////////////////////////////////////////////////////
// collinear normal

/*
 * Compute a closed focus for the Bezier curve B and return it in F
 * A focus is any curve through which all lines perpendicular to B(t) pass.
 */
inline
void make_focus (std::vector<Point> & F, std::vector<Point> const& B)
{
    assert (B.size() > 2);
    size_t n = B.size() - 1;
    normal(F, B);
    Point c(1, 1);
#if VERBOSE
    if (!solve(c, F[0], -F[n-1], B[n]-B[0]))
    {
        std::cerr << "make_focus: unable to make up a closed focus" << std::endl;
    }
#else
    solve(c, F[0], -F[n-1], B[n]-B[0]);
#endif
//    std::cerr << "c = " << c << std::endl;


    // B(t) + c(t) * N(t)
    double n_inv = 1 / (double)(n);
    Point c0ni;
    F.push_back(c[1] * F[n-1]);
    F[n] += B[n];
    for (size_t i = n-1; i > 0; --i)
    {
        F[i] *= -c[0];
        c0ni = F[i];
        F[i] += (c[1] * F[i-1]);
        F[i] *= (i * n_inv);
        F[i] -= c0ni;
        F[i] += B[i];
    }
    F[0] *= c[0];
    F[0] += B[0];
}

/*
 * Compute the projection on the plane (t, d) of the control points
 * (t, u, D(t,u)) where D(t,u) = <(B(t) - F(u)), B'(t)> with 0 <= t, u <= 1
 * B is a Bezier curve and F is a focus of another Bezier curve.
 * See Sederberg, Nishita, 1990 - Curve intersection using Bezier clipping.
 */
void distance_control_points (std::vector<Point> & D,
                              std::vector<Point> const& B,
                              std::vector<Point> const& F)
{
    assert (B.size() > 1);
    assert (F.size() > 0);
    const size_t n = B.size() - 1;
    const size_t m = F.size() - 1;
    const size_t r = 2 * n - 1;
    const double r_inv = 1 / (double)(r);
    D.clear();
    D.reserve (B.size() * F.size());

    std::vector<Point> dB;
    dB.reserve(n);
    for (size_t k = 0; k < n; ++k)
    {
        dB.push_back (B[k+1] - B[k]);
    }
    NL::Matrix dBB(n,B.size());
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < B.size(); ++j)
            dBB(i,j) = dot (dB[i], B[j]);
    NL::Matrix dBF(n, F.size());
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < F.size(); ++j)
            dBF(i,j) = dot (dB[i], F[j]);

    size_t k0, kn, l;
    double bc, bri;
    Point dij;
    std::vector<double> d(F.size());
    for (size_t i = 0; i <= r; ++i)
    {
        for (size_t j = 0; j <= m; ++j)
        {
            d[j] = 0;
        }
        k0 = std::max(i, n) - n;
        kn = std::min(i, n-1);
        bri = n / binomial(r,i);
        for (size_t k = k0; k <= kn; ++k)
        {
            //if (k > i || (i-k) > n) continue;
            l = i - k;
#if CHECK
            assert (l <= n);
#endif
            bc = bri * binomial(n,l) * binomial(n-1, k);
            for (size_t j = 0; j <= m; ++j)
            {
                //d[j] += bc * dot(dB[k], B[l] - F[j]);
                d[j] += bc * (dBB(k,l) - dBF(k,j));
            }
        }
        double dmin, dmax;
        dmin = dmax = d[m];
        for (size_t j = 0; j < m; ++j)
        {
            if (dmin > d[j])  dmin = d[j];
            if (dmax < d[j])  dmax = d[j];
        }
        dij[0] = i * r_inv;
        dij[1] = dmin;
        D.push_back (dij);
        dij[1] = dmax;
        D.push_back (dij);
    }
}

/*
 * Clip the Bezier curve "B" wrt the focus "F"; the new parameter interval for
 * the clipped curve is returned through the output parameter "dom"
 */
void clip_interval (Interval& dom,
                    std::vector<Point> const& B,
                    std::vector<Point> const& F)
{
    std::vector<Point> D;     // distance curve control points
    distance_control_points(D, B, F);
    //print(D, "D");
//    ConvexHull chD(D);
//    std::vector<Point>& p = chD.boundary; // convex hull vertices

    convex_hull(D);
    std::vector<Point> & p = D;
    //print(p, "CH(D)");

    bool plower, clower;
    double t, tmin = 1, tmax = 0;

    plower = (p[0][Y] < 0);
    if (p[0][Y] == 0)  // on the x axis
    {
        if (tmin > p[0][X])  tmin = p[0][X];
        if (tmax < p[0][X])  tmax = p[0][X];
//        std::cerr << "0 : on x axis " << p[0]
//                  << " : tmin = " << tmin << ", tmax = " << tmax << std::endl;
    }

    for (size_t i = 1; i < p.size(); ++i)
    {
        clower = (p[i][Y] < 0);
        if (p[i][Y] == 0)  // on x axis
        {
            if (tmin > p[i][X])  tmin = p[i][X];
            if (tmax < p[i][X])  tmax = p[i][X];
//            std::cerr << i << " : on x axis " << p[i]
//                      << " : tmin = " << tmin << ", tmax = " << tmax
//                      << std::endl;
        }
        else if (clower != plower)  // cross the x axis
        {
            t = intersect(p[i-1], p[i], 0);
            if (tmin > t)  tmin = t;
            if (tmax < t)  tmax = t;
            plower = clower;
//            std::cerr << i << " : lower " << p[i]
//                      << " : tmin = " << tmin << ", tmax = " << tmax
//                      << std::endl;
        }
    }

    // we have to test the closing segment for intersection
    size_t last = p.size() - 1;
    clower = (p[0][Y] < 0);
    if (clower != plower)  // cross the x axis
    {
        t = intersect(p[last], p[0], 0);
        if (tmin > t)  tmin = t;
        if (tmax < t)  tmax = t;
//        std::cerr << "0 : lower " << p[0]
//                  << " : tmin = " << tmin << ", tmax = " << tmax << std::endl;
    }
    dom[0] = tmin;
    dom[1] = tmax;
}

/*
 *  Clip the Bezier curve "B" wrt the Bezier curve "A" for individuating
 *  points which have collinear normals; the new parameter interval
 *  for the clipped curve is returned through the output parameter "dom"
 */
template <>
inline
void clip<collinear_normal_tag> (Interval & dom,
                                 std::vector<Point> const& A,
                                 std::vector<Point> const& B)
{
    std::vector<Point> F;
    make_focus(F, A);
    clip_interval(dom, B, F);
}



const double MAX_PRECISION = 1e-8;
const double MIN_CLIPPED_SIZE_THRESHOLD = 0.8;
const Interval UNIT_INTERVAL(0,1);
const Interval EMPTY_INTERVAL = make_empty_interval();
const Interval H1_INTERVAL(0, 0.5);
const Interval H2_INTERVAL(0.5 + MAX_PRECISION, 1.0);

/*
 * iterate
 *
 * input:
 * A, B: control point sets of two bezier curves
 * domA, domB: real parameter intervals of the two curves
 * precision: required computational precision of the returned parameter ranges
 * output:
 * domsA, domsB: sets of parameter intervals
 *
 * The parameter intervals are computed by using a Bezier clipping algorithm,
 * in case the clipping doesn't shrink the initial interval more than 20%,
 * a subdivision step is performed.
 * If during the computation both curves collapse to a single point
 * the routine exits indipendently by the precision reached in the computation
 * of the curve intervals.
 */
template <>
void iterate<intersection_point_tag> (std::vector<Interval>& domsA,
                                      std::vector<Interval>& domsB,
                                      std::vector<Point> const& A,
                                      std::vector<Point> const& B,
                                      Interval const& domA,
                                      Interval const& domB,
                                      double precision )
{
    // in order to limit recursion
    static size_t counter = 0;
    if (domA.extent() == 1 && domB.extent() == 1) counter  = 0;
    if (++counter > 100) return;
#if VERBOSE
    std::cerr << std::fixed << std::setprecision(16);
    std::cerr << ">> curve subdision performed <<" << std::endl;
    std::cerr << "dom(A) : " << domA << std::endl;
    std::cerr << "dom(B) : " << domB << std::endl;
//    std::cerr << "angle(A) : " << angle(A) << std::endl;
//    std::cerr << "angle(B) : " << angle(B) << std::endl;
#endif

    if (precision < MAX_PRECISION)
        precision = MAX_PRECISION;

    std::vector<Point> pA = A;
    std::vector<Point> pB = B;
    std::vector<Point>* C1 = &pA;
    std::vector<Point>* C2 = &pB;

    Interval dompA = domA;
    Interval dompB = domB;
    Interval* dom1 = &dompA;
    Interval* dom2 = &dompB;

    Interval dom;

    if ( is_constant(A) && is_constant(B) ){
        Point M1 = middle_point(C1->front(), C1->back());
        Point M2 = middle_point(C2->front(), C2->back());
        if (are_near(M1,M2)){
            domsA.push_back(domA);
            domsB.push_back(domB);
        }
        return;
    }

    size_t iter = 0;
    while (++iter < 100
            && (dompA.extent() >= precision || dompB.extent() >= precision))
    {
#if VERBOSE
        std::cerr << "iter: " << iter << std::endl;
#endif
        clip<intersection_point_tag>(dom, *C1, *C2);

        // [1,0] is utilized to represent an empty interval
        if (dom == EMPTY_INTERVAL)
        {
#if VERBOSE
            std::cerr << "dom: empty" << std::endl;
#endif
            return;
        }
#if VERBOSE
        std::cerr << "dom : " << dom << std::endl;
#endif
        // all other cases where dom[0] > dom[1] are invalid
        if (dom.min() >  dom.max())
        {
            assert(dom.min() <  dom.max());
        }

        map_to(*dom2, dom);

        portion(*C2, dom);
        if (is_constant(*C2) && is_constant(*C1))
        {
            Point M1 = middle_point(C1->front(), C1->back());
            Point M2 = middle_point(C2->front(), C2->back());
#if VERBOSE
            std::cerr << "both curves are constant: \n"
                      << "M1: " << M1 << "\n"
                      << "M2: " << M2 << std::endl;
            print(*C2, "C2");
            print(*C1, "C1");
#endif
            if (are_near(M1,M2))
                break;  // append the new interval
            else
                return; // exit without appending any new interval
        }


        // if we have clipped less than 20% than we need to subdive the curve
        // with the largest domain into two sub-curves
        if ( dom.extent() > MIN_CLIPPED_SIZE_THRESHOLD)
        {
#if VERBOSE
            std::cerr << "clipped less than 20% : " << dom.extent() << std::endl;
            std::cerr << "angle(pA) : " << angle(pA) << std::endl;
            std::cerr << "angle(pB) : " << angle(pB) << std::endl;
#endif
            std::vector<Point> pC1, pC2;
            Interval dompC1, dompC2;
            if (dompA.extent() > dompB.extent())
            {
                pC1 = pC2 = pA;
                portion(pC1, H1_INTERVAL);
                portion(pC2, H2_INTERVAL);
                dompC1 = dompC2 = dompA;
                map_to(dompC1, H1_INTERVAL);
                map_to(dompC2, H2_INTERVAL);
                iterate<intersection_point_tag>(domsA, domsB, pC1, pB,
                                                dompC1, dompB, precision);
                iterate<intersection_point_tag>(domsA, domsB, pC2, pB,
                                                dompC2, dompB, precision);
            }
            else
            {
                pC1 = pC2 = pB;
                portion(pC1, H1_INTERVAL);
                portion(pC2, H2_INTERVAL);
                dompC1 = dompC2 = dompB;
                map_to(dompC1, H1_INTERVAL);
                map_to(dompC2, H2_INTERVAL);
                iterate<intersection_point_tag>(domsB, domsA, pC1, pA,
                                                dompC1, dompA, precision);
                iterate<intersection_point_tag>(domsB, domsA, pC2, pA,
                                                dompC2, dompA, precision);
            }
            return;
        }

        std::swap(C1, C2);
        std::swap(dom1, dom2);
#if VERBOSE
        std::cerr << "dom(pA) : " << dompA << std::endl;
        std::cerr << "dom(pB) : " << dompB << std::endl;
#endif
    }
    domsA.push_back(dompA);
    domsB.push_back(dompB);
}


/*
 * iterate
 *
 * input:
 * A, B: control point sets of two bezier curves
 * domA, domB: real parameter intervals of the two curves
 * precision: required computational precision of the returned parameter ranges
 * output:
 * domsA, domsB: sets of parameter intervals
 *
 * The parameter intervals are computed by using a Bezier clipping algorithm,
 * in case the clipping doesn't shrink the initial interval more than 20%,
 * a subdivision step is performed.
 * If during the computation one of the two curve interval length becomes less
 * than MAX_PRECISION the routine exits indipendently by the precision reached
 * in the computation of the other curve interval.
 */
template <>
void iterate<collinear_normal_tag> (std::vector<Interval>& domsA,
                                    std::vector<Interval>& domsB,
                                    std::vector<Point> const& A,
                                    std::vector<Point> const& B,
                                    Interval const& domA,
                                    Interval const& domB,
                                    double precision)
{
    // in order to limit recursion
    static size_t counter = 0;
    if (domA.extent() == 1 && domB.extent() == 1) counter  = 0;
    if (++counter > 100) return;
#if VERBOSE
    std::cerr << std::fixed << std::setprecision(16);
    std::cerr << ">> curve subdision performed <<" << std::endl;
    std::cerr << "dom(A) : " << domA << std::endl;
    std::cerr << "dom(B) : " << domB << std::endl;
//    std::cerr << "angle(A) : " << angle(A) << std::endl;
//    std::cerr << "angle(B) : " << angle(B) << std::endl;
#endif

    if (precision < MAX_PRECISION)
        precision = MAX_PRECISION;

    std::vector<Point> pA = A;
    std::vector<Point> pB = B;
    std::vector<Point>* C1 = &pA;
    std::vector<Point>* C2 = &pB;

    Interval dompA = domA;
    Interval dompB = domB;
    Interval* dom1 = &dompA;
    Interval* dom2 = &dompB;

    Interval dom;

    size_t iter = 0;
    while (++iter < 100
            && (dompA.extent() >= precision || dompB.extent() >= precision))
    {
#if VERBOSE
        std::cerr << "iter: " << iter << std::endl;
#endif
        clip<collinear_normal_tag>(dom, *C1, *C2);

        // [1,0] is utilized to represent an empty interval
        if (dom == EMPTY_INTERVAL)
        {
#if VERBOSE
            std::cerr << "dom: empty" << std::endl;
#endif
            return;
        }
#if VERBOSE
        std::cerr << "dom : " << dom << std::endl;
#endif
        // all other cases where dom[0] > dom[1] are invalid
        if (dom.min() >  dom.max())
        {
            assert(dom.min() <  dom.max());
        }

        map_to(*dom2, dom);

        // it's better to stop before losing computational precision
        if (iter > 1 && (dom2->extent() <= MAX_PRECISION))
        {
#if VERBOSE
            std::cerr << "beyond max precision limit" << std::endl;
#endif
            break;
        }

        portion(*C2, dom);
        if (iter > 1 && is_constant(*C2))
        {
#if VERBOSE
            std::cerr << "new curve portion pC1 is constant" << std::endl;
#endif
            break;
        }


        // if we have clipped less than 20% than we need to subdive the curve
        // with the largest domain into two sub-curves
        if ( dom.extent() > MIN_CLIPPED_SIZE_THRESHOLD)
        {
#if VERBOSE
            std::cerr << "clipped less than 20% : " << dom.extent() << std::endl;
            std::cerr << "angle(pA) : " << angle(pA) << std::endl;
            std::cerr << "angle(pB) : " << angle(pB) << std::endl;
#endif
            std::vector<Point> pC1, pC2;
            Interval dompC1, dompC2;
            if (dompA.extent() > dompB.extent())
            {
                if ((dompA.extent() / 2) < MAX_PRECISION)
                {
                    break;
                }
                pC1 = pC2 = pA;
                portion(pC1, H1_INTERVAL);
                if (false && is_constant(pC1))
                {
#if VERBOSE
                    std::cerr << "new curve portion pC1 is constant" << std::endl;
#endif
                    break;
                }
                portion(pC2, H2_INTERVAL);
                if (is_constant(pC2))
                {
#if VERBOSE
                    std::cerr << "new curve portion pC2 is constant" << std::endl;
#endif
                    break;
                }
                dompC1 = dompC2 = dompA;
                map_to(dompC1, H1_INTERVAL);
                map_to(dompC2, H2_INTERVAL);
                iterate<collinear_normal_tag>(domsA, domsB, pC1, pB,
                                              dompC1, dompB, precision);
                iterate<collinear_normal_tag>(domsA, domsB, pC2, pB,
                                              dompC2, dompB, precision);
            }
            else
            {
                if ((dompB.extent() / 2) < MAX_PRECISION)
                {
                    break;
                }
                pC1 = pC2 = pB;
                portion(pC1, H1_INTERVAL);
                if (is_constant(pC1))
                {
#if VERBOSE
                    std::cerr << "new curve portion pC1 is constant" << std::endl;
#endif
                    break;
                }
                portion(pC2, H2_INTERVAL);
                if (is_constant(pC2))
                {
#if VERBOSE
                    std::cerr << "new curve portion pC2 is constant" << std::endl;
#endif
                    break;
                }
                dompC1 = dompC2 = dompB;
                map_to(dompC1, H1_INTERVAL);
                map_to(dompC2, H2_INTERVAL);
                iterate<collinear_normal_tag>(domsB, domsA, pC1, pA,
                                              dompC1, dompA, precision);
                iterate<collinear_normal_tag>(domsB, domsA, pC2, pA,
                                              dompC2, dompA, precision);
            }
            return;
        }

        std::swap(C1, C2);
        std::swap(dom1, dom2);
#if VERBOSE
        std::cerr << "dom(pA) : " << dompA << std::endl;
        std::cerr << "dom(pB) : " << dompB << std::endl;
#endif
    }
    domsA.push_back(dompA);
    domsB.push_back(dompB);
}


/*
 * get_solutions
 *
 *  input: A, B       - set of control points of two Bezier curve
 *  input: precision  - required precision of computation
 *  input: clip       - the routine used for clipping
 *  output: xs        - set of pairs of parameter values
 *                      at which the clipping algorithm converges
 *
 *  This routine is based on the Bezier Clipping Algorithm,
 *  see: Sederberg - Computer Aided Geometric Design
 */
template <typename Tag>
void get_solutions (std::vector< std::pair<double, double> >& xs,
                    std::vector<Point> const& A,
                    std::vector<Point> const& B,
                    double precision)
{
    std::pair<double, double> ci;
    std::vector<Interval> domsA, domsB;
    iterate<Tag> (domsA, domsB, A, B, UNIT_INTERVAL, UNIT_INTERVAL, precision);
    if (domsA.size() != domsB.size())
    {
        assert (domsA.size() == domsB.size());
    }
    xs.clear();
    xs.reserve(domsA.size());
    for (size_t i = 0; i < domsA.size(); ++i)
    {
#if VERBOSE
        std::cerr << i << " : domA : " << domsA[i] << std::endl;
        std::cerr << "extent A: " << domsA[i].extent() << "  ";
        std::cerr << "precision A: " << get_precision(domsA[i]) << std::endl;
        std::cerr << i << " : domB : " << domsB[i] << std::endl;
        std::cerr << "extent B: " << domsB[i].extent() << "  ";
        std::cerr << "precision B: " << get_precision(domsB[i]) << std::endl;
#endif
        ci.first = domsA[i].middle();
        ci.second = domsB[i].middle();
        xs.push_back(ci);
    }
}

} /* end namespace bezier_clipping */ } /* end namespace detail */


/*
 * find_collinear_normal
 *
 *  input: A, B       - set of control points of two Bezier curve
 *  input: precision  - required precision of computation
 *  output: xs        - set of pairs of parameter values
 *                      at which there are collinear normals
 *
 *  This routine is based on the Bezier Clipping Algorithm,
 *  see: Sederberg, Nishita, 1990 - Curve intersection using Bezier clipping
 */
void find_collinear_normal (std::vector< std::pair<double, double> >& xs,
                            std::vector<Point> const& A,
                            std::vector<Point> const& B,
                            double precision)
{
    using detail::bezier_clipping::get_solutions;
    using detail::bezier_clipping::collinear_normal_tag;
    get_solutions<collinear_normal_tag>(xs, A, B, precision);
}


/*
 * find_intersections_bezier_clipping
 *
 *  input: A, B       - set of control points of two Bezier curve
 *  input: precision  - required precision of computation
 *  output: xs        - set of pairs of parameter values
 *                      at which crossing happens
 *
 *  This routine is based on the Bezier Clipping Algorithm,
 *  see: Sederberg, Nishita, 1990 - Curve intersection using Bezier clipping
 */
void find_intersections_bezier_clipping (std::vector< std::pair<double, double> >& xs,
                         std::vector<Point> const& A,
                         std::vector<Point> const& B,
                         double precision)
{
    using detail::bezier_clipping::get_solutions;
    using detail::bezier_clipping::intersection_point_tag;
    get_solutions<intersection_point_tag>(xs, A, B, precision);
}

}  // end namespace Geom




/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99 :
