// Copyright 2013 Google Inc. All Rights Reserved.
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

#ifndef S2_S2LAX_POLYLINE_SHAPE_H_
#define S2_S2LAX_POLYLINE_SHAPE_H_

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/types/span.h"
#include "s2/util/coding/coder.h"
#include "s2/_fp_contract_off.h"  // IWYU pragma: keep
#include "s2/encoded_s2point_vector.h"
#include "s2/s2coder.h"
#include "s2/s2error.h"
#include "s2/s2point.h"
#include "s2/s2polyline.h"
#include "s2/s2shape.h"

// S2LaxPolylineShape represents a polyline.  It is similar to
// S2Polyline::Shape except that adjacent vertices are allowed to be identical
// or antipodal, and the representation is slightly more compact.
//
// Polylines may have any number of vertices, but note that polylines with
// fewer than 2 vertices do not define any edges.  (To create a polyline
// consisting of a single degenerate edge, either repeat the same vertex twice
// or use S2LaxClosedPolylineShape defined in s2_lax_loop_shape.h.)
class S2LaxPolylineShape : public S2Shape {
 public:
  static constexpr TypeTag kTypeTag = 4;

  typedef s2coding::S2HintCoder<S2LaxPolylineShape> Coder;

  // Constructs an empty polyline.
  S2LaxPolylineShape() = default;

  S2LaxPolylineShape(S2LaxPolylineShape&& other) noexcept;
  S2LaxPolylineShape& operator=(S2LaxPolylineShape&& other) noexcept;

  // Constructs an S2LaxPolylineShape with the given vertices.
  explicit S2LaxPolylineShape(absl::Span<const S2Point> vertices);

  // Constructs an S2LaxPolylineShape from the given S2Polyline, by copying
  // its data.
  explicit S2LaxPolylineShape(const S2Polyline& polyline);

  // Initializes an S2LaxPolylineShape with the given vertices.
  void Init(absl::Span<const S2Point> vertices);

  // Initializes an S2LaxPolylineShape from the given S2Polyline, by copying
  // its data.
  void Init(const S2Polyline& polyline);

  int num_vertices() const { return num_vertices_; }
  const S2Point& vertex(int i) const { return vertices_[i]; }

  // Appends an encoded representation of the S2LaxPolylineShape to "encoder".
  //
  // REQUIRES: "encoder" uses the default constructor, so that its buffer
  //           can be enlarged as necessary by calling Ensure(int).
  void Encode(Encoder* encoder, s2coding::CodingHint hint) const override;

  // Decodes an S2LaxPolylineShape, returning true on success.  (The method
  // name is chosen for compatibility with EncodedS2LaxPolylineShape below.)
  bool Init(Decoder* decoder);

  // Populates an S2Error when Init fails.
  bool Init(Decoder* decoder, S2Error& error);

  // S2Shape interface:
  int num_edges() const final { return std::max(0, num_vertices() - 1); }
  Edge edge(int e) const final;
  int dimension() const final { return 1; }
  ReferencePoint GetReferencePoint() const final {
    return ReferencePoint::Contained(false);
  }
  int num_chains() const final;
  Chain chain(int i) const final;
  Edge chain_edge(int i, int j) const final;
  ChainPosition chain_position(int e) const final;
  TypeTag type_tag() const override { return kTypeTag; }

 private:
  // For clients that have many small polylines, we save some memory by
  // representing the vertices as an array rather than using std::vector.
  int32_t num_vertices_ = 0;
  std::unique_ptr<S2Point[]> vertices_;
};

// Exactly like S2LaxPolylineShape, except that the vertices are kept in an
// encoded form and are decoded only as they are accessed.  This allows for
// very fast initialization and no additional memory use beyond the encoded
// data.  The encoded data is not owned by this class; typically it points
// into a large contiguous buffer that contains other encoded data as well.
class EncodedS2LaxPolylineShape : public S2Shape {
 public:
  static constexpr TypeTag kTypeTag = S2LaxPolylineShape::kTypeTag;

  // Constructs an uninitialized object; requires Init() to be called.
  EncodedS2LaxPolylineShape() = default;

  // Initializes an EncodedS2LaxPolylineShape.
  //
  // REQUIRES: The Decoder data buffer must outlive this object.
  bool Init(Decoder* decoder);

  // Appends an encoded representation of the S2LaxPolylineShape to "encoder".
  // The coding hint is ignored, and whatever method was originally used to
  // encode the shape is preserved.
  //
  // REQUIRES: "encoder" uses the default constructor, so that its buffer
  //           can be enlarged as necessary by calling Ensure(int).
  void Encode(Encoder* encoder, s2coding::CodingHint hint) const override;

  int num_vertices() const { return vertices_.size(); }
  S2Point vertex(int i) const { return vertices_[i]; }

  // S2Shape interface:
  int num_edges() const final { return std::max(0, num_vertices() - 1); }
  Edge edge(int e) const final;
  int dimension() const final { return 1; }
  ReferencePoint GetReferencePoint() const final {
    return ReferencePoint::Contained(false);
  }
  int num_chains() const final;
  Chain chain(int i) const final;
  Edge chain_edge(int i, int j) const final;
  ChainPosition chain_position(int e) const final;
  TypeTag type_tag() const override { return kTypeTag; }

 private:
  s2coding::EncodedS2PointVector vertices_;
};

#endif  // S2_S2LAX_POLYLINE_SHAPE_H_
