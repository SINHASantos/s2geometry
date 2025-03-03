// Copyright 2005 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS-IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// Author: ericv@google.com (Eric Veach)

#ifndef S2_S2CELL_H_
#define S2_S2CELL_H_

#include <cmath>
#include <cstdint>
#include <vector>

#include "s2/_fp_contract_off.h"  // IWYU pragma: keep
#include "s2/r2rect.h"
#include "s2/s1chord_angle.h"
#include "s2/s2cell_id.h"
#include "s2/s2coords.h"
#include "s2/s2point.h"
#include "s2/s2region.h"
#include "s2/util/coding/coder.h"
#include "s2/util/math/vector.h"

class S2Cap;
class S2LatLng;
class S2LatLngRect;

// An S2Cell is an S2Region object that represents a cell.  Unlike S2CellIds,
// it supports efficient containment and intersection tests.  However, it is
// also a more expensive representation (currently 48 bytes rather than 8).

// This class is intended to be copied by value as desired.  It uses
// the default copy constructor and assignment operator, however it is
// not a "plain old datatype" (POD) because it has virtual functions.
class S2Cell final : public S2Region {
 public:
  // Canonical identifiers for the boundaries of the cell.  It's promised that
  // both GetVertex() and GetBoundUV().GetVertex() return vertices in this
  // order.
  //
  // That is, for a given boundary k, the edge defining the boundary is:
  //   {GetVertex(k), GetVertex(k+1)}
  //
  // The boundaries are defined in UV coordinates.  The orientation may be
  // rotated relative to other face cells, but are consistent within a face
  // (i.e. a cell's left edge is its left-ward neighbor's right edge).
  //
  enum Boundary {
    // clang-format off
    kBottomEdge = 0,
    kRightEdge  = 1,
    kTopEdge    = 2,
    kLeftEdge   = 3
    // clang-format on
  };

  // The default constructor is required in order to use freelists.
  // Cells should otherwise always be constructed explicitly.
  S2Cell() = default;

  // An S2Cell always corresponds to a particular S2CellId.  The other
  // constructors are just convenience methods.
  explicit S2Cell(S2CellId id);

  // Convenience constructors.  The S2LatLng must be normalized.
  explicit S2Cell(const S2Point& p) : S2Cell(S2CellId(p)) {}
  explicit S2Cell(const S2LatLng& ll) : S2Cell(S2CellId(ll)) {}

  // Returns the cell corresponding to the given S2 cube face.
  static S2Cell FromFace(int face) {
    return S2Cell(S2CellId::FromFace(face));
  }

  // Returns a cell given its face (range 0..5), Hilbert curve position within
  // that face (an unsigned integer with S2CellId::kPosBits bits), and level
  // (range 0..kMaxLevel).  The given position will be modified to correspond
  // to the Hilbert curve position at the center of the returned cell.  This
  // is a static function rather than a constructor in order to indicate what
  // the arguments represent.
  static S2Cell FromFacePosLevel(int face, uint64_t pos, int level) {
    return S2Cell(S2CellId::FromFacePosLevel(face, pos, level));
  }

  S2CellId id() const { return id_; }
  int face() const { return face_; }
  int level() const { return level_; }
  int orientation() const { return orientation_; }
  bool is_leaf() const { return level_ == S2CellId::kMaxLevel; }

  // These are equivalent to the S2CellId methods, but have a more efficient
  // implementation since the level has been precomputed.
  int GetSizeIJ() const;
  double GetSizeST() const;

  // Returns the k-th vertex of the cell (k = 0,1,2,3).  Vertices are returned
  // in CCW order (lower left, lower right, upper right, upper left in the UV
  // plane).  The points returned by GetVertexRaw are not normalized.
  // For convenience, the argument is reduced modulo 4 to the range [0..3].
  S2Point GetVertex(int k) const { return GetVertexRaw(k).Normalize(); }
  S2Point GetVertexRaw(int k) const {
    return S2::FaceUVtoXYZ(face_, uv_.GetVertex(k));
  }

  // Returns either U or V for the given edge, whichever is constant along it.
  //
  // E.g. boundaries 0 and 2 are constant in the V axis so we return those
  // coordinates, but boundaries 1 and 3 are constant in the U axis, so we
  // return those coordinates.
  //
  // For convenience, the argument is reduced modulo 4 to the range [0..3].
  double GetUVCoordOfEdge(int k) const {
    k %= 4;
    if (k % 2 == 0) {
      return GetBoundUV().GetVertex(k).y();
    }
    return GetBoundUV().GetVertex(k).x();
  }

  // Returns either I or J for the given edge, whichever is constant along it.
  //
  // E.g. boundaries 0 and 2 are constant in the J axis so we return those
  // coordinates, but boundaries 1 and 3 are constant in the I axis, so we
  // return those coordinates.
  //
  // The returned value is not clamped to S2::kLimitIJ-1 as in S2::StToIJ, so
  // that cell edges at the maximum extent of a face are properly returned as
  // S2::kLimitIJ.
  //
  // For convenience, the argument is reduced modulo 4 to the range [0..3].
  int32_t GetIJCoordOfEdge(int k) const {
    // We can just convert UV->ST->IJ for this because the IJ coordinates only
    // have 30 bits of resolution in each axis.  The error in the conversion
    // will be a couple of epsilon which is <<< 2^-30, so if we use a proper
    // round-to-nearest operation, we'll always round to the correct IJ value.
    //
    // But, we need to explicitly use round here since Mathutil::FastIntRound
    // rounds differently on different platforms.  If we land on 0, we may end
    // up rounding to -1.
    //
    // Intel CPUs that support SSE4.1 have the ROUNDSD instruction, and ARM CPUs
    // with VFP have the VCVT instruction, both of which can implement correct
    // rounding efficiently regardless of the current FPU rounding mode.
    return std::round(S2::kLimitIJ * S2::UVtoST(GetUVCoordOfEdge(k)));
  }

  // Returns the inward-facing normal of the great circle passing through the
  // edge from vertex k to vertex k+1 (mod 4).  The normals returned by
  // GetEdgeRaw are not necessarily unit length, but their length is bounded by
  // sqrt(2) since the worst case is two components of magnitude 1.
  //
  // The vertices returned by GetVertex are not guaranteed to actually be on the
  // boundary of the cell exactly.  Instead, they're the nearest representable
  // point to the corner.
  //
  // Cell edge normals returned by GetEdgeRaw, however, are computed exactly and
  // can be used with exact predicates to determine spatial relationships to the
  // cell exactly.
  //
  // GetEdge() normalizes it's return value and thus may no longer be exact.
  //
  // For convenience, the argument is reduced modulo 4 to the range [0..3].
  S2Point GetEdge(int k) const { return GetEdgeRaw(k).Normalize(); }
  S2Point GetEdgeRaw(int k) const;

  // If this is not a leaf cell, sets children[0..3] to the four children of
  // this cell (in traversal order) and return true.  Otherwise returns false.
  // This method is equivalent to the following:
  //
  // for (pos=0, id=child_begin(); id != child_end(); id = id.next(), ++pos)
  //   children[pos] = S2Cell(id);
  //
  // except that it is more than two times faster.
  bool Subdivide(S2Cell children[4]) const;

  // Returns the direction vector corresponding to the center in (s,t)-space of
  // the given cell.  This is the point at which the cell is divided into four
  // subcells; it is not necessarily the centroid of the cell in (u,v)-space
  // or (x,y,z)-space.  The point returned by GetCenterRaw is not necessarily
  // unit length.
  S2Point GetCenter() const { return GetCenterRaw().Normalize(); }
  S2Point GetCenterRaw() const;

  // Returns the average area for cells at the given level.
  static double AverageArea(int level);

  // Returns the average area of cells at this level in steradians.  This is
  // accurate to within a factor of 1.7 (for S2_QUADRATIC_PROJECTION) and is
  // extremely cheap to compute.
  double AverageArea() const { return AverageArea(level_); }

  // Returns the approximate area of this cell in steradians.  This method is
  // accurate to within 3% percent for all cell sizes and accurate to within
  // 0.1% for cells at level 5 or higher (i.e. squares 350km to a side or
  // smaller on the Earth's surface).  It is moderately cheap to compute.
  double ApproxArea() const;

  // Returns the area of this cell as accurately as possible.  This method is
  // more expensive but it is accurate to 6 digits of precision even for leaf
  // cells (whose area is approximately 1e-18).
  double ExactArea() const;

  // Returns the bounds of this cell in (u,v)-space.
  R2Rect GetBoundUV() const { return uv_; }

  // Returns the distance from the cell to the given point.  Returns zero if
  // the point is inside the cell.
  S1ChordAngle GetDistance(const S2Point& target) const;

  // Return the distance from the cell boundary to the given point.
  S1ChordAngle GetBoundaryDistance(const S2Point& target) const;

  // Returns the maximum distance from the cell (including its interior) to the
  // given point.
  S1ChordAngle GetMaxDistance(const S2Point& target) const;

  // Returns the minimum distance from the cell to the given edge AB.  Returns
  // zero if the edge intersects the cell interior.
  S1ChordAngle GetDistance(const S2Point& a, const S2Point& b) const;

  // Returns the maximum distance from the cell (including its interior) to the
  // given edge AB.
  S1ChordAngle GetMaxDistance(const S2Point& a, const S2Point& b) const;

  // Returns the distance from the cell to the given cell.  Returns zero if
  // one cell contains the other.
  S1ChordAngle GetDistance(const S2Cell& target) const;

  // Returns the maximum distance from the cell (including its interior) to the
  // given target cell.
  S1ChordAngle GetMaxDistance(const S2Cell& target) const;

  ////////////////////////////////////////////////////////////////////////
  // S2Region interface (see s2region.h for details):

  S2Cell* Clone() const override;
  S2Cap GetCapBound() const override;
  S2LatLngRect GetRectBound() const override;
  void GetCellUnionBound(std::vector<S2CellId>* cell_ids) const override;
  bool Contains(const S2Cell& cell) const override;
  bool MayIntersect(const S2Cell& cell) const override;

  // Returns true if the cell contains the given point "p".  Note that unlike
  // S2Loop/S2Polygon, S2Cells are considered to be closed sets.  This means
  // that points along an S2Cell edge (or at a vertex) belong to the adjacent
  // cell(s) as well.
  //
  // If instead you want every point to be contained by exactly one S2Cell,
  // you will need to convert the S2Cells to S2Loops (which implement point
  // containment this way).
  //
  // The point "p" does not need to be normalized.
  bool Contains(const S2Point& p) const override;

  // Appends a serialized representation of the S2Cell to "encoder".
  //
  // REQUIRES: "encoder" uses the default constructor, so that its buffer
  //           can be enlarged as necessary by calling Ensure(int).
  void Encode(Encoder* encoder) const;

  // Decodes an S2Cell encoded with Encode().  Returns true on success.
  bool Decode(Decoder* decoder);

 private:
  // Returns the latitude or longitude of the cell vertex given by (i,j),
  // where "i" and "j" are either 0 or 1.
  double GetLatitude(int i, int j) const;
  double GetLongitude(int i, int j) const;

  S1ChordAngle VertexChordDist(const S2Point& p, int i, int j) const;
  bool UEdgeIsClosest(const S2Point& target, int v_end) const;
  bool VEdgeIsClosest(const S2Point& target, int u_end) const;

  // Returns the distance from the given point to the interior of the cell if
  // "to_interior" is true, and to the boundary of the cell otherwise.
  S1ChordAngle GetDistanceInternal(const S2Point& target_xyz,
                                   bool to_interior) const;

  // This structure occupies 44 bytes plus one pointer for the vtable.
  int8_t face_;
  int8_t level_;
  int8_t orientation_;
  S2CellId id_;
  R2Rect uv_;
};

inline bool operator==(const S2Cell& x, const S2Cell& y) {
  return x.id() == y.id();
}

inline bool operator!=(const S2Cell& x, const S2Cell& y) {
  return x.id() != y.id();
}

inline bool operator<(const S2Cell& x, const S2Cell& y) {
  return x.id() < y.id();
}

inline bool operator>(const S2Cell& x, const S2Cell& y) {
  return x.id() > y.id();
}

inline bool operator<=(const S2Cell& x, const S2Cell& y) {
  return x.id() <= y.id();
}

inline bool operator>=(const S2Cell& x, const S2Cell& y) {
  return x.id() >= y.id();
}

inline int S2Cell::GetSizeIJ() const {
  return S2CellId::GetSizeIJ(level());
}

inline double S2Cell::GetSizeST() const {
  return S2CellId::GetSizeST(level());
}

#endif  // S2_S2CELL_H_
