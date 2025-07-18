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

#include "s2/s2crossing_edge_query.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "absl/log/absl_check.h"
#include "absl/log/log_streamer.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"

#include "s2/base/casts.h"
#include "s2/mutable_s2shape_index.h"
#include "s2/r2.h"
#include "s2/s1angle.h"
#include "s2/s2cap.h"
#include "s2/s2cell.h"
#include "s2/s2cell_id.h"
#include "s2/s2coords.h"
#include "s2/s2edge_clipping.h"
#include "s2/s2edge_crossings.h"
#include "s2/s2edge_distances.h"
#include "s2/s2edge_vector_shape.h"
#include "s2/s2metrics.h"
#include "s2/s2padded_cell.h"
#include "s2/s2point.h"
#include "s2/s2polyline.h"
#include "s2/s2random.h"
#include "s2/s2shape.h"
#include "s2/s2shape_index.h"
#include "s2/s2shapeutil_shape_edge.h"
#include "s2/s2shapeutil_shape_edge_id.h"
#include "s2/s2testing.h"
#include "s2/s2text_format.h"

using absl::StrCat;
using s2shapeutil::ShapeEdge;
using s2shapeutil::ShapeEdgeId;
using s2textformat::MakePointOrDie;
using s2textformat::MakePolylineOrDie;
using std::is_sorted;
using std::make_unique;
using std::pair;
using std::string;
using std::vector;

namespace {

using TestEdge = pair<S2Point, S2Point>;
using CrossingType = s2shapeutil::CrossingType;

S2Point PerturbAtDistance(absl::BitGenRef bitgen, S1Angle distance,
                          const S2Point& a0, const S2Point& b0) {
  S2Point x = S2::GetPointOnLine(a0, b0, distance);
  if (absl::Bernoulli(bitgen, 0.5)) {
    for (int i = 0; i < 3; ++i) {
      x[i] = nextafter(x[i], absl::Bernoulli(bitgen, 0.5) ? 1 : -1);
    }
    x = x.Normalize();
  }
  return x;
}

// Generate sub-edges of some given edge (a0,b0).  The length of the sub-edges
// is distributed exponentially over a large range, and the endpoints may be
// slightly perturbed to one side of (a0,b0) or the other.
void GetPerturbedSubEdges(absl::BitGenRef bitgen, S2Point a0, S2Point b0,
                          int count, vector<TestEdge>* edges) {
  edges->clear();
  a0 = a0.Normalize();
  b0 = b0.Normalize();
  S1Angle length0(a0, b0);
  for (int i = 0; i < count; ++i) {
    S1Angle length = length0 * s2random::LogUniform(bitgen, 1e-15, 1.0);
    S1Angle offset = (length0 - length) * absl::Uniform(bitgen, 0.0, 1.0);
    edges->push_back(
        std::make_pair(PerturbAtDistance(bitgen, offset, a0, b0),
                       PerturbAtDistance(bitgen, offset + length, a0, b0)));
  }
}

// Generate edges whose center is randomly chosen from the given S2Cap, and
// whose length is randomly chosen up to "max_length".
void GetCapEdges(absl::BitGenRef bitgen, const S2Cap& center_cap,
                 S1Angle max_length, int count, vector<TestEdge>* edges) {
  edges->clear();
  for (int i = 0; i < count; ++i) {
    S2Point center = s2random::SamplePoint(bitgen, center_cap);
    S2Cap edge_cap(center, 0.5 * max_length);
    S2Point p1 = s2random::SamplePoint(bitgen, edge_cap);
    // Compute p1 reflected through "center", and normalize for good measure.
    S2Point p2 = (2 * p1.DotProd(center) * center - p1).Normalize();
    edges->push_back(std::make_pair(p1, p2));
  }
}

// Project ShapeEdges to ShapeEdgeIds.  Useful because
// ShapeEdge does not have operator==, but ShapeEdgeId does.
static vector<ShapeEdgeId> GetShapeEdgeIds(
    absl::Span<const ShapeEdge> shape_edges) {
  vector<ShapeEdgeId> shape_edge_ids;
  for (const auto& shape_edge : shape_edges) {
    shape_edge_ids.push_back(shape_edge.id());
  }
  return shape_edge_ids;
}

void TestAllCrossings(absl::Span<const TestEdge> edges) {
  auto shape = new S2EdgeVectorShape;  // raw pointer since "shape" used below
  for (const TestEdge& edge : edges) {
    shape->Add(edge.first, edge.second);
  }
  // Force more subdivision than usual to make the test more challenging.
  MutableS2ShapeIndex::Options options;
  options.set_max_edges_per_cell(1);
  MutableS2ShapeIndex index(options);
  const int shape_id = index.Add(absl::WrapUnique(shape));
  EXPECT_EQ(0, shape_id);
  // To check that candidates are being filtered reasonably, we count the
  // total number of candidates that the total number of edge pairs that
  // either intersect or are very close to intersecting.
  int num_candidates = 0, num_nearby_pairs = 0;
  int i = 0;
  for (const TestEdge& edge : edges) {
    SCOPED_TRACE(StrCat("Iteration ", i++));
    const S2Point& a = edge.first;
    const S2Point& b = edge.second;
    S2CrossingEdgeQuery query(&index);
    const vector<ShapeEdgeId> candidates =
        query.GetCandidates(a, b, shape_id, *shape);

    // Verify that the second version of GetCandidates returns the same result.
    const vector<ShapeEdgeId> edge_candidates = query.GetCandidates(a, b);
    EXPECT_EQ(candidates, edge_candidates);
    EXPECT_TRUE(!candidates.empty());

    // Now check the actual candidates.
    EXPECT_TRUE(is_sorted(candidates.begin(), candidates.end()));
    EXPECT_EQ(candidates.back().shape_id, 0);  // Implies all shape_ids are 0.
    EXPECT_GE(candidates.front().edge_id, 0);
    EXPECT_LT(candidates.back().edge_id, shape->num_edges());
    num_candidates += candidates.size();
    string missing_candidates;
    vector<ShapeEdgeId> expected_crossings, expected_interior_crossings;
    for (int i = 0; i < shape->num_edges(); ++i) {
      auto edge = shape->edge(i);
      const S2Point& c = edge.v0;
      const S2Point& d = edge.v1;
      int sign = S2::CrossingSign(a, b, c, d);
      if (sign >= 0) {
        expected_crossings.push_back({0, i});
        if (sign > 0) {
          expected_interior_crossings.push_back({0, i});
        }
        ++num_nearby_pairs;
        if (!std::binary_search(candidates.begin(), candidates.end(),
                                ShapeEdgeId{0, i})) {
          absl::StrAppend(&missing_candidates, " ", i);
        }
      } else {
        const double kMaxDist = S2::kMaxDiag.GetValue(S2::kMaxCellLevel);
        if (S2::GetDistance(a, c, d).radians() < kMaxDist ||
            S2::GetDistance(b, c, d).radians() < kMaxDist ||
            S2::GetDistance(c, a, b).radians() < kMaxDist ||
            S2::GetDistance(d, a, b).radians() < kMaxDist) {
          ++num_nearby_pairs;
        }
      }
    }
    EXPECT_TRUE(missing_candidates.empty()) << missing_candidates;

    // Test that GetCrossings() returns only the actual crossing edges.
    const vector<ShapeEdge> actual_crossings =
        query.GetCrossingEdges(a, b, shape_id, *shape, CrossingType::ALL);
    EXPECT_EQ(expected_crossings, GetShapeEdgeIds(actual_crossings));

    // Verify that the second version of GetCrossings returns the same result.
    const vector<ShapeEdge> actual_edge_crossings =
        query.GetCrossingEdges(a, b, CrossingType::ALL);
    EXPECT_EQ(expected_crossings, GetShapeEdgeIds(actual_edge_crossings));

    // Verify that CrossingType::INTERIOR returns only the interior crossings.
    const vector<ShapeEdge> actual_interior_crossings =
        query.GetCrossingEdges(a, b, shape_id, *shape, CrossingType::INTERIOR);
    EXPECT_EQ(expected_interior_crossings,
              GetShapeEdgeIds(actual_interior_crossings));
  }
  // There is nothing magical about this particular ratio; this check exists
  // to catch changes that dramatically increase the number of candidates.
  EXPECT_LE(num_candidates, 3 * num_nearby_pairs);
}

// Test edges that lie in the plane of one of the S2 cube edges.  Such edges
// may lie on the boundary between two cube faces, or pass through a cube
// vertex, or follow a 45 diagonal across a cube face toward its center.
//
// This test is sufficient to demonstrate that padding the cell boundaries is
// necessary for correctness.  (It fails if MutableS2ShapeIndex::kCellPadding
// is set to zero.)
TEST(GetCrossingCandidates, PerturbedCubeEdges) {
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "PERTURBED_CUBE_EDGES", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  vector<TestEdge> edges;
  for (int iter = 0; iter < 10; ++iter) {
    int face = absl::Uniform(bitgen, 0, 6);
    double scale = s2random::LogUniform(bitgen, 1e-15, 1.0);
    R2Point uv(2 * absl::Uniform(bitgen, 0, 2) - 1,
               2 * absl::Uniform(bitgen, 0, 2) - 1);  // vertex
    S2Point a0 = S2::FaceUVtoXYZ(face, scale * uv);
    S2Point b0 = a0 - 2 * S2::GetNorm(face);
    // TODO(ericv): This test is currently slow because *every* crossing test
    // needs to invoke s2pred::ExpensiveSign().
    GetPerturbedSubEdges(bitgen, a0, b0, 30, &edges);
    TestAllCrossings(edges);
  }
}

// Test edges that lie in the plane of one of the S2 cube face axes.  These
// edges are special because one coordinate is zero, and they lie on the
// boundaries between the immediate child cells of the cube face.
TEST(GetCrossingCandidates, PerturbedCubeFaceAxes) {
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "PERTURBED_CUBE_FACE_AXES", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  vector<TestEdge> edges;
  for (int iter = 0; iter < 5; ++iter) {
    int face = absl::Uniform(bitgen, 0, 6);
    double scale = s2random::LogUniform(bitgen, 1e-15, 1.0);
    S2Point axis = S2::GetUVWAxis(face, absl::Uniform(bitgen, 0, 2));
    S2Point a0 = scale * axis + S2::GetNorm(face);
    S2Point b0 = scale * axis - S2::GetNorm(face);
    GetPerturbedSubEdges(bitgen, a0, b0, 30, &edges);
    TestAllCrossings(edges);
  }
}

TEST(GetCrossingCandidates, CapEdgesNearCubeVertex) {
  // Test a random collection of edges near the S2 cube vertex where the
  // Hilbert curve starts and ends.
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "CAP_EDGES_NEAR_CUBE_VERTEX", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  vector<TestEdge> edges;
  GetCapEdges(bitgen,
              S2Cap(S2Point(-1, -1, 1).Normalize(), S1Angle::Radians(1e-3)),
              S1Angle::Radians(1e-4), 1000, &edges);
  TestAllCrossings(edges);
}

TEST(GetCrossingCandidates, DegenerateEdgeOnCellVertexIsItsOwnCandidate) {
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "DEGENERATE_EDGE_ON_CELL_VERTEX_IS_ITS_OWN_CANDIDATE",
      absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  for (int i = 0; i < 100; ++i) {
    vector<TestEdge> edges;
    S2Cell cell(s2random::CellId(bitgen));
    edges.push_back(std::make_pair(cell.GetVertex(0), cell.GetVertex(0)));
    TestAllCrossings(edges);
  }
}

TEST(GetCrossingCandidates, CollinearEdgesOnCellBoundaries) {
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "COLLINEAR_EDGES_ON_CELL_BOUNDARIES", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  constexpr int kNumEdgeIntervals = 8;  // 9*8/2 = 36 edges
  for (int level = 0; level <= S2CellId::kMaxLevel; ++level) {
    S2Cell cell(s2random::CellId(bitgen, level));
    int i = absl::Uniform(bitgen, 0, 4);
    S2Point p1 = cell.GetVertexRaw(i);
    S2Point p2 = cell.GetVertexRaw(i + 1);
    S2Point delta = (p2 - p1) / kNumEdgeIntervals;
    vector<TestEdge> edges;
    for (int i = 0; i <= kNumEdgeIntervals; ++i) {
      for (int j = 0; j < i; ++j) {
        edges.push_back(std::make_pair((p1 + i * delta).Normalize(),
                                       (p1 + j * delta).Normalize()));
      }
    }
    TestAllCrossings(edges);
  }
}

// This is the example from the header file, with a few extras.
void TestPolylineCrossings(const S2ShapeIndex& index,
                           const S2Point& a0, const S2Point& a1) {
  S2CrossingEdgeQuery query(&index);
  const vector<ShapeEdge> edges =
      query.GetCrossingEdges(a0, a1, CrossingType::ALL);
  if (edges.empty()) return;
  for (const auto& edge : edges) {
    ABSL_CHECK_GE(S2::CrossingSign(a0, a1, edge.v0(), edge.v1()), 0);
  }
  // Also test that no edges are missing.
  for (int i = 0; i < index.num_shape_ids(); ++i) {
    const auto* shape = down_cast<const S2Polyline::Shape*>(index.shape(i));
    const S2Polyline* polyline = shape->polyline();
    for (int e = 0; e < polyline->num_vertices() - 1; ++e) {
      if (S2::CrossingSign(a0, a1, polyline->vertex(e),
                                   polyline->vertex(e + 1)) >= 0) {
        EXPECT_EQ(1, std::count_if(edges.begin(), edges.end(),
                                   [e, i](const ShapeEdge& edge) {
                                     return edge.id() == ShapeEdgeId{i, e};
                                   }));
      }
    }
  }
}

TEST(GetCrossings, PolylineCrossings) {
  MutableS2ShapeIndex index;
  // Three zig-zag lines near the equator.
  index.Add(make_unique<S2Polyline::OwningShape>(
      MakePolylineOrDie("0:0, 2:1, 0:2, 2:3, 0:4, 2:5, 0:6")));
  index.Add(make_unique<S2Polyline::OwningShape>(
      MakePolylineOrDie("1:0, 3:1, 1:2, 3:3, 1:4, 3:5, 1:6")));
  index.Add(make_unique<S2Polyline::OwningShape>(
      MakePolylineOrDie("2:0, 4:1, 2:2, 4:3, 2:4, 4:5, 2:6")));
  TestPolylineCrossings(index, MakePointOrDie("1:0"), MakePointOrDie("1:4"));
  TestPolylineCrossings(index, MakePointOrDie("5:5"), MakePointOrDie("6:6"));
}

TEST(GetCrossings, ShapeIdsAreCorrect) {
  // This tests that when some index cells contain only one shape, the
  // intersecting edges are returned with the correct shape id.
  MutableS2ShapeIndex index;
  index.Add(make_unique<S2Polyline::OwningShape>(
      make_unique<S2Polyline>(S2Testing::MakeRegularPoints(
          MakePointOrDie("0:0"), S1Angle::Degrees(5), 100))));
  index.Add(make_unique<S2Polyline::OwningShape>(
      make_unique<S2Polyline>(S2Testing::MakeRegularPoints(
          MakePointOrDie("0:20"), S1Angle::Degrees(5), 100))));
  TestPolylineCrossings(index, MakePointOrDie("1:-10"), MakePointOrDie("1:30"));
}

// Verifies that when VisitCells() is called with a specified root cell and a
// query edge that barely intersects that cell, that at least one cell is
// visited.  (At one point this was not always true, because when the query edge
// is clipped to the index cell boundary without using any padding then the
// result is sometimes empty, i.e., the query edge appears not to intersect the
// specified root cell.  The code now uses an appropriate amount of padding,
// i.e. S2::kFaceClipErrorUVCoord.)
TEST(VisitCells, QueryEdgeOnFaceBoundary) {
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "QUERY_EDGE_ON_FACE_BOUNDARY", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  constexpr int kIters = 100;
  for (int iter = 0; iter < kIters; ++iter) {
    SCOPED_TRACE(StrCat("Iteration ", iter));

    // Choose an edge AB such that B is nearly on the edge between two S2 cube
    // faces, and such that the result of clipping AB to the face that nominally
    // contains B (according to S2::GetFace) is empty when no padding is used.
    int a_face, b_face;
    S2Point a, b;
    R2Point a_uv, b_uv;
    do {
      a_face = absl::Uniform(bitgen, 0, 6);
      a = S2::FaceUVtoXYZ(a_face, absl::Uniform<double>(bitgen, -1, 1),
                          absl::Uniform<double>(bitgen, -1, 1))
              .Normalize();
      b_face = S2::GetUVWFace(a_face, 0, 1);  // Towards positive u-axis
      b = S2::FaceUVtoXYZ(b_face,
                          1 - absl::Uniform(bitgen, 0, 2) * 0.5 * DBL_EPSILON,
                          absl::Uniform(bitgen, -1.0, 1.0))
              .Normalize();
    } while (S2::GetFace(b) != b_face ||
             S2::ClipToFace(a, b, b_face, &a_uv, &b_uv));

    // Verify that the clipping result is non-empty when a padding of
    // S2::kFaceClipErrorUVCoord is used instead.
    EXPECT_TRUE(S2::ClipToPaddedFace(a, b, b_face, S2::kFaceClipErrorUVCoord,
                                     &a_uv, &b_uv));

    // Create an S2ShapeIndex containing a single edge BC, where C is on the
    // same S2 cube face as B (which is different than the face containing A).
    S2Point c = S2::FaceUVtoXYZ(b_face, absl::Uniform(bitgen, -1.0, 1.0),
                                absl::Uniform(bitgen, -1.0, 1.0))
                    .Normalize();
    MutableS2ShapeIndex index;
    index.Add(make_unique<S2Polyline::OwningShape>(
        make_unique<S2Polyline>(vector<S2Point>{b, c})));

    // Check that the intersection between AB and BC is detected when the face
    // containing BC is specified as a root cell.  (Note that VisitCells()
    // returns false only if the CellVisitor returns false, and otherwise
    // returns true.)
    S2CrossingEdgeQuery query(&index);
    S2PaddedCell root(S2CellId::FromFace(b_face), 0);
    EXPECT_FALSE(query.VisitCells(a, b, root, [](const S2ShapeIndexCell&) {
        return false;
      }));
  }
}

TEST(S2CrossingEdgeQuery, Index) {
  // Just give this some test coverage so it's not marked as dead code.
  MutableS2ShapeIndex index;
  S2CrossingEdgeQuery query(&index);
  EXPECT_EQ(&query.index(), &index);
}

}  // namespace
