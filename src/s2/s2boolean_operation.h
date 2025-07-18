// Copyright 2017 Google Inc. All Rights Reserved.
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

#ifndef S2_S2BOOLEAN_OPERATION_H_
#define S2_S2BOOLEAN_OPERATION_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/log/absl_log.h"
#include "s2/_fp_contract_off.h"  // IWYU pragma: keep
#include "s2/s2builder.h"
#include "s2/s2builder_graph.h"
#include "s2/s2builder_layer.h"
#include "s2/s2error.h"
#include "s2/s2memory_tracker.h"
#include "s2/s2shape_index.h"
#include "s2/value_lexicon.h"

// This class implements boolean operations (intersection, union, difference,
// and symmetric difference) for regions whose boundaries are defined by
// geodesic edges.
//
// S2BooleanOperation operates on exactly two input regions at a time.  Each
// region is represented as an S2ShapeIndex and may contain any number of
// points, polylines, and polygons.  The region is essentially the union of
// these objects, except that polygon interiors must be disjoint from all
// other geometry (including other polygon interiors).  If the input geometry
// for a region does not meet this condition, it can be normalized by
// computing its union first.  Duplicate polygon edges are not allowed (even
// among different polygons), however polylines may have duplicate edges and
// may even be self-intersecting.  Note that points or polylines are allowed
// to coincide with the boundaries of polygons.
//
// Degeneracies are fully supported.  Supported degeneracy types include the
// following:
//
//  - Point polylines consisting of a single degenerate edge AA.
//
//  - Point loops consisting of a single vertex A.  Such loops may represent
//    either shells or holes according to whether the loop adds to or
//    subtracts from the surrounding region of the polygon.
//
//  - Sibling edge pairs of the form {AB, BA}.  Such sibling pairs may
//    represent either shells or holes according to whether they add to or
//    subtract from the surrounding region.  The edges of a sibling pair may
//    belong to the same polygon loop (e.g. a loop AB) or to different polygon
//    loops or polygons (e.g. the polygons {ABC, CBD}).
//
// A big advantage of degeneracy support is that geometry may be simplified
// without completely losing small details.  For example, if a polygon
// representing a land area with many lakes and rivers is simplified using a
// tolerance of 1 kilometer, every water feature in the input is guaranteed to
// be within 1 kilometer of some water feature in the input (even if some
// lakes and rivers are merged and/or reduced to degenerate point or sibling
// edge pair holes).  Mathematically speaking, degeneracy support allows
// geometry to be simplified while guaranteeing that the Hausdorff distance
// between the boundaries of the original and simplified geometries is at
// most the simplification tolerance.  It also allows geometry to be
// simplified without changing its dimension, thus preserving boundary
// semantics.  (Note that the boundary of a polyline ABCD is {A,D}, whereas
// the boundary of a degenerate shell ABCDCB is its entire set of vertices and
// edges.)
//
// Points and polyline edges are treated as multisets: if the same point or
// polyline edge appears multiple times in the input, it will appear multiple
// times in the output.  For example, the union of a point with an identical
// point consists of two points.  This feature is useful for modeling large
// sets of points or polylines as a single region while maintaining their
// distinct identities, even when the points or polylines intersect each
// other.  It is also useful for reconstructing polylines that loop back on
// themselves (e.g., time series such as GPS tracks).  If duplicate geometry
// is not desired, it can easily be removed by choosing the appropriate
// S2Builder output layer options.
//
// Self-intersecting polylines can be manipulated without materializing new
// vertices at the self-intersection points.  This feature is important when
// processing polylines with large numbers of self-intersections such as GPS
// tracks (e.g., consider the path of a race car in the Indy 500).
//
// Polylines are always considered to be directed.  Polyline edges between the
// same pair of vertices are defined to intersect even if the two edges are in
// opposite directions.  (Undirected polylines can be modeled by specifying
// GraphOptions::EdgeType::UNDIRECTED in the S2Builder output layer.)
//
// The output of each operation is sent to an S2Builder::Layer provided by the
// client.  This allows clients to build any representation of the geometry
// they choose.  It also allows the client to do additional postprocessing of
// the output before building data structures; for example, the client can
// easily discard degeneracies or convert them to another data type.
//
// The boundaries of polygons and polylines can be modeled as open, semi-open,
// or closed.  Polyline boundaries are controlled by the PolylineModel class,
// whose options are as follows:
//
//  - In the OPEN model, polylines do not contain their first or last vertex
//    except for one special case: namely, if the polyline forms a loop and
//    the polyline_loops_have_boundaries() option is set to false, then the
//    first/last vertex is contained.
//
//  - In the SEMI_OPEN model, polylines contain all vertices except the last.
//    Therefore if one polyline starts where another polyline stops, the two
//    polylines do not intersect.
//
//  - In the CLOSED model, polylines contain all of their vertices.
//
// When multiple polylines are present, they are processed independently and
// have no effect on each other.  For example, in the OPEN boundary model the
// polyline ABC contains the vertex B, while set of polylines {AB, BC} does
// not.  (If you want to treat the polylines as a union instead, with
// boundaries merged according to the "mod 2" rule, this can be achieved by
// reassembling the edges into maximal polylines using S2PolylineVectorLayer
// with EdgeType::UNDIRECTED, DuplicateEdges::MERGE, and PolylineType::WALK.)
//
// Polygon boundaries are controlled by the PolygonModel class, which has the
// following options:
//
//  - In the OPEN model, polygons do not contain their vertices or edges.
//    This implies that a polyline that follows the boundary of a polygon will
//    not intersect it.
//
//  - In the SEMI_OPEN model, polygon point containment is defined such that
//    if several polygons tile the region around a vertex, then exactly one of
//    those polygons contains that vertex.  Similarly polygons contain all of
//    their edges, but none of their reversed edges.  This implies that a
//    polyline and polygon edge with the same endpoints intersect if and only
//    if they are in the same direction.  (This rule ensures that if a
//    polyline is intersected with a polygon and its complement, the two
//    resulting polylines do not have any edges in common.)
//
//  - In the CLOSED model, polygons contain all their vertices, edges, and
//    reversed edges.  This implies that a polyline that shares an edge (in
//    either direction) with a polygon is defined to intersect it.  Similarly,
//    this is the only model where polygons that touch at a vertex or along an
//    edge intersect.
//
// Note that PolylineModel and PolygonModel are defined as separate classes in
// order to allow for possible future extensions.
//
// Operations between geometry of different dimensions are defined as follows:
//
//  - For UNION, the higher-dimensional shape always wins.  For example the
//    union of a closed polygon A with a polyline B that coincides with the
//    boundary of A consists only of the polygon A.
//
//  - For INTERSECTION, the lower-dimensional shape always wins.  For example,
//    the intersection of a closed polygon A with a point B that coincides
//    with a vertex of A consists only of the point B.
//
//  - For DIFFERENCE, higher-dimensional shapes are not affected by
//    subtracting lower-dimensional shapes.  For example, subtracting a point
//    or polyline from a polygon A yields the original polygon A.  This rule
//    exists because in general, it is impossible to represent the output
//    using the specified boundary model(s).  (Consider subtracting one vertex
//    from a PolylineModel::CLOSED polyline, or subtracting one edge from a
//    PolygonModel::CLOSED polygon.)  If you want to perform operations like
//    this, consider representing all boundaries explicitly (topological
//    boundaries) using OPEN boundary models.  Another option for polygons is
//    to subtract a degenerate loop, which yields a polygon with a degenerate
//    hole (see S2LaxPolygonShape).
//
// Note that in the case of Precision::EXACT operations, the above remarks
// only apply to the output before snapping.  Snapping may cause nearby
// distinct edges to become coincident, e.g. a polyline may become coincident
// with a polygon boundary.  However also note that S2BooleanOperation is
// perfectly happy to accept such geometry as input.
//
// Example usage:
//   S2ShapeIndex a, b;  // Input geometry, e.g. containing polygons.
//   S2Polygon polygon;  // Output geometry.
//   S2BooleanOperation::Options options;
//   options.set_snap_function(snap_function);
//   S2BooleanOperation op(S2BooleanOperation::OpType::INTERSECTION,
//                         std::make_unique<S2PolygonLayer>(&polygon),
//                         options);
//   S2Error error;
//   if (!op.Build(a, b, &error)) {
//     ABSL_LOG(ERROR) << error;
//     ...
//   }
//
// If the output includes objects of different dimensions, they can be
// assembled into different layers with code like this:
//
//   vector<S2Point> points;
//   vector<unique_ptr<S2Polyline>> polylines;
//   S2Polygon polygon;
//   S2BooleanOperation op(
//       S2BooleanOperation::OpType::UNION,
//       std::make_unique<s2builderutil::S2PointVectorLayer>(&points),
//       std::make_unique<s2builderutil::S2PolylineVectorLayer>(&polylines),
//       std::make_unique<S2PolygonLayer>(&polygon));

class S2BooleanOperation {
 public:
  // The supported operation types.
  enum class OpType : uint8_t {
    UNION,                // Contained by either region.
    INTERSECTION,         // Contained by both regions.
    DIFFERENCE,           // Contained by the first region but not the second.
    SYMMETRIC_DIFFERENCE  // Contained by one region but not the other.
  };
  // Translates OpType to one of the strings above.
  static absl::string_view OpTypeToString(OpType op_type);

  // Defines whether polygons are considered to contain their vertices and/or
  // edges (see definitions above).
  enum class PolygonModel : uint8_t { OPEN, SEMI_OPEN, CLOSED };

  // Translates PolygonModel to one of the strings above.
  static absl::string_view PolygonModelToString(PolygonModel model);

  // Defines whether polylines are considered to contain their endpoints
  // (see definitions above).
  enum class PolylineModel : uint8_t { OPEN, SEMI_OPEN, CLOSED };

  // Translates PolylineModel to one of the strings above.
  static absl::string_view PolylineModelToString(PolylineModel model);

  // With Precision::EXACT, the operation is evaluated using the exact input
  // geometry.  Predicates that use this option will produce exact results;
  // for example, they can distinguish between a polyline that barely
  // intersects a polygon from one that barely misses it.  Constructive
  // operations (ones that yield new geometry, as opposed to predicates) are
  // implemented by computing the exact result and then snap rounding it
  // according to the given snap_function() (see below).  This is as close as
  // it is possible to get to the exact result while requiring that vertex
  // coordinates have type "double".
  //
  // With Precision::SNAPPED, the input regions are snapped together *before*
  // the operation is evaluated.  So for example, two polygons that overlap
  // slightly will be treated as though they share a common boundary, and
  // similarly two polygons that are slightly separated from each other will
  // be treated as though they share a common boundary.  Snapped results are
  // useful for dealing with points, since in S2 the only points that lie
  // exactly on a polyline or polygon edge are the endpoints of that edge.
  //
  // Conceptually, the difference between these two options is that with
  // Precision::SNAPPED, the inputs are snap rounded (together), whereas with
  // Precision::EXACT only the result is snap rounded.
  enum class Precision : uint8_t { EXACT, SNAPPED };

  // SourceId identifies an edge from one of the two input S2ShapeIndexes.
  // It consists of a region id (0 or 1), a shape id within that region's
  // S2ShapeIndex, and an edge id within that shape.
  class SourceId {
   public:
    SourceId();
    SourceId(int region_id, int32_t shape_id, int32_t edge_id);
    explicit SourceId(int32_t special_edge_id);
    int region_id() const { return region_id_; }
    int32_t shape_id() const { return shape_id_; }
    int32_t edge_id() const { return edge_id_; }
    // TODO(ericv): Convert to functions, define all 6 comparisons.
    bool operator==(SourceId other) const;
    bool operator<(SourceId other) const;

   private:
    // TODO(user): Use in-class initializers when C++20 is allowed in
    // opensource.
    uint32_t region_id_ : 1;
    uint32_t shape_id_ : 31;
    int32_t edge_id_;
  };

  class Options {
   public:
    Options();

    // Convenience constructor that calls set_snap_function().
    explicit Options(const S2Builder::SnapFunction& snap_function);

    // Specifies the function to be used for snap rounding.
    //
    // DEFAULT: s2builderutil::IdentitySnapFunction(S1Angle::Zero())
    //  - This does no snapping and preserves all input vertices exactly unless
    //    there are crossing edges, in which case the snap radius is increased
    //    to the maximum intersection point error (S2::kIntersectionError).
    const S2Builder::SnapFunction& snap_function() const;
    void set_snap_function(const S2Builder::SnapFunction& snap_function);

    // Defines whether polygons are considered to contain their vertices
    // and/or edges (see comments above).
    //
    // DEFAULT: PolygonModel::SEMI_OPEN
    PolygonModel polygon_model() const;
    void set_polygon_model(PolygonModel model);

    // Defines whether polylines are considered to contain their vertices (see
    // comments above).
    //
    // DEFAULT: PolylineModel::CLOSED
    PolylineModel polyline_model() const;
    void set_polyline_model(PolylineModel model);

    // Specifies whether a polyline loop is considered to have a non-empty
    // boundary.  By default this option is true, meaning that even if the
    // first and last vertices of a polyline are the same, the polyline is
    // considered to have a well-defined "start" and "end".  For example, if
    // the polyline boundary model is OPEN then the polyline loop would not
    // include the start/end vertices.  These are the best semantics for most
    // applications, such as GPS tracks or road network segments.
    //
    // If the polyline forms a loop and this option is set to false, then
    // instead the first and last vertices are considered to represent a
    // single vertex in the interior of the polyline.  In this case the
    // boundary of the polyline is empty, meaning that the first/last vertex
    // will be contained by the polyline even if the boundary model is OPEN.
    // (Note that this option also has a small effect on the CLOSED boundary
    // model, because the first/last vertices of a polyline loop are
    // considered to represent one vertex rather than two.)
    //
    // The main reason for this option is to implement the "mod 2 union"
    // boundary semantics of the OpenGIS Simple Features spec.  This can be
    // achieved by making sure that all polylines are constructed using
    // S2Builder::Graph::PolylineType::WALK (which ensures that all polylines
    // are as long as possible), and then setting this option to false.
    //
    // DEFAULT: true
    bool polyline_loops_have_boundaries() const;
    void set_polyline_loops_have_boundaries(bool value);

    // Specifies that a new vertex should be added whenever a polyline edge
    // crosses another polyline edge.  Note that this can cause the size of
    // polylines with many self-intersections to increase quadratically.
    //
    // If false, new vertices are added only when a polyline from one input
    // region cross a polyline from the other input region.  This allows
    // self-intersecting input polylines to be modified as little as possible.
    //
    // DEFAULT: false
    bool split_all_crossing_polyline_edges() const;
    void set_split_all_crossing_polyline_edges(bool value);

    // Specifies whether the operation should use the exact input geometry
    // (Precision::EXACT), or whether the two input regions should be snapped
    // together first (Precision::SNAPPED).
    //
    // DEFAULT: Precision::EXACT
    Precision precision() const;
    // TODO(b/78590369): Precision.SNAPPED is not yet implemented.
    // void set_precision(Precision precision);

    // If true, the input geometry is interpreted as representing nearby
    // geometry that has been snapped or simplified.  It then outputs a
    // conservative result based on the value of polygon_model() and
    // polyline_model().  For the most part, this only affects the handling of
    // degeneracies.
    //
    // - If the model is OPEN, the result is as open as possible.  For
    //   example, the intersection of two identical degenerate shells is empty
    //   under PolygonModel::OPEN because they could have been disjoint before
    //   snapping.  Similarly, two identical degenerate polylines have an
    //   empty intersection under PolylineModel::OPEN.
    //
    // - If the model is CLOSED, the result is as closed as possible.  In the
    //   case of the DIFFERENCE operation, this is equivalent to evaluating
    //   A - B as Closure(A) - Interior(B).  For other operations, it affects
    //   only the handling of degeneracies.  For example, the union of two
    //   identical degenerate holes is empty under PolygonModel::CLOSED
    //   (i.e., the hole disappears) because the holes could have been
    //   disjoint before snapping.
    //
    // - If the model is SEMI_OPEN, the result is as degenerate as possible.
    //   New degeneracies will not be created, but all degeneracies that
    //   coincide with the opposite region's boundary are retained unless this
    //   would cause a duplicate polygon edge to be created.  This model is
    //   is very useful for working with input data that has both positive and
    //   negative degeneracies (i.e., degenerate shells and holes).
    //
    // DEFAULT: false
    bool conservative_output() const;
    // TODO(b/315152896): Implement support for conservative output.
    // void set_conservative_output(bool conservative);

    // If specified, then each output edge will be labelled with one or more
    // SourceIds indicating which input edge(s) it corresponds to.  This
    // can be useful if your input geometry has additional data that needs to
    // be propagated from the input to the output (e.g., elevations).
    //
    // You can access the labels by using an S2Builder::Layer type that
    // supports labels, such as S2PolygonLayer.  The layer outputs a
    // "label_set_lexicon" and an "label_set_id" for each edge.  You can then
    // look up the source information for each edge like this:
    //
    // for (int32_t label : label_set_lexicon.id_set(label_set_id)) {
    //   const SourceId& src = source_id_lexicon.value(label);
    //   // region_id() specifies which S2ShapeIndex the edge is from (0 or 1).
    //   DoSomething(src.region_id(), src.shape_id(), src.edge_id());
    // }
    //
    // DEFAULT: nullptr
    ValueLexicon<SourceId>* source_id_lexicon() const;
    // TODO(b/314819022): Implement support for source_id_lexicon.
    // void set_source_id_lexicon(ValueLexicon<SourceId>* source_id_lexicon);

    // Specifies that internal memory usage should be tracked using the given
    // S2MemoryTracker.  If a memory limit is specified and more more memory
    // than this is required then an error will be returned.  Example usage:
    //
    //   S2MemoryTracker tracker;
    //   tracker.set_limit(500 << 20);  // 500 MB
    //   S2BooleanOperation::Options options;
    //   options.set_memory_tracker(&tracker);
    //   S2BooleanOperation op(..., options);
    //   ...
    //   S2Error error;
    //   if (!op.Build(..., &error)) {
    //     if (error.code() == S2Error::RESOURCE_EXHAUSTED) {
    //       ABSL_LOG(ERROR) << error;  // Memory limit exceeded
    //     }
    //   }
    //
    // CAVEATS:
    //
    //  - Memory used by the input S2ShapeIndexes and the output S2Builder
    //    layers is not counted towards the total.
    //
    //  - While memory tracking is reasonably complete and accurate, it does
    //    not account for every last byte.  It is intended only for the
    //    purpose of preventing clients from running out of memory.
    //
    // DEFAULT: nullptr (memory tracking disabled)
    S2MemoryTracker* memory_tracker() const;
    void set_memory_tracker(S2MemoryTracker* tracker);

    // Options may be assigned and copied.
    Options(const Options& options);
    Options& operator=(const Options& options);

   private:
    std::unique_ptr<S2Builder::SnapFunction> snap_function_;
    PolygonModel polygon_model_ = PolygonModel::SEMI_OPEN;
    PolylineModel polyline_model_ = PolylineModel::CLOSED;
    bool polyline_loops_have_boundaries_ = true;
    bool split_all_crossing_polyline_edges_ = false;
    Precision precision_ = Precision::EXACT;
    bool conservative_output_ = false;
    ValueLexicon<SourceId>* source_id_lexicon_ = nullptr;
    S2MemoryTracker* memory_tracker_ = nullptr;
  };

#ifndef SWIG
  // Specifies that the output boundary edges should be sent to a single
  // S2Builder layer.  This version can be used when the dimension of the
  // output geometry is known (e.g., intersecting two polygons to yield a
  // third polygon).
  S2BooleanOperation(OpType op_type,
                     std::unique_ptr<S2Builder::Layer> layer,
                     const Options& options = Options());

  // Specifies that the output boundary edges should be sent to three
  // different layers according to their dimension.  Points (represented by
  // degenerate edges) are sent to layer 0, polyline edges are sent to
  // layer 1, and polygon edges are sent to layer 2.
  //
  // The dimension of an edge is defined as the minimum dimension of the two
  // input edges that produced it.  For example, the intersection of two
  // crossing polyline edges is a considered to be a degenerate polyline
  // rather than a point, so it is sent to layer 1.  Clients can easily
  // reclassify such polylines as points if desired, but this rule makes it
  // easier for clients that want to process point, polyline, and polygon
  // inputs differently.
  //
  // The layers are always built in the order 0, 1, 2, and all arguments to
  // the Build() calls are guaranteed to be valid until the last call returns.
  // All Graph objects have the same set of vertices and the same lexicon
  // objects, in order to make it easier to write classes that process all the
  // edges in parallel.
  S2BooleanOperation(OpType op_type,
                     std::vector<std::unique_ptr<S2Builder::Layer>> layers,
                     const Options& options = Options());
#endif

  OpType op_type() const { return op_type_; }
  const Options& options() const { return options_; }

  // Executes the given operation.  Returns true on success, and otherwise
  // sets "error" appropriately.  (This class does not generate any errors
  // itself, but the S2Builder::Layer might.)
  bool Build(const S2ShapeIndex& a, const S2ShapeIndex& b,
             S2Error* error);

  // Convenience method that returns true if the result of the given operation
  // is empty.
  static bool IsEmpty(OpType op_type,
                      const S2ShapeIndex& a, const S2ShapeIndex& b,
                      const Options& options = Options());

  // Convenience method that returns true if A intersects B.
  static bool Intersects(const S2ShapeIndex& a, const S2ShapeIndex& b,
                         const Options& options = Options()) {
    return !IsEmpty(OpType::INTERSECTION, b, a, options);
  }

  // Convenience method that returns true if A contains B, i.e., if the
  // difference (B - A) is empty.
  static bool Contains(const S2ShapeIndex& a, const S2ShapeIndex& b,
                       const Options& options = Options()) {
    return IsEmpty(OpType::DIFFERENCE, b, a, options);
  }

  // Convenience method that returns true if the symmetric difference of A and
  // B is empty.  (Note that A and B may still not be identical, e.g. A may
  // contain two copies of a polyline while B contains one.)
  static bool Equals(const S2ShapeIndex& a, const S2ShapeIndex& b,
                     const Options& options = Options()) {
    return IsEmpty(OpType::SYMMETRIC_DIFFERENCE, b, a, options);
  }

 private:
  class Impl;  // The actual implementation.

  // Internal constructor to reduce code duplication.
  S2BooleanOperation(OpType op_type, const Options& options);

  // Specifies that "result_empty" should be set to indicate whether the exact
  // result of the operation is empty.  This constructor is used to efficiently
  // test boolean relationships (see IsEmpty above).
  S2BooleanOperation(OpType op_type, bool* result_empty,
                     const Options& options = Options());

  Options options_;
  OpType op_type_;

  // The input regions.
  const S2ShapeIndex* regions_[2];

  // The output consists either of zero layers, one layer, or three layers.
  std::vector<std::unique_ptr<S2Builder::Layer>> layers_;

  // The following field is set if and only if there are no output layers.
  bool* result_empty_ = nullptr;
};


//////////////////   Implementation details follow   ////////////////////


inline S2BooleanOperation::SourceId::SourceId()
    : region_id_(0), shape_id_(0), edge_id_(-1) {
}

inline S2BooleanOperation::SourceId::SourceId(int region_id, int32_t shape_id,
                                              int32_t edge_id)
    : region_id_(region_id), shape_id_(shape_id), edge_id_(edge_id) {}

inline S2BooleanOperation::SourceId::SourceId(int special_edge_id)
    : region_id_(0), shape_id_(0), edge_id_(special_edge_id) {
}

inline bool S2BooleanOperation::SourceId::operator==(SourceId other) const {
  return (region_id_ == other.region_id_ &&
          shape_id_ == other.shape_id_ &&
          edge_id_ == other.edge_id_);
}

inline bool S2BooleanOperation::SourceId::operator<(SourceId other) const {
  if (region_id_ < other.region_id_) return true;
  if (region_id_ > other.region_id_) return false;
  if (shape_id_ < other.shape_id_) return true;
  if (shape_id_ > other.shape_id_) return false;
  return edge_id_ < other.edge_id_;
}

#endif  // S2_S2BOOLEAN_OPERATION_H_
