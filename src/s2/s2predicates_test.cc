// Copyright 2016 Google Inc. All Rights Reserved.
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

#include "s2/s2predicates.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/base/casts.h"
#include "absl/flags/flag.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "absl/log/log_streamer.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

#include "s2/base/commandlineflags.h"
#include "s2/s1angle.h"
#include "s2/s1chord_angle.h"
#include "s2/s2cell.h"
#include "s2/s2edge_crossings.h"
#include "s2/s2edge_distances.h"
#include "s2/s2point.h"
#include "s2/s2pointutil.h"
#include "s2/s2predicates_internal.h"
#include "s2/s2random.h"
#include "s2/s2testing.h"

S2_DEFINE_int32(consistency_iters, 5000,
             "Number of iterations for precision consistency tests");

using absl::string_view;
using std::back_inserter;
using std::min;
using std::numeric_limits;
using std::string;
using std::vector;

using ::testing::Eq;

namespace s2pred {

TEST(epsilon_for_digits, recursion) {
  EXPECT_EQ(1.0, epsilon_for_digits(0));
  EXPECT_EQ(std::ldexp(1.0, -24), epsilon_for_digits(24));
  EXPECT_EQ(std::ldexp(1.0, -53), epsilon_for_digits(53));
  EXPECT_EQ(std::ldexp(1.0, -64), epsilon_for_digits(64));
  EXPECT_EQ(std::ldexp(1.0, -106), epsilon_for_digits(106));
  EXPECT_EQ(std::ldexp(1.0, -113), epsilon_for_digits(113));
}

TEST(rounding_epsilon, vs_numeric_limits) {
  // Check that rounding_epsilon<T>() returns the expected value for "float"
  // and "double".  We explicitly do not test "long double" since if this type
  // is implemented using double-double arithmetic then the numeric_limits
  // epsilon() value is completely unrelated to the maximum rounding error.
  EXPECT_EQ(0.5 * numeric_limits<float>::epsilon(),
            rounding_epsilon<float>());
  EXPECT_EQ(0.5 * numeric_limits<double>::epsilon(),
            rounding_epsilon<double>());
}

TEST(Sign, CollinearPoints) {
  // The following points happen to be *exactly collinear* along a line that it
  // approximate tangent to the surface of the unit sphere.  In fact, C is the
  // exact midpoint of the line segment AB.  All of these points are close
  // enough to unit length to satisfy S2::IsUnitLength().
  S2Point a(0.72571927877036835, 0.46058825605889098, 0.51106749730504852);
  S2Point b(0.7257192746638208, 0.46058826573818168, 0.51106749441312738);
  S2Point c(0.72571927671709457, 0.46058826089853633, 0.51106749585908795);
  EXPECT_EQ(c - a, b - c);
  EXPECT_NE(0, Sign(a, b, c));
  EXPECT_EQ(Sign(a, b, c), Sign(b, c, a));
  EXPECT_EQ(Sign(a, b, c), -Sign(c, b, a));

  // The points "x1" and "x2" are exactly proportional, i.e. they both lie
  // on a common line through the origin.  Both points are considered to be
  // normalized, and in fact they both satisfy (x == x.Normalize()).
  // Therefore the triangle (x1, x2, -x1) consists of three distinct points
  // that all lie on a common line through the origin.
  S2Point x1(0.99999999999999989, 1.4901161193847655e-08, 0);
  S2Point x2(1, 1.4901161193847656e-08, 0);
  EXPECT_EQ(x1, x1.Normalize());
  EXPECT_EQ(x2, x2.Normalize());
  EXPECT_NE(0, Sign(x1, x2, -x1));
  EXPECT_EQ(Sign(x1, x2, -x1), Sign(x2, -x1, x1));
  EXPECT_EQ(Sign(x1, x2, -x1), -Sign(-x1, x2, x1));

  // Here are two more points that are distinct, exactly proportional, and
  // that satisfy (x == x.Normalize()).
  S2Point x3 = S2Point(1, 1, 1).Normalize();
  S2Point x4 = 0.99999999999999989 * x3;
  EXPECT_EQ(x3, x3.Normalize());
  EXPECT_EQ(x4, x4.Normalize());
  EXPECT_NE(x3, x4);
  EXPECT_NE(0, Sign(x3, x4, -x3));

  // The following two points demonstrate that Normalize() is not idempotent,
  // i.e. y0.Normalize() != y0.Normalize().Normalize().  Both points satisfy
  // S2::IsNormalized(), though, and the two points are exactly proportional.
  S2Point y0 = S2Point(1, 1, 0);
  S2Point y1 = y0.Normalize();
  S2Point y2 = y1.Normalize();
  EXPECT_NE(y1, y2);
  EXPECT_EQ(y2, y2.Normalize());
  EXPECT_NE(0, Sign(y1, y2, -y1));
  EXPECT_EQ(Sign(y1, y2, -y1), Sign(y2, -y1, y1));
  EXPECT_EQ(Sign(y1, y2, -y1), -Sign(-y1, y2, y1));
}

TEST(Sign, StableSignUnderflow) {
  // Verify that StableSign returns zero (indicating that the result is
  // uncertain) when its error calculation underflows.
  S2Point a(1, 1.9535722048627587e-90, 7.4882501322554515e-80);
  S2Point b(1, 9.6702373087191359e-127, 3.706704857169321e-116);
  S2Point c(1, 3.8163353663361477e-142, 1.4628419538608985e-131);

  EXPECT_EQ(StableSign(a, b, c), 0);
  EXPECT_EQ(ExactSign(a, b, c, true), 1);
  EXPECT_EQ(Sign(a, b, c), 1);
}

// This test repeatedly constructs some number of points that are on or nearly
// on a given great circle.  Then it chooses one of these points as the
// "origin" and sorts the other points in CCW order around it.  Of course,
// since the origin is on the same great circle as the points being sorted,
// nearly all of these tests are degenerate.  It then does various consistency
// checks to verify that the points are indeed sorted in CCW order.
//
// It is easier to think about what this test is doing if you imagine that the
// points are in general position rather than on a great circle.
class SignTest : public testing::Test {
 protected:
  // The following method is used to sort a collection of points in CCW order
  // around a given origin.  It returns true if A comes before B in the CCW
  // ordering (starting at an arbitrary fixed direction).
  class LessCCW {
   public:
    LessCCW(const S2Point& origin, const S2Point& start)
        : origin_(origin), start_(start) {
    }
    bool operator()(const S2Point& a, const S2Point& b) const {
      // OrderedCCW() acts like "<=", so we need to invert the comparison.
      return !s2pred::OrderedCCW(start_, b, a, origin_);
    }
   private:
    const S2Point origin_;
    const S2Point start_;
  };

  // Given a set of points with no duplicates, first remove "origin" from
  // "points" (if it exists) and then sort the remaining points in CCW order
  // around "origin" putting the result in "sorted".
  static void SortCCW(absl::Span<const S2Point> points, const S2Point& origin,
                      vector<S2Point>* sorted) {
    // Make a copy of the points with "origin" removed.
    sorted->clear();
    std::remove_copy(points.begin(), points.end(), back_inserter(*sorted),
                     origin);

    // Sort the points CCW around the origin starting at (*sorted)[0].
    LessCCW less(origin, (*sorted)[0]);
    std::sort(sorted->begin(), sorted->end(), less);
  }

  // Given a set of points sorted circularly CCW around "origin", and the
  // index "start" of a point A, count the number of CCW triangles OAB over
  // all sorted points B not equal to A.  Also check that the results of the
  // CCW tests are consistent with the hypothesis that the points are sorted.
  static int CountCCW(absl::Span<const S2Point> sorted, const S2Point& origin,
                      int start) {
    int num_ccw = 0;
    int last_sign = 1;
    const int n = sorted.size();
    for (int j = 1; j < n; ++j) {
      int sign = Sign(origin, sorted[start], sorted[(start + j) % n]);
      EXPECT_NE(0, sign);
      if (sign > 0) ++num_ccw;

      // Since the points are sorted around the origin, we expect to see a
      // (possibly empty) sequence of CCW triangles followed by a (possibly
      // empty) sequence of CW triangles.
      EXPECT_FALSE(sign > 0 && last_sign < 0);
      last_sign = sign;
    }
    return num_ccw;
  }

  // Test exhaustively whether the points in "sorted" are sorted circularly
  // CCW around "origin".
  static void TestCCW(absl::Span<const S2Point> sorted, const S2Point& origin) {
    const int n = sorted.size();
    int total_num_ccw = 0;
    int last_num_ccw = CountCCW(sorted, origin, n - 1);
    for (int start = 0; start < n; ++start) {
      int num_ccw = CountCCW(sorted, origin, start);
      // Each iteration we increase the start index by 1, therefore the number
      // of CCW triangles should decrease by at most 1.
      EXPECT_GE(num_ccw, last_num_ccw - 1);
      total_num_ccw += num_ccw;
      last_num_ccw = num_ccw;
    }
    // We have tested all triangles of the form OAB.  Exactly half of these
    // should be CCW.
    EXPECT_EQ(n * (n-1) / 2, total_num_ccw);
  }

  static void AddNormalized(const S2Point& a, vector<S2Point>* points) {
    points->push_back(a.Normalize());
  }

  // Try to add two points A1 and A2 that are slightly offset from A along the
  // tangent toward B, and such that A, A1, and A2 are exactly collinear
  // (i.e. even with infinite-precision arithmetic).  If such points cannot
  // be found, add zero points.
  static void MaybeAddTangentPoints(absl::BitGenRef bitgen, const S2Point& a,
                                    const S2Point& b, vector<S2Point>* points) {
    Vector3_d dir = S2::RobustCrossProd(a, b).CrossProd(a).Normalize();
    // We just normalized this, so if it's not unit length, it's 0 or NaN.
    if (!S2::IsUnitLength(dir)) return;
    // TODO(b/339162879): Without `kMaxIters`, we could get stuck here for
    // a very long time, possibly forever.  The `a + delta != a` condition
    // requires `delta` is large, and the others require delta is small.
    // They may have a small or empty overlap.  We could search for the
    // valid range and sample from that.  For now, just don't add
    // <0.1% of test runs will reach 100k iters, so we are not losing
    // a significant amount of test coverage.
    constexpr int kMaxIters = 100'000;
    for (int i = 0; i < kMaxIters; ++i) {
      S2Point delta = 1e-15 * absl::Uniform(bitgen, 0.0, 1.0) * dir;
      if ((a + delta) != a && (a + delta) - a == a - (a - delta) &&
          S2::IsUnitLength(a + delta) && S2::IsUnitLength(a - delta)) {
        points->push_back(a + delta);
        points->push_back(a - delta);
        return;
      }
    }
  }

  // Add zero or more (but usually one) point that is likely to trigger
  // Sign() degeneracies among the given points.
  static void AddDegeneracy(absl::BitGenRef bitgen, vector<S2Point>* points) {
    S2Point a = (*points)[absl::Uniform<size_t>(bitgen, 0, points->size())];
    S2Point b = (*points)[absl::Uniform<size_t>(bitgen, 0, points->size())];
    int coord = absl::Uniform(bitgen, 0, 3);
    switch (absl::Uniform(bitgen, 0, 8)) {
      case 0:
        // Add a random point (not uniformly distributed) along the great
        // circle AB.
        AddNormalized(absl::Uniform(bitgen, -1.0, 1.0) * a +
                          absl::Uniform(bitgen, -1.0, 1.0) * b,
                      points);
        break;
      case 1:
        // Perturb one coordinate by the minimum amount possible.
        a[coord] = nextafter(a[coord], absl::Bernoulli(bitgen, 0.5) ? 2 : -2);
        AddNormalized(a, points);
        break;
      case 2:
        // Perturb one coordinate by up to 1e-15.
        a[coord] += 1e-15 * absl::Uniform(bitgen, -1.0, 1.0);
        AddNormalized(a, points);
        break;
      case 3:
        // Scale a point just enough so that it is different while still being
        // considered normalized.
        a *= absl::Bernoulli(bitgen, 0.5) ? (1 + 2e-16) : (1 - 1e-16);
        if (S2::IsUnitLength(a)) points->push_back(a);
        break;
      case 4: {
        // Add the intersection point of AB with X=0, Y=0, or Z=0.
        S2Point dir(0, 0, 0);
        dir[coord] = absl::Bernoulli(bitgen, 0.5) ? 1 : -1;
        Vector3_d norm = S2::RobustCrossProd(a, b).Normalize();
        if (norm.Norm2() > 0) {
          AddNormalized(S2::RobustCrossProd(dir, norm), points);
        }
        break;
      }
      case 5:
        // Try to add two closely spaced points along the tangent at A to the
        // great circle through AB.
        MaybeAddTangentPoints(bitgen, a, b, points);
        break;
      case 6:
        // Try to add two closely spaced points along the tangent at A to the
        // great circle through A and the X-axis.
        MaybeAddTangentPoints(bitgen, a, S2Point(1, 0, 0), points);
        break;
      case 7:
        // Add the negative of a point.
        points->push_back(-a);
        break;
    }
  }

  // Sort the points around the given origin, and then do some consistency
  // checks to verify that they are actually sorted.
  static void SortAndTest(absl::Span<const S2Point> points,
                          const S2Point& origin) {
    vector<S2Point> sorted;
    SortCCW(points, origin, &sorted);
    TestCCW(sorted, origin);
  }

  // Construct approximately "n" points near the great circle through A and B,
  // then sort them and test whether they are sorted.
  static void TestGreatCircle(absl::BitGenRef bitgen, S2Point a, S2Point b,
                              int n, int min_unique_points) {
    a = a.Normalize();
    b = b.Normalize();
    vector<S2Point> points;
    points.push_back(a);
    points.push_back(b);
    while (points.size() < n) {
      AddDegeneracy(bitgen, &points);
    }
    // Remove any (0, 0, 0) points that were accidentally created, then sort
    // the points and remove duplicates.
    points.erase(std::remove(points.begin(), points.end(), S2Point(0, 0, 0)),
                 points.end());
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    EXPECT_GE(points.size(), min_unique_points);

    SortAndTest(points, a);
    SortAndTest(points, b);
    for (const S2Point& origin : points) {
      SortAndTest(points, origin);
    }
  }
};

TEST_F(SignTest, StressTest) {
  absl::BitGen bitgen(
      S2Testing::MakeTaggedSeedSeq("STRESS_TEST", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  // The run time of this test is *cubic* in the parameter below.
  static constexpr int kNumPointsPerCircle = 17;
  // We will fail the min unique points test ~0.5% `kMinUniquePoints == 8`,
  // so use `7`, which moves the remaining flakiness to `AddTangentPoints`
  // due to b/339162879.
  static constexpr int kMinUniquePoints = 7;

  // This test is randomized, so it is beneficial to run it several times.
  for (int iter = 0; iter < 3; ++iter) {
    // The most difficult great circles are the ones in the X-Y, Y-Z, and X-Z
    // planes, for two reasons.  First, when one or more coordinates are close
    // to zero then the perturbations can be much smaller, since floating
    // point numbers are spaced much more closely together near zero.  (This
    // tests the handling of things like underflow.)  The second reason is
    // that most of the cases of SymbolicallyPerturbedSign() can only be
    // reached when one or more input point coordinates are zero.
    TestGreatCircle(bitgen, S2Point(1, 0, 0), S2Point(0, 1, 0),
                    kNumPointsPerCircle, kMinUniquePoints);
    TestGreatCircle(bitgen, S2Point(1, 0, 0), S2Point(0, 0, 1),
                    kNumPointsPerCircle, kMinUniquePoints);
    TestGreatCircle(bitgen, S2Point(0, -1, 0), S2Point(0, 0, 1),
                    kNumPointsPerCircle, kMinUniquePoints);

    // This tests a great circle where at least some points have X, Y, and Z
    // coordinates with exactly the same mantissa.  One useful property of
    // such points is that when they are scaled (e.g. multiplying by 1+eps),
    // all such points are exactly collinear with the origin.
    TestGreatCircle(bitgen, S2Point(1 << 25, 1, -8), S2Point(-4, -(1 << 20), 1),
                    kNumPointsPerCircle, kMinUniquePoints);
  }
}

class StableSignTest : public testing::Test {
 protected:
  // Estimate the probability that S2::StableSign() will not be able to compute
  // the determinant sign of a triangle A, B, C consisting of three points
  // that are as collinear as possible and spaced the given distance apart.
  double GetFailureRate(double km) {
    absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
        "STABLE_SIGN_TEST_FAILURE_RATE", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
    constexpr int kIters = 10000;
    int failure_count = 0;
    double m = tan(S2Testing::KmToAngle(km).radians());
    for (int iter = 0; iter < kIters; ++iter) {
      S2Point a, x, y;
      s2random::Frame(bitgen, a, x, y);
      S2Point b = (a - m * x).Normalize();
      S2Point c = (a + m * x).Normalize();
      int sign = s2pred::StableSign(a, b, c);
      if (sign != 0) {
        EXPECT_EQ(s2pred::ExactSign(a, b, c, true), sign);
      } else {
        ++failure_count;
      }
    }
    double rate = static_cast<double>(failure_count) / kIters;
    ABSL_LOG(INFO) << "StableSign failure rate for " << km << " km = " << rate;
    return rate;
  }
};

TEST_F(StableSignTest, FailureRate) {
  // Verify that StableSign() is able to handle most cases where the three
  // points are as collinear as possible.  (For reference, TriageSign() fails
  // virtually 100% of the time on this test.)
  //
  // Note that the failure rate *decreases* as the points get closer together,
  // and the decrease is approximately linear.  For example, the failure rate
  // is 0.4% for collinear points spaced 1km apart, but only 0.0004% for
  // collinear points spaced 1 meter apart.

  EXPECT_LT(GetFailureRate(1.0), 0.01);  //  1km spacing: <  1% (actual 0.4%)
  EXPECT_LT(GetFailureRate(10.0), 0.1);  // 10km spacing: < 10% (actual 4%)
}

// Given 3 points A, B, C that are exactly coplanar with the origin and where
// A < B < C in lexicographic order, verify that ABC is counterclockwise (if
// expected == 1) or clockwise (if expected == -1) using ExpensiveSign().
//
// This method is intended specifically for checking the cases where
// symbolic perturbations are needed to break ties.
static void CheckSymbolicSign(int expected, const S2Point& a,
                              const S2Point& b, const S2Point& c) {
  ABSL_CHECK_LT(a, b);
  ABSL_CHECK_LT(b, c);
  ABSL_CHECK_EQ(0, a.DotProd(b.CrossProd(c)));

  // Use ASSERT rather than EXPECT to suppress spurious error messages.
  ASSERT_EQ(expected, ExpensiveSign(a, b, c));
  ASSERT_EQ(expected, ExpensiveSign(b, c, a));
  ASSERT_EQ(expected, ExpensiveSign(c, a, b));
  ASSERT_EQ(-expected, ExpensiveSign(c, b, a));
  ASSERT_EQ(-expected, ExpensiveSign(b, a, c));
  ASSERT_EQ(-expected, ExpensiveSign(a, c, b));
}

TEST(Sign, SymbolicPerturbationCodeCoverage) {
  // The purpose of this test is simply to get code coverage of
  // SymbolicallyPerturbedSign().  Let M_1, M_2, ... be the sequence of
  // submatrices whose determinant sign is tested by that function.  Then the
  // i-th test below is a 3x3 matrix M (with rows A, B, C) such that:
  //
  //    det(M) = 0
  //    det(M_j) = 0 for j < i
  //    det(M_i) != 0
  //    A < B < C in lexicographic order.
  //
  // I checked that reversing the sign of any of the "return" statements in
  // SymbolicallyPerturbedSign() will cause this test to fail.

  // det(M_1) = b0*c1 - b1*c0
  CheckSymbolicSign(1,
                    S2Point(-3, -1, 0), S2Point(-2, 1, 0), S2Point(1, -2, 0));

  // det(M_2) = b2*c0 - b0*c2
  CheckSymbolicSign(1,
                    S2Point(-6, 3, 3), S2Point(-4, 2, -1), S2Point(-2, 1, 4));

  // det(M_3) = b1*c2 - b2*c1
  CheckSymbolicSign(1, S2Point(0, -1, -1), S2Point(0, 1, -2), S2Point(0, 2, 1));
  // From this point onward, B or C must be zero, or B is proportional to C.

  // det(M_4) = c0*a1 - c1*a0
  CheckSymbolicSign(1, S2Point(-1, 2, 7), S2Point(2, 1, -4), S2Point(4, 2, -8));

  // det(M_5) = c0
  CheckSymbolicSign(1,
                    S2Point(-4, -2, 7), S2Point(2, 1, -4), S2Point(4, 2, -8));

  // det(M_6) = -c1
  CheckSymbolicSign(1, S2Point(0, -5, 7), S2Point(0, -4, 8), S2Point(0, -2, 4));

  // det(M_7) = c2*a0 - c0*a2
  CheckSymbolicSign(1,
                    S2Point(-5, -2, 7), S2Point(0, 0, -2), S2Point(0, 0, -1));

  // det(M_8) = c2
  CheckSymbolicSign(1, S2Point(0, -2, 7), S2Point(0, 0, 1), S2Point(0, 0, 2));
  // From this point onward, C must be zero.

  // det(M_9) = a0*b1 - a1*b0
  CheckSymbolicSign(1, S2Point(-3, 1, 7), S2Point(-1, -4, 1), S2Point(0, 0, 0));

  // det(M_10) = -b0
  CheckSymbolicSign(1,
                    S2Point(-6, -4, 7), S2Point(-3, -2, 1), S2Point(0, 0, 0));

  // det(M_11) = b1
  CheckSymbolicSign(-1, S2Point(0, -4, 7), S2Point(0, -2, 1), S2Point(0, 0, 0));

  // det(M_12) = a0
  CheckSymbolicSign(-1,
                    S2Point(-1, -4, 5), S2Point(0, 0, -3), S2Point(0, 0, 0));

  // det(M_13) = 1
  CheckSymbolicSign(1, S2Point(0, -4, 5), S2Point(0, 0, -5), S2Point(0, 0, 0));
}

enum Precision { DOUBLE, LONG_DOUBLE, EXACT, SYMBOLIC, NUM_PRECISIONS };

static constexpr string_view kPrecisionNames[] = {"double", "long double",
                                                  "exact", "symbolic"};

// If `sizeof(long double) == sizeof(double)`, then we will never do
// calculations with `long double` and instead fall back to exact.
constexpr Precision kLongDoublePrecision =
    s2pred::kHasLongDouble ? LONG_DOUBLE : EXACT;

// A helper class that keeps track of how often each precision was used and
// generates a string for logging purposes.
class PrecisionStats {
 public:
  PrecisionStats();
  void Tally(Precision precision) { ++counts_[precision]; }
  string ToString();

 private:
  int counts_[NUM_PRECISIONS];
};

PrecisionStats::PrecisionStats() {
  for (int& count : counts_) count = 0;
}

string PrecisionStats::ToString() {
  string result;
  int total = 0;
  for (int i = 0; i < NUM_PRECISIONS; ++i) {
    absl::StrAppendFormat(&result, "%s=%6d, ", kPrecisionNames[i], counts_[i]);
    total += counts_[i];
  }
  absl::StrAppendFormat(&result, "total=%6d", total);
  return result;
}

// Chooses a random S2Point that is often near the intersection of one of the
// coordinates planes or coordinate axes with the unit sphere.  (It is possible
// to represent very small perturbations near such points.)
static S2Point ChoosePoint(absl::BitGenRef bitgen) {
  S2Point x = s2random::Point(bitgen);
  for (int i = 0; i < 3; ++i) {
    if (absl::Bernoulli(bitgen, 1.0 / 3)) {
      x[i] *= s2random::LogUniform(bitgen, 1e-50, 1.0);
    }
  }
  return x.Normalize();
}

// The following helper classes allow us to test the various distance
// calculation methods using a common test framework.
class Sin2Distances {
 public:
  template <class T>
  static int Triage(const Vector3<T>& x,
                    const Vector3<T>& a, const Vector3<T>& b) {
    return TriageCompareSin2Distances(x, a, b);
  }
};

class CosDistances {
 public:
  template <class T>
  static int Triage(const Vector3<T>& x,
                    const Vector3<T>& a, const Vector3<T>& b) {
    return TriageCompareCosDistances(x, a, b);
  }
};

// Compares distances greater than 90 degrees using sin^2(distance).
class MinusSin2Distances {
 public:
  template <class T>
  static int Triage(const Vector3<T>& x,
                    const Vector3<T>& a, const Vector3<T>& b) {
    return -TriageCompareSin2Distances(-x, a, b);
  }
};

// Verifies that CompareDistances(x, a, b) == expected_sign, and furthermore
// checks that the minimum required precision is "expected_prec" when the
// distance calculation method defined by CompareDistancesWrapper is used.
template <class CompareDistancesWrapper>
void TestCompareDistances(S2Point x, S2Point a, S2Point b,
                          int expected_sign, Precision expected_prec) {
  // Don't normalize the arguments unless necessary (to allow testing points
  // that differ only in magnitude).
  if (!S2::IsUnitLength(x)) x = x.Normalize();
  if (!S2::IsUnitLength(a)) a = a.Normalize();
  if (!S2::IsUnitLength(b)) b = b.Normalize();

  int dbl_sign = CompareDistancesWrapper::Triage(x, a, b);
  int ld_sign = CompareDistancesWrapper::Triage(ToLD(x), ToLD(a), ToLD(b));
  int exact_sign = ExactCompareDistances(ToExact(x), ToExact(a), ToExact(b));
  int actual_sign = (exact_sign != 0 ? exact_sign :
                     SymbolicCompareDistances(x, a, b));

  // Check that the signs are correct (if non-zero), and also that if dbl_sign
  // is non-zero then so is ld_sign, etc.
  EXPECT_EQ(expected_sign, actual_sign);
  if (exact_sign != 0) EXPECT_EQ(exact_sign, actual_sign);
  if (ld_sign != 0) EXPECT_EQ(exact_sign, ld_sign);
  if (dbl_sign != 0) EXPECT_EQ(ld_sign, dbl_sign);

  Precision actual_prec = (dbl_sign ? DOUBLE :
                           ld_sign ? LONG_DOUBLE :
                           exact_sign ? EXACT : SYMBOLIC);
  EXPECT_EQ(expected_prec, actual_prec);

  // Make sure that the top-level function returns the expected result.
  EXPECT_EQ(expected_sign, CompareDistances(x, a, b));

  // Check that reversing the arguments negates the result.
  EXPECT_EQ(-expected_sign, CompareDistances(x, b, a));
}

TEST(CompareDistances, Coverage) {
  // This test attempts to exercise all the code paths in all precisions.

  // Test TriageCompareSin2Distances.
  TestCompareDistances<Sin2Distances>(
      S2Point(1, 1, 1), S2Point(1, 1 - 1e-15, 1), S2Point(1, 1, 1 + 2e-15),
      -1, DOUBLE);
  TestCompareDistances<Sin2Distances>(
      S2Point(1, 1, 0), S2Point(1, 1 - 1e-15, 1e-21), S2Point(1, 1 - 1e-15, 0),
      1, DOUBLE);
  TestCompareDistances<Sin2Distances>(
      S2Point(2, 0, 0), S2Point(2, -1, 0), S2Point(2, 1, 1e-8),
      -1, kLongDoublePrecision);
  TestCompareDistances<Sin2Distances>(
      S2Point(2, 0, 0), S2Point(2, -1, 0), S2Point(2, 1, 1e-100),
      -1, EXACT);
  TestCompareDistances<Sin2Distances>(
      S2Point(1, 0, 0), S2Point(1, -1, 0), S2Point(1, 1, 0),
      1, SYMBOLIC);
  TestCompareDistances<Sin2Distances>(
      S2Point(1, 0, 0), S2Point(1, 0, 0), S2Point(1, 0, 0),
      0, SYMBOLIC);

  // Test TriageCompareCosDistances.
  TestCompareDistances<CosDistances>(
      S2Point(1, 1, 1), S2Point(1, -1, 0), S2Point(-1, 1, 3e-15),
      1, DOUBLE);
  TestCompareDistances<CosDistances>(
      S2Point(1, 0, 0), S2Point(1, 1e-30, 0), S2Point(-1, 1e-40, 0),
      -1, DOUBLE);
  TestCompareDistances<CosDistances>(
      S2Point(1, 1, 1), S2Point(1, -1, 0), S2Point(-1, 1, 3e-18),
      1, kLongDoublePrecision);
  TestCompareDistances<CosDistances>(
      S2Point(1, 1, 1), S2Point(1, -1, 0), S2Point(-1, 1, 1e-100),
      1, EXACT);
  TestCompareDistances<CosDistances>(
      S2Point(1, 1, 1), S2Point(1, -1, 0), S2Point(-1, 1, 0),
      -1, SYMBOLIC);
  TestCompareDistances<CosDistances>(
      S2Point(1, 1, 1), S2Point(1, -1, 0), S2Point(1, -1, 0),
      0, SYMBOLIC);

  // Test TriageCompareSin2Distances using distances greater than 90 degrees.
  TestCompareDistances<MinusSin2Distances>(
      S2Point(1, 1, 0), S2Point(-1, -1 + 1e-15, 0), S2Point(-1, -1, 0),
      -1, DOUBLE);
  TestCompareDistances<MinusSin2Distances>(
      S2Point(-1, -1, 0), S2Point(1, 1 - 1e-15, 0),
      S2Point(1, 1 - 1e-15, 1e-21), 1, DOUBLE);
  TestCompareDistances<MinusSin2Distances>(
      S2Point(-1, -1, 0), S2Point(2, 1, 0), S2Point(2, 1, 1e-8),
      1, kLongDoublePrecision);
  TestCompareDistances<MinusSin2Distances>(
      S2Point(-1, -1, 0), S2Point(2, 1, 0), S2Point(2, 1, 1e-30),
      1, EXACT);
  TestCompareDistances<MinusSin2Distances>(
      S2Point(-1, -1, 0), S2Point(2, 1, 0), S2Point(1, 2, 0),
      -1, SYMBOLIC);
}

// Checks that the result at one level of precision is consistent with the
// result at the next higher level of precision.  Returns the minimum
// precision that yielded a non-zero result.
template <class CompareDistancesWrapper>
Precision TestCompareDistancesConsistency(const S2Point& x, const S2Point& a,
                                          const S2Point& b) {
  int dbl_sign = CompareDistancesWrapper::Triage(x, a, b);
  int ld_sign = CompareDistancesWrapper::Triage(ToLD(x), ToLD(a), ToLD(b));
  int exact_sign = ExactCompareDistances(ToExact(x), ToExact(a), ToExact(b));
  if (dbl_sign != 0) EXPECT_EQ(ld_sign, dbl_sign);
  if (ld_sign != 0) EXPECT_EQ(exact_sign, ld_sign);
  if (exact_sign != 0) {
    EXPECT_EQ(exact_sign, CompareDistances(x, a, b));
    return (ld_sign == 0) ? EXACT : (dbl_sign == 0) ? LONG_DOUBLE : DOUBLE;
  } else {
    // Unlike the other methods, SymbolicCompareDistances has the
    // precondition that the exact sign must be zero.
    int symbolic_sign = SymbolicCompareDistances(x, a, b);
    EXPECT_EQ(symbolic_sign, CompareDistances(x, a, b));
    return SYMBOLIC;
  }
}

TEST(CompareDistances, Consistency) {
  // This test chooses random point pairs that are nearly equidistant from a
  // target point, and then checks that the answer given by a method at one
  // level of precision is consistent with the answer given at the next higher
  // level of precision.
  //
  // The way the .cc file is structured, we can only do comparisons using a
  // specific precision if we also choose the specific distance calculation
  // method.  The code below checks that the Cos, Sin2, and MinusSin2 methods
  // are consistent across their entire valid range of inputs, and also
  // simulates the logic in CompareDistance that chooses which method to use
  // in order to gather statistics about how often each precision is needed.
  // (These statistics are only useful for coverage purposes, not benchmarks,
  // since the input points are chosen to be pathological worst cases.)
  TestCompareDistancesConsistency<CosDistances>(
      S2Point(1, 0, 0), S2Point(0, -1, 0), S2Point(0, 1, 0));
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "COMPARE_DISTANCES_CONSISTENCY", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  PrecisionStats sin2_stats, cos_stats, minus_sin2_stats;
  for (int iter = 0; iter < absl::GetFlag(FLAGS_consistency_iters); ++iter) {
    S2Point x = ChoosePoint(bitgen);
    S2Point dir = ChoosePoint(bitgen);
    S1Angle r =
        S1Angle::Radians(M_PI_2 * s2random::LogUniform(bitgen, 1e-30, 1.0));
    if (absl::Bernoulli(bitgen, 0.5)) r = S1Angle::Radians(M_PI_2) - r;
    if (absl::Bernoulli(bitgen, 0.5)) r = S1Angle::Radians(M_PI_2) + r;
    S2Point a = S2::GetPointOnLine(x, dir, r);
    S2Point b = S2::GetPointOnLine(x, -dir, r);
    Precision prec = TestCompareDistancesConsistency<CosDistances>(x, a, b);
    if (r.degrees() >= 45 && r.degrees() <= 135) cos_stats.Tally(prec);
    // The Sin2 method is only valid if both distances are less than 90
    // degrees, and similarly for the MinusSin2 method.  (In the actual
    // implementation these methods are only used if both distances are less
    // than 45 degrees or greater than 135 degrees respectively.)
    if (r.radians() < M_PI_2 - 1e-14) {
      prec = TestCompareDistancesConsistency<Sin2Distances>(x, a, b);
      if (r.degrees() < 45) {
        // Don't skew the statistics by recording degenerate inputs.
        if (a == b) {
          EXPECT_EQ(SYMBOLIC, prec);
        } else {
          sin2_stats.Tally(prec);
        }
      }
    } else if (r.radians() > M_PI_2 + 1e-14) {
      prec = TestCompareDistancesConsistency<MinusSin2Distances>(x, a, b);
      if (r.degrees() > 135) minus_sin2_stats.Tally(prec);
    }
  }
  ABSL_LOG(ERROR) << "\nsin2:  " << sin2_stats.ToString()
                  << "\ncos:   " << cos_stats.ToString()
                  << "\n-sin2: " << minus_sin2_stats.ToString();
}

// Helper classes for testing the various distance calculation methods.
class Sin2Distance {
 public:
  template <class T>
  static int Triage(const Vector3<T>& x, const Vector3<T>& y, S1ChordAngle r) {
    return TriageCompareSin2Distance(x, y, absl::implicit_cast<T>(r.length2()));
  }
};

class CosDistance {
 public:
  template <class T>
  static int Triage(const Vector3<T>& x, const Vector3<T>& y, S1ChordAngle r) {
    return TriageCompareCosDistance(x, y, absl::implicit_cast<T>(r.length2()));
  }
};

// Verifies that CompareDistance(x, y, r) == expected_sign, and furthermore
// checks that the minimum required precision is "expected_prec" when the
// distance calculation method defined by CompareDistanceWrapper is used.
template <class CompareDistanceWrapper>
void TestCompareDistance(S2Point x, S2Point y, S1ChordAngle r,
                         int expected_sign, Precision expected_prec) {
  // Don't normalize the arguments unless necessary (to allow testing points
  // that differ only in magnitude).
  if (!S2::IsUnitLength(x)) x = x.Normalize();
  if (!S2::IsUnitLength(y)) y = y.Normalize();

  int dbl_sign = CompareDistanceWrapper::Triage(x, y, r);
  int ld_sign = CompareDistanceWrapper::Triage(ToLD(x), ToLD(y), r);
  int exact_sign = ExactCompareDistance(ToExact(x), ToExact(y), r.length2());

  // Check that the signs are correct (if non-zero), and also that if dbl_sign
  // is non-zero then so is ld_sign, etc.
  EXPECT_EQ(expected_sign, exact_sign);
  if (ld_sign != 0) EXPECT_EQ(exact_sign, ld_sign);
  if (dbl_sign != 0) EXPECT_EQ(ld_sign, dbl_sign);

  Precision actual_prec = (dbl_sign ? DOUBLE : ld_sign ? LONG_DOUBLE : EXACT);
  EXPECT_EQ(expected_prec, actual_prec);

  // Make sure that the top-level function returns the expected result.
  EXPECT_EQ(expected_sign, CompareDistance(x, y, r));

  // Mathematically, if d(X, Y) < r then d(-X, Y) > (Pi - r).  Unfortunately
  // there can be rounding errors when computing the supplementary distance,
  // so to ensure the two distances are exactly supplementary we need to do
  // the following.
  S1ChordAngle r_supp = S1ChordAngle::Straight() - r;
  r = S1ChordAngle::Straight() - r_supp;
  EXPECT_EQ(-CompareDistance(x, y, r), CompareDistance(-x, y, r_supp));
}

TEST(CompareDistance, Coverage) {
  // Test TriageCompareSin2Distance.
  TestCompareDistance<Sin2Distance>(
      S2Point(1, 1, 1), S2Point(1, 1 - 1e-15, 1),
      S1ChordAngle::Radians(1e-15), -1, DOUBLE);
  TestCompareDistance<Sin2Distance>(
      S2Point(1, 0, 0), S2Point(1, 1, 0),
      S1ChordAngle::Radians(M_PI_4), -1, kLongDoublePrecision);
  TestCompareDistance<Sin2Distance>(
      S2Point(1, 1e-40, 0), S2Point(1 + DBL_EPSILON, 1e-40, 0),
      S1ChordAngle::Radians(0.9 * DBL_EPSILON * 1e-40), 1, EXACT);
  TestCompareDistance<Sin2Distance>(
      S2Point(1, 1e-40, 0), S2Point(1 + DBL_EPSILON, 1e-40, 0),
      S1ChordAngle::Radians(1.1 * DBL_EPSILON * 1e-40), -1, EXACT);
  TestCompareDistance<Sin2Distance>(
      S2Point(1, 0, 0), S2Point(1 + DBL_EPSILON, 0, 0),
      S1ChordAngle::Zero(), 0, EXACT);

  // Test TriageCompareCosDistance.
  TestCompareDistance<CosDistance>(
      S2Point(1, 0, 0), S2Point(1, 1e-8, 0),
      S1ChordAngle::Radians(1e-7), -1, DOUBLE);
  TestCompareDistance<CosDistance>(
      S2Point(1, 0, 0), S2Point(-1, 1e-8, 0),
      S1ChordAngle::Radians(M_PI - 1e-7), 1, DOUBLE);
  TestCompareDistance<CosDistance>(
      S2Point(1, 1, 0), S2Point(1, -1 - 2 * DBL_EPSILON, 0),
      S1ChordAngle::Right(), 1, DOUBLE);
  TestCompareDistance<CosDistance>(
      S2Point(1, 1, 0), S2Point(1, -1 - DBL_EPSILON, 0),
      S1ChordAngle::Right(), 1, kLongDoublePrecision);
  TestCompareDistance<CosDistance>(
      S2Point(1, 1, 0), S2Point(1, -1, 1e-30),
      S1ChordAngle::Right(), 0, EXACT);
  // The angle between these two points is exactly 60 degrees.
  TestCompareDistance<CosDistance>(
      S2Point(1, 1, 0), S2Point(0, 1, 1),
      S1ChordAngle::FromLength2(1), 0, EXACT);
}

// Checks that the result at one level of precision is consistent with the
// result at the next higher level of precision.  Returns the minimum
// precision that yielded a non-zero result.
template <class CompareDistanceWrapper>
Precision TestCompareDistanceConsistency(const S2Point& x, const S2Point& y,
                                         S1ChordAngle r) {
  int dbl_sign = CompareDistanceWrapper::Triage(x, y, r);
  int ld_sign = CompareDistanceWrapper::Triage(ToLD(x), ToLD(y), r);
  int exact_sign = ExactCompareDistance(ToExact(x), ToExact(y), r.length2());
  EXPECT_EQ(exact_sign, CompareDistance(x, y, r));
  if (dbl_sign != 0) EXPECT_EQ(ld_sign, dbl_sign);
  if (ld_sign != 0) EXPECT_EQ(exact_sign, ld_sign);
  return (ld_sign == 0) ? EXACT : (dbl_sign == 0) ? LONG_DOUBLE : DOUBLE;
}

TEST(CompareDistance, Consistency) {
  // This test chooses random inputs such that the distance between points X
  // and Y is very close to the threshold distance "r".  It then checks that
  // the answer given by a method at one level of precision is consistent with
  // the answer given at the next higher level of precision.  See also the
  // comments in the CompareDistances consistency test.
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "COMPARE_DISTANCE_CONSISTENCY", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  PrecisionStats sin2_stats, cos_stats;
  for (int iter = 0; iter < absl::GetFlag(FLAGS_consistency_iters); ++iter) {
    S2Point x = ChoosePoint(bitgen);
    S2Point dir = ChoosePoint(bitgen);
    S1Angle r =
        S1Angle::Radians(M_PI_2 * s2random::LogUniform(bitgen, 1e-30, 1.0));
    if (absl::Bernoulli(bitgen, 0.5)) r = S1Angle::Radians(M_PI_2) - r;
    if (absl::Bernoulli(bitgen, 0.2)) r = S1Angle::Radians(M_PI_2) + r;
    S2Point y = S2::GetPointOnLine(x, dir, r);
    Precision prec = TestCompareDistanceConsistency<CosDistance>(
        x, y, S1ChordAngle(r));
    if (r.degrees() >= 45) cos_stats.Tally(prec);
    if (r.radians() < M_PI_2 - 1e-14) {
      prec = TestCompareDistanceConsistency<Sin2Distance>(
          x, y, S1ChordAngle(r));
      if (r.degrees() < 45) sin2_stats.Tally(prec);
    }
  }
  ABSL_LOG(ERROR) << "\nsin2:  " << sin2_stats.ToString()
                  << "\ncos:   " << cos_stats.ToString();
}

// Verifies that CompareEdgeDistance(x, a0, a1, r) == expected_sign, and
// furthermore checks that the minimum required precision is "expected_prec".
void TestCompareEdgeDistance(S2Point x, S2Point a0, S2Point a1, S1ChordAngle r,
                             int expected_sign, Precision expected_prec) {
  // Don't normalize the arguments unless necessary (to allow testing points
  // that differ only in magnitude).
  if (!S2::IsUnitLength(x)) x = x.Normalize();
  if (!S2::IsUnitLength(a0)) a0 = a0.Normalize();
  if (!S2::IsUnitLength(a1)) a1 = a1.Normalize();

  int dbl_sign = TriageCompareEdgeDistance(x, a0, a1, r.length2());
  int ld_sign = TriageCompareEdgeDistance(ToLD(x), ToLD(a0), ToLD(a1),
                                          ToLD(r.length2()));
  int exact_sign = ExactCompareEdgeDistance(x, a0, a1, r);

  // Check that the signs are correct (if non-zero), and also that if dbl_sign
  // is non-zero then so is ld_sign, etc.
  EXPECT_EQ(expected_sign, exact_sign);
  if (ld_sign != 0) EXPECT_EQ(exact_sign, ld_sign);
  if (dbl_sign != 0) EXPECT_EQ(ld_sign, dbl_sign);

  Precision actual_prec = (dbl_sign ? DOUBLE : ld_sign ? LONG_DOUBLE : EXACT);
  EXPECT_EQ(expected_prec, actual_prec);

  // Make sure that the top-level function returns the expected result.
  EXPECT_EQ(expected_sign, CompareEdgeDistance(x, a0, a1, r));
}

TEST(CompareEdgeDistance, Coverage) {
  // Test TriageCompareLineSin2Distance.
  TestCompareEdgeDistance(
      S2Point(1, 1e-10, 1e-15), S2Point(1, 0, 0), S2Point(0, 1, 0),
      S1ChordAngle::Radians(1e-15 + DBL_EPSILON), -1, DOUBLE);
  TestCompareEdgeDistance(
      S2Point(1, 1, 1e-15), S2Point(1, 0, 0), S2Point(0, 1, 0),
      S1ChordAngle::Radians(1e-15 + DBL_EPSILON), -1, kLongDoublePrecision);
  TestCompareEdgeDistance(
      S2Point(1, 1, 1e-40), S2Point(1, 0, 0), S2Point(0, 1, 0),
      S1ChordAngle::Radians(1e-40), -1, EXACT);
  TestCompareEdgeDistance(
      S2Point(1, 1, 0), S2Point(1, 0, 0), S2Point(0, 1, 0),
      S1ChordAngle::Zero(), 0, EXACT);

  // Test TriageCompareLineCos2Distance.
  TestCompareEdgeDistance(
      S2Point(1e-15, 0, 1), S2Point(1, 0, 0), S2Point(0, 1, 0),
      S1ChordAngle::Radians(M_PI_2 - 1e-15 - 3 * DBL_EPSILON),
      1, DOUBLE);
  TestCompareEdgeDistance(
      S2Point(1e-15, 0, 1), S2Point(1, 0, 0), S2Point(0, 1, 0),
      S1ChordAngle::Radians(M_PI_2 - 1e-15 - DBL_EPSILON),
      1, kLongDoublePrecision);
  TestCompareEdgeDistance(
      S2Point(1e-40, 0, 1), S2Point(1, 0, 0), S2Point(0, 1, 0),
      S1ChordAngle::Right(), -1, EXACT);
  TestCompareEdgeDistance(
      S2Point(0, 0, 1), S2Point(1, 0, 0), S2Point(0, 1, 0),
      S1ChordAngle::Right(), 0, EXACT);

  // Test cases where the closest point is an edge endpoint.
  TestCompareEdgeDistance(
      S2Point(1e-15, -1, 0), S2Point(1, 0, 0), S2Point(1, 1, 0),
      S1ChordAngle::Right(), -1, DOUBLE);
  TestCompareEdgeDistance(
      S2Point(-1, -1, 1), S2Point(1, 0, 0), S2Point(1, 1, 0),
      S1ChordAngle::Right(), 1, DOUBLE);
  TestCompareEdgeDistance(
      S2Point(1e-18, -1, 0), S2Point(1, 0, 0), S2Point(1, 1, 0),
      S1ChordAngle::Right(), -1, kLongDoublePrecision);
  TestCompareEdgeDistance(
      S2Point(1e-100, -1, 0), S2Point(1, 0, 0), S2Point(1, 1, 0),
      S1ChordAngle::Right(), -1, EXACT);
  TestCompareEdgeDistance(
      S2Point(0, -1, 0), S2Point(1, 0, 0), S2Point(1, 1, 0),
      S1ChordAngle::Right(), 0, EXACT);

  // Test cases where x == -a0 or x == -a1.
  TestCompareEdgeDistance(
      S2Point(-1, 0, 0), S2Point(1, 0, 0), S2Point(1, 1, 0),
      S1ChordAngle::Right(), 1, DOUBLE);
  TestCompareEdgeDistance(
      S2Point(-1, 0, 0), S2Point(1, 0, 0), S2Point(1e-18, 1, 0),
      S1ChordAngle::Right(), 1, kLongDoublePrecision);
  TestCompareEdgeDistance(
      S2Point(-1, 0, 0), S2Point(1, 0, 0), S2Point(1e-100, 1, 0),
      S1ChordAngle::Right(), 1, EXACT);
  TestCompareEdgeDistance(
      S2Point(0, -1, 0), S2Point(1, 0, 0), S2Point(0, 1, 0),
      S1ChordAngle::Right(), 0, EXACT);
}

// Checks that the result at one level of precision is consistent with the
// result at the next higher level of precision.  Returns the minimum
// precision that yielded a non-zero result.
Precision TestCompareEdgeDistanceConsistency(
    const S2Point& x, const S2Point& a0, const S2Point& a1, S1ChordAngle r) {
  int dbl_sign = TriageCompareEdgeDistance(x, a0, a1, r.length2());
  int ld_sign = TriageCompareEdgeDistance(ToLD(x), ToLD(a0), ToLD(a1),
                                          ToLD(r.length2()));
  int exact_sign = ExactCompareEdgeDistance(x, a0, a1, r);
  EXPECT_EQ(exact_sign, CompareEdgeDistance(x, a0, a1, r));
  if (dbl_sign != 0) EXPECT_EQ(ld_sign, dbl_sign);
  if (ld_sign != 0) EXPECT_EQ(exact_sign, ld_sign);
  return (ld_sign == 0) ? EXACT : (dbl_sign == 0) ? LONG_DOUBLE : DOUBLE;
}

TEST(CompareEdgeDistance, Consistency) {
  // This test chooses random inputs such that the distance between "x" and
  // the line (a0, a1) is very close to the threshold distance "r".  It then
  // checks that the answer given by a method at one level of precision is
  // consistent with the answer given at the next higher level of precision.
  // See also the comments in the CompareDistances consistency test.
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "COMPARE_EDGE_DISTANCE_CONSISTENCY", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  PrecisionStats stats;
  for (int iter = 0; iter < absl::GetFlag(FLAGS_consistency_iters); ++iter) {
    S2Point a0 = ChoosePoint(bitgen);
    S1Angle len =
        S1Angle::Radians(M_PI * s2random::LogUniform(bitgen, 1e-20, 1.0));
    S2Point a1 = S2::GetPointOnLine(a0, ChoosePoint(bitgen), len);
    if (absl::Bernoulli(bitgen, 0.5)) a1 = -a1;
    if (a0 == -a1) continue;  // Not allowed by API.
    S2Point n = S2::RobustCrossProd(a0, a1).Normalize();
    double f = s2random::LogUniform(bitgen, 1e-20, 1.0);
    S2Point a = ((1 - f) * a0 + f * a1).Normalize();
    S1Angle r =
        S1Angle::Radians(M_PI_2 * s2random::LogUniform(bitgen, 1e-20, 1.0));
    if (absl::Bernoulli(bitgen, 0.5)) r = S1Angle::Radians(M_PI_2) - r;
    S2Point x = S2::GetPointOnLine(a, n, r);
    if (absl::Bernoulli(bitgen, 0.2)) {
      // Replace "x" with a random point that is closest to an edge endpoint.
      do {
        x = ChoosePoint(bitgen);
      } while (CompareEdgeDirections(a0, x, a0, a1) > 0 &&
               CompareEdgeDirections(x, a1, a0, a1) > 0);
      r = min(S1Angle(x, a0), S1Angle(x, a1));
    }
    Precision prec = TestCompareEdgeDistanceConsistency(x, a0, a1,
                                                        S1ChordAngle(r));
    stats.Tally(prec);
  }
  ABSL_LOG(ERROR) << stats.ToString();
}

TEST(CompareEdgePairDistance, Coverage) {
  // Since CompareEdgePairDistance() is implemented using other predicates, we
  // only test to verify that those predicates are being used correctly.
  S2Point x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
  S2Point a(1, 1e-100, 1e-99), b(1, 1e-100, -1e-99);

  // Test cases where the edges have an interior crossing.
  EXPECT_EQ(CompareEdgePairDistance(x, y, a, b, S1ChordAngle::Zero()), 0);
  EXPECT_EQ(CompareEdgePairDistance(x, y, a, b, S1ChordAngle::Radians(1)), -1);
  EXPECT_EQ(CompareEdgePairDistance(x, y, a, b, S1ChordAngle::Radians(-1)), 1);

  // Test cases where the edges share an endpoint.
  EXPECT_EQ(CompareEdgePairDistance(x, y, x, z, S1ChordAngle::Radians(0)), 0);
  EXPECT_EQ(CompareEdgePairDistance(x, y, z, x, S1ChordAngle::Radians(0)), 0);
  EXPECT_EQ(CompareEdgePairDistance(y, x, x, z, S1ChordAngle::Radians(0)), 0);
  EXPECT_EQ(CompareEdgePairDistance(y, x, z, x, S1ChordAngle::Radians(0)), 0);

  // Test cases where one edge is degenerate.
  EXPECT_EQ(CompareEdgePairDistance(x, x, x, y, S1ChordAngle::Radians(0)), 0);
  EXPECT_EQ(CompareEdgePairDistance(x, y, x, x, S1ChordAngle::Radians(0)), 0);
  EXPECT_EQ(CompareEdgePairDistance(x, x, y, z, S1ChordAngle::Radians(1)), 1);
  EXPECT_EQ(CompareEdgePairDistance(y, z, x, x, S1ChordAngle::Radians(1)), 1);

  // Test cases where both edges are degenerate.
  EXPECT_EQ(CompareEdgePairDistance(x, x, x, x, S1ChordAngle::Radians(0)), 0);
  EXPECT_EQ(CompareEdgePairDistance(x, x, y, y, S1ChordAngle::Radians(1)), 1);

  // Test cases where the minimum distance is non-zero and is achieved at each
  // of the four edge endpoints.
  S1ChordAngle kHi = S1ChordAngle::Radians(1e-100 + 1e-115);
  S1ChordAngle kLo = S1ChordAngle::Radians(1e-100 - 1e-115);
  EXPECT_EQ(CompareEdgePairDistance(a, y, x, z, kHi), -1);
  EXPECT_EQ(CompareEdgePairDistance(a, y, x, z, kLo), 1);
  EXPECT_EQ(CompareEdgePairDistance(y, a, x, z, kHi), -1);
  EXPECT_EQ(CompareEdgePairDistance(y, a, x, z, kLo), 1);
  EXPECT_EQ(CompareEdgePairDistance(x, z, a, y, kHi), -1);
  EXPECT_EQ(CompareEdgePairDistance(x, z, a, y, kLo), 1);
  EXPECT_EQ(CompareEdgePairDistance(x, z, y, a, kHi), -1);
  EXPECT_EQ(CompareEdgePairDistance(x, z, y, a, kLo), 1);
}

// Verifies that CompareEdgeDirections(a0, a1, b0, b1) == expected_sign, and
// furthermore checks that the minimum required precision is "expected_prec".
void TestCompareEdgeDirections(S2Point a0, S2Point a1, S2Point b0, S2Point b1,
                               int expected_sign, Precision expected_prec) {
  // Don't normalize the arguments unless necessary (to allow testing points
  // that differ only in magnitude).
  if (!S2::IsUnitLength(a0)) a0 = a0.Normalize();
  if (!S2::IsUnitLength(a1)) a1 = a1.Normalize();
  if (!S2::IsUnitLength(b0)) b0 = b0.Normalize();
  if (!S2::IsUnitLength(b1)) b1 = b1.Normalize();

  int dbl_sign = TriageCompareEdgeDirections(a0, a1, b0, b1);
  int ld_sign = TriageCompareEdgeDirections(ToLD(a0), ToLD(a1),
                                            ToLD(b0), ToLD(b1));
  int exact_sign = ExactCompareEdgeDirections(ToExact(a0), ToExact(a1),
                                              ToExact(b0), ToExact(b1));

  // Check that the signs are correct (if non-zero), and also that if dbl_sign
  // is non-zero then so is ld_sign, etc.
  EXPECT_EQ(expected_sign, exact_sign);
  if (ld_sign != 0) EXPECT_EQ(exact_sign, ld_sign);
  if (dbl_sign != 0) EXPECT_EQ(ld_sign, dbl_sign);

  Precision actual_prec = (dbl_sign ? DOUBLE : ld_sign ? LONG_DOUBLE : EXACT);
  EXPECT_EQ(expected_prec, actual_prec);

  // Make sure that the top-level function returns the expected result.
  EXPECT_EQ(expected_sign, CompareEdgeDirections(a0, a1, b0, b1));

  // Check various identities involving swapping or negating arguments.
  EXPECT_EQ(expected_sign, CompareEdgeDirections(b0, b1, a0, a1));
  EXPECT_EQ(expected_sign, CompareEdgeDirections(-a0, -a1, b0, b1));
  EXPECT_EQ(expected_sign, CompareEdgeDirections(a0, a1, -b0, -b1));
  EXPECT_EQ(-expected_sign, CompareEdgeDirections(a1, a0, b0, b1));
  EXPECT_EQ(-expected_sign, CompareEdgeDirections(a0, a1, b1, b0));
  EXPECT_EQ(-expected_sign, CompareEdgeDirections(-a0, a1, b0, b1));
  EXPECT_EQ(-expected_sign, CompareEdgeDirections(a0, -a1, b0, b1));
  EXPECT_EQ(-expected_sign, CompareEdgeDirections(a0, a1, -b0, b1));
  EXPECT_EQ(-expected_sign, CompareEdgeDirections(a0, a1, b0, -b1));
}

TEST(CompareEdgeDirections, Coverage) {
  TestCompareEdgeDirections(S2Point(1, 0, 0), S2Point(1, 1, 0),
                            S2Point(1, -1, 0), S2Point(1, 0, 0),
                            1, DOUBLE);
  TestCompareEdgeDirections(S2Point(1, 0, 1.5e-15), S2Point(1, 1, 0),
                            S2Point(0, -1, 0), S2Point(0, 0, 1),
                            1, DOUBLE);
  TestCompareEdgeDirections(S2Point(1, 0, 1e-18), S2Point(1, 1, 0),
                            S2Point(0, -1, 0), S2Point(0, 0, 1),
                            1, kLongDoublePrecision);
  TestCompareEdgeDirections(S2Point(1, 0, 1e-50), S2Point(1, 1, 0),
                            S2Point(0, -1, 0), S2Point(0, 0, 1),
                            1, EXACT);
  TestCompareEdgeDirections(S2Point(1, 0, 0), S2Point(1, 1, 0),
                            S2Point(0, -1, 0), S2Point(0, 0, 1),
                            0, EXACT);
}

// Verifies that SignDotProd(a, b) == expected, and that the minimum
// required precision is "expected_prec".
void TestSignDotProd(S2Point a, S2Point b, int expected,
                     Precision expected_prec) {
  int actual = SignDotProd(a, b);
  EXPECT_THAT(actual, Eq(expected));

  // We triage in double precision and then fall back to exact for 0.
  Precision actual_prec = EXACT;
  if (TriageSignDotProd(a, b) != 0) {
    actual_prec = DOUBLE;
  } else {
    if (TriageSignDotProd(ToLD(a), ToLD(b)) != 0) {
      actual_prec = LONG_DOUBLE;
    }
  }
  EXPECT_THAT(actual_prec, Eq(expected_prec));
}

TEST(SignDotProd, Orthogonal) {
  const S2Point a(1, 0, 0);
  const S2Point b(0, 1, 0);
  TestSignDotProd(a, b, 0, EXACT);
}

TEST(SignDotProd, NearlyOrthogonalPositive) {
  Precision LD_OR_EXACT = kHasLongDouble ? LONG_DOUBLE : EXACT;
  const S2Point a(1, 0, 0);
  const S2Point b(DBL_EPSILON, 1, 0);
  TestSignDotProd(a, b, +1, LD_OR_EXACT);

  const S2Point c(1e-45, 1, 0);
  TestSignDotProd(a, c, +1, EXACT);
}

TEST(SignDotProd, NearlyOrthogonalNegative) {
  Precision LD_OR_EXACT = kHasLongDouble ? LONG_DOUBLE : EXACT;
  const S2Point a(1, 0, 0);
  const S2Point b(-DBL_EPSILON, 1, 0);
  TestSignDotProd(a, b, -1, LD_OR_EXACT);

  const S2Point c(-1e-45, 1, 0);
  TestSignDotProd(a, c, -1, EXACT);
}
// Verifies that CircleEdgeIntersectionOrdering(a, b, c, d, n, m) == expected,
// and that the minimum required precision is "expected_prec".
void TestIntersectionOrdering(S2Point a, S2Point b, S2Point c, S2Point d,
                              S2Point m, S2Point n, int expected,
                              Precision expected_prec) {
  int actual = CircleEdgeIntersectionOrdering(a, b, c, d, m, n);
  EXPECT_THAT(actual, Eq(expected));

  // We triage in double precision and then fall back to long double and exact
  // for 0.
  Precision actual_prec = EXACT;
  if (TriageIntersectionOrdering(a, b, c, d, m, n) != 0) {
    actual_prec = DOUBLE;
  } else {
    // We got zero, check for duplicate/reverse duplicate edges before falling
    // back to more precision.
    if ((a == c && b == d) || (a == d && b == c)) {
      actual_prec = DOUBLE;
    } else {
      auto la = ToLD(a);
      auto lb = ToLD(b);
      auto lc = ToLD(c);
      auto ld = ToLD(d);
      auto ln = ToLD(n);
      auto lm = ToLD(m);
      if (TriageIntersectionOrdering(la, lb, lc, ld, lm, ln) != 0) {
        actual_prec = LONG_DOUBLE;
      }
    }
  }
  EXPECT_THAT(actual_prec, Eq(expected_prec));
}

TEST(CircleEdgeIntersectionOrdering, Works) {
  Precision LD_OR_EXACT = kHasLongDouble ? LONG_DOUBLE : EXACT;

  // Two cells who's left and right edges are on the prime meridian,
  const S2Cell cell0(S2CellId::FromToken("054"));
  const S2Cell cell1(S2CellId::FromToken("1ac"));

  // And then the three neighbors above them.
  const S2Cell cella(S2CellId::FromToken("0fc"));
  const S2Cell cellb(S2CellId::FromToken("104"));
  const S2Cell cellc(S2CellId::FromToken("10c"));

  // Top, left and right edges of the cell as unnormalized vectors.
  const S2Point e3 = cell1.GetEdgeRaw(3);
  const S2Point e2 = cell1.GetEdgeRaw(2);
  const S2Point e1 = cell1.GetEdgeRaw(1);
  const S2Point c1 = cell1.GetCenter();
  const S2Point cb = cellb.GetCenter();

  // The same edge should cross at the same spot exactly.
  TestIntersectionOrdering(c1, cb, c1, cb, e2, e1, 0, DOUBLE);

  // Simple case where the crossings aren't too close, AB should cross after CD.
  TestIntersectionOrdering(  //
      c1, cellb.GetVertex(3), c1, cellb.GetVertex(2), e2, e1, +1, DOUBLE);

  // Swapping the boundary we're comparing against should negate the sign.
  TestIntersectionOrdering(  //
      c1, cellb.GetVertex(3), c1, cellb.GetVertex(2), e2, e3, -1, DOUBLE);

  // As should swapping the edge ordering.
  TestIntersectionOrdering(  //
      c1, cellb.GetVertex(2), c1, cellb.GetVertex(3), e2, e1, -1, DOUBLE);
  TestIntersectionOrdering(  //
      c1, cellb.GetVertex(2), c1, cellb.GetVertex(3), e2, e3, +1, DOUBLE);

  // Nearly the same edge but with one endpoint perturbed enough to require
  // long double precision.
  const S2Point yeps = S2Point(0, DBL_EPSILON, 0);
  TestIntersectionOrdering(c1, cb + yeps, c1, cb, e2, e1, -1, LD_OR_EXACT);
  TestIntersectionOrdering(c1, cb - yeps, c1, cb, e2, e1, +1, LD_OR_EXACT);
  TestIntersectionOrdering(c1, cb, c1, cb + yeps, e2, e1, +1, LD_OR_EXACT);
  TestIntersectionOrdering(c1, cb, c1, cb - yeps, e2, e1, -1, LD_OR_EXACT);
}

// Verifies that CircleEdgeIntersectionSign(a, b, n, x) == expected, and
// that the minimum required precision is "expected_prec".
void TestCircleEdgeIntersectionSign(S2Point a, S2Point b, S2Point n, S2Point x,
                                    int expected, Precision expected_prec) {
  int actual = CircleEdgeIntersectionSign(a, b, n, x);
  EXPECT_THAT(actual, Eq(expected));

  // We triage in double precision and then fall back to long double and exact
  // for 0.
  Precision actual_prec = EXACT;
  if (TriageCircleEdgeIntersectionSign(a, b, n, x) != 0) {
    actual_prec = DOUBLE;
  } else {
    auto la = ToLD(a);
    auto lb = ToLD(b);
    auto ln = ToLD(n);
    auto lx = ToLD(x);
    if (TriageCircleEdgeIntersectionSign(la, lb, ln, lx) != 0) {
      actual_prec = LONG_DOUBLE;
    }
  }
  EXPECT_THAT(actual_prec, Eq(expected_prec));
}

TEST(CircleEdgeIntersectionSign, Works) {
  // Two cells who's left and right edges are on the prime meridian,
  const S2Cell cell0(S2CellId::FromToken("054"));
  const S2Cell cell1(S2CellId::FromToken("1ac"));

  // And then the three neighbors above them.
  const S2Cell cella(S2CellId::FromToken("0fc"));
  const S2Cell cellb(S2CellId::FromToken("104"));
  const S2Cell cellc(S2CellId::FromToken("10c"));

  {
    // Top, left and right edges of the cell as unnormalized vectors.
    const S2Point nt = cell1.GetEdgeRaw(2);
    const S2Point nl = cell1.GetEdgeRaw(3);
    const S2Point nr = cell1.GetEdgeRaw(1);
    const S2Point v0 = cell1.GetCenter();

    TestCircleEdgeIntersectionSign(v0, cella.GetVertex(0), nt, nl, -1, DOUBLE);
    TestCircleEdgeIntersectionSign(v0, cella.GetVertex(0), nt, nr, +1, DOUBLE);

    TestCircleEdgeIntersectionSign(v0, cell1.GetVertex(3), nt, nl, 0, EXACT);
    TestCircleEdgeIntersectionSign(v0, cell1.GetVertex(3), nt, nr, +1, DOUBLE);

    TestCircleEdgeIntersectionSign(v0, cellb.GetCenter(), nt, nl, +1, DOUBLE);
    TestCircleEdgeIntersectionSign(v0, cellb.GetCenter(), nt, nr, +1, DOUBLE);

    TestCircleEdgeIntersectionSign(v0, cellc.GetVertex(1), nt, nl, +1, DOUBLE);
    TestCircleEdgeIntersectionSign(v0, cellc.GetVertex(1), nt, nr, -1, DOUBLE);
  }

  {
    // Test landing exactly on the right edge.
    const S2Point nt = cell0.GetEdgeRaw(2);
    const S2Point nl = cell0.GetEdgeRaw(3);
    const S2Point nr = cell0.GetEdgeRaw(1);
    const S2Point v0 = cell0.GetCenter();

    TestCircleEdgeIntersectionSign(v0, cell0.GetVertex(2), nt, nl, +1, DOUBLE);
    TestCircleEdgeIntersectionSign(v0, cell0.GetVertex(2), nt, nr, 0, EXACT);
  }
}

// Checks that the result at one level of precision is consistent with the
// result at the next higher level of precision.  Returns the minimum
// precision that yielded a non-zero result.
Precision TestCompareEdgeDirectionsConsistency(
    const S2Point& a0, const S2Point& a1,
    const S2Point& b0, const S2Point& b1) {
  int dbl_sign = TriageCompareEdgeDirections(a0, a1, b0, b1);
  int ld_sign = TriageCompareEdgeDirections(ToLD(a0), ToLD(a1),
                                            ToLD(b0), ToLD(b1));
  int exact_sign = ExactCompareEdgeDirections(ToExact(a0), ToExact(a1),
                                              ToExact(b0), ToExact(b1));
  EXPECT_EQ(exact_sign, CompareEdgeDirections(a0, a1, b0, b1));
  if (dbl_sign != 0) EXPECT_EQ(ld_sign, dbl_sign);
  if (ld_sign != 0) EXPECT_EQ(exact_sign, ld_sign);
  return (ld_sign == 0) ? EXACT : (dbl_sign == 0) ? LONG_DOUBLE : DOUBLE;
}

TEST(CompareEdgeDirections, Consistency) {
  // This test chooses random pairs of edges that are nearly perpendicular,
  // then checks that the answer given by a method at one level of precision
  // is consistent with the answer given at the next higher level of
  // precision.  See also the comments in the CompareDistances test.
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "COMPARE_EDGE_DIRECTIONS_CONSISTENCY", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  PrecisionStats stats;
  for (int iter = 0; iter < absl::GetFlag(FLAGS_consistency_iters); ++iter) {
    S2Point a0 = ChoosePoint(bitgen);
    S1Angle a_len =
        S1Angle::Radians(M_PI * s2random::LogUniform(bitgen, 1e-20, 1.0));
    S2Point a1 = S2::GetPointOnLine(a0, ChoosePoint(bitgen), a_len);
    S2Point a_norm = S2::RobustCrossProd(a0, a1).Normalize();
    S2Point b0 = ChoosePoint(bitgen);
    S1Angle b_len =
        S1Angle::Radians(M_PI * s2random::LogUniform(bitgen, 1e-20, 1.0));
    S2Point b1 = S2::GetPointOnLine(b0, a_norm, b_len);
    if (a0 == -a1 || b0 == -b1) continue;  // Not allowed by API.
    Precision prec = TestCompareEdgeDirectionsConsistency(a0, a1, b0, b1);
    // Don't skew the statistics by recording degenerate inputs.
    if (a0 == a1 || b0 == b1) {
      EXPECT_EQ(EXACT, prec);
    } else {
      stats.Tally(prec);
    }
  }
  ABSL_LOG(ERROR) << stats.ToString();
}

// Verifies that EdgeCircumcenterSign(x0, x1, a, b, c) == expected_sign, and
// furthermore checks that the minimum required precision is "expected_prec".
void TestEdgeCircumcenterSign(
    S2Point x0, S2Point x1, S2Point a, S2Point b, S2Point c,
    int expected_sign, Precision expected_prec) {
  // Don't normalize the arguments unless necessary (to allow testing points
  // that differ only in magnitude).
  if (!S2::IsUnitLength(x0)) x0 = x0.Normalize();
  if (!S2::IsUnitLength(x1)) x1 = x1.Normalize();
  if (!S2::IsUnitLength(a)) a = a.Normalize();
  if (!S2::IsUnitLength(b)) b = b.Normalize();
  if (!S2::IsUnitLength(c)) c = c.Normalize();

  int abc_sign = Sign(a, b, c);
  int dbl_sign = TriageEdgeCircumcenterSign(x0, x1, a, b, c, abc_sign);
  int ld_sign = TriageEdgeCircumcenterSign(
      ToLD(x0), ToLD(x1), ToLD(a), ToLD(b), ToLD(c), abc_sign);
  int exact_sign = ExactEdgeCircumcenterSign(
      ToExact(x0), ToExact(x1), ToExact(a), ToExact(b), ToExact(c), abc_sign);
  int actual_sign = (exact_sign != 0 ? exact_sign :
                     SymbolicEdgeCircumcenterSign(x0, x1, a, b, c));

  // Check that the signs are correct (if non-zero), and also that if dbl_sign
  // is non-zero then so is ld_sign, etc.
  EXPECT_EQ(expected_sign, actual_sign);
  if (exact_sign != 0) EXPECT_EQ(exact_sign, actual_sign);
  if (ld_sign != 0) EXPECT_EQ(exact_sign, ld_sign);
  if (dbl_sign != 0) EXPECT_EQ(ld_sign, dbl_sign);

  Precision actual_prec = (dbl_sign ? DOUBLE :
                           ld_sign ? LONG_DOUBLE :
                           exact_sign ? EXACT : SYMBOLIC);
  EXPECT_EQ(expected_prec, actual_prec);

  // Make sure that the top-level function returns the expected result.
  EXPECT_EQ(expected_sign, EdgeCircumcenterSign(x0, x1, a, b, c));

  // Check various identities involving swapping or negating arguments.
  EXPECT_EQ(expected_sign, EdgeCircumcenterSign(x0, x1, a, c, b));
  EXPECT_EQ(expected_sign, EdgeCircumcenterSign(x0, x1, b, a, c));
  EXPECT_EQ(expected_sign, EdgeCircumcenterSign(x0, x1, b, c, a));
  EXPECT_EQ(expected_sign, EdgeCircumcenterSign(x0, x1, c, a, b));
  EXPECT_EQ(expected_sign, EdgeCircumcenterSign(x0, x1, c, b, a));
  EXPECT_EQ(-expected_sign, EdgeCircumcenterSign(x1, x0, a, b, c));
  EXPECT_EQ(expected_sign, EdgeCircumcenterSign(-x0, -x1, a, b, c));
  if (actual_sign == exact_sign) {
    // Negating the input points may not preserve the result when symbolic
    // perturbations are used, since -X is not an exact multiple of X.
    EXPECT_EQ(-expected_sign, EdgeCircumcenterSign(x0, x1, -a, -b, -c));
  }
}

TEST(EdgeCircumcenterSign, Coverage) {
  TestEdgeCircumcenterSign(
      S2Point(1, 0, 0), S2Point(1, 1, 0),
      S2Point(0, 0, 1), S2Point(1, 0, 1), S2Point(0, 1, 1),
      1, DOUBLE);
  TestEdgeCircumcenterSign(
      S2Point(1, 0, 0), S2Point(1, 1, 0),
      S2Point(0, 0, -1), S2Point(1, 0, -1), S2Point(0, 1, -1),
      -1, DOUBLE);
  TestEdgeCircumcenterSign(
      S2Point(1, -1, 0), S2Point(1, 1, 0),
      S2Point(1, -1e-5, 1), S2Point(1, 1e-5, -1), S2Point(1, 1 - 1e-5, 1e-5),
      -1, DOUBLE);
  TestEdgeCircumcenterSign(
      S2Point(1, -1, 0), S2Point(1, 1, 0),
      S2Point(1, -1e-5, 1), S2Point(1, 1e-5, -1), S2Point(1, 1 - 1e-9, 1e-5),
      -1, kLongDoublePrecision);
  TestEdgeCircumcenterSign(
      S2Point(1, -1, 0), S2Point(1, 1, 0),
      S2Point(1, -1e-5, 1), S2Point(1, 1e-5, -1), S2Point(1, 1 - 1e-15, 1e-5),
      -1, EXACT);
  TestEdgeCircumcenterSign(
      S2Point(1, -1, 0), S2Point(1, 1, 0),
      S2Point(1, -1e-5, 1), S2Point(1, 1e-5, -1), S2Point(1, 1, 1e-5),
      1, SYMBOLIC);

  // This test falls back to the second symbolic perturbation:
  TestEdgeCircumcenterSign(
      S2Point(1, -1, 0), S2Point(1, 1, 0),
      S2Point(0, -1, 0), S2Point(0, 0, -1), S2Point(0, 0, 1),
      -1, SYMBOLIC);

  // This test falls back to the third symbolic perturbation:
  TestEdgeCircumcenterSign(
      S2Point(0, -1, 1), S2Point(0, 1, 1),
      S2Point(0, 1, 0), S2Point(0, -1, 0), S2Point(1, 0, 0),
      -1, SYMBOLIC);
}

// Checks that the result at one level of precision is consistent with the
// result at the next higher level of precision.  Returns the minimum
// precision that yielded a non-zero result.
Precision TestEdgeCircumcenterSignConsistency(
    const S2Point& x0, const S2Point& x1,
    const S2Point& a, const S2Point& b, const S2Point& c) {
  int abc_sign = Sign(a, b, c);
  int dbl_sign = TriageEdgeCircumcenterSign(x0, x1, a, b, c, abc_sign);
  int ld_sign = TriageEdgeCircumcenterSign(
      ToLD(x0), ToLD(x1), ToLD(a), ToLD(b), ToLD(c), abc_sign);
  int exact_sign = ExactEdgeCircumcenterSign(
      ToExact(x0), ToExact(x1), ToExact(a), ToExact(b), ToExact(c), abc_sign);
  if (dbl_sign != 0) EXPECT_EQ(ld_sign, dbl_sign);
  if (ld_sign != 0) EXPECT_EQ(exact_sign, ld_sign);
  if (exact_sign != 0) {
    EXPECT_EQ(exact_sign, EdgeCircumcenterSign(x0, x1, a, b, c));
    return (ld_sign == 0) ? EXACT : (dbl_sign == 0) ? LONG_DOUBLE : DOUBLE;
  } else {
    // Unlike the other methods, SymbolicEdgeCircumcenterSign has the
    // precondition that the exact sign must be zero.
    int symbolic_sign = SymbolicEdgeCircumcenterSign(x0, x1, a, b, c);
    EXPECT_EQ(symbolic_sign, EdgeCircumcenterSign(x0, x1, a, b, c));
    return SYMBOLIC;
  }
}

TEST(EdgeCircumcenterSign, Consistency) {
  // This test chooses random a random edge X, then chooses a random point Z
  // on the great circle through X, and finally choose three points A, B, C
  // that are nearly equidistant from X.  It then checks that the answer given
  // by a method at one level of precision is consistent with the answer given
  // at the next higher level of precision.
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "EDGE_CIRCUMCENTER_SIGN_CONSISTENCY", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  PrecisionStats stats;
  for (int iter = 0; iter < absl::GetFlag(FLAGS_consistency_iters); ++iter) {
    S2Point x0 = ChoosePoint(bitgen);
    S2Point x1 = ChoosePoint(bitgen);
    if (x0 == -x1) continue;  // Not allowed by API.
    double c0 = (absl::Bernoulli(bitgen, 0.5) ? -1 : 1) *
                s2random::LogUniform(bitgen, 1e-20, 1.0);
    double c1 = (absl::Bernoulli(bitgen, 0.5) ? -1 : 1) *
                s2random::LogUniform(bitgen, 1e-20, 1.0);
    S2Point z = (c0 * x0 + c1 * x1).Normalize();
    S1Angle r =
        S1Angle::Radians(M_PI * s2random::LogUniform(bitgen, 1e-30, 1.0));
    S2Point a = S2::GetPointOnLine(z, ChoosePoint(bitgen), r);
    S2Point b = S2::GetPointOnLine(z, ChoosePoint(bitgen), r);
    S2Point c = S2::GetPointOnLine(z, ChoosePoint(bitgen), r);
    Precision prec = TestEdgeCircumcenterSignConsistency(x0, x1, a, b, c);
    // Don't skew the statistics by recording degenerate inputs.
    if (x0 == x1) {
      // This precision would be SYMBOLIC if we handled this degeneracy.
      EXPECT_EQ(EXACT, prec);
    } else if (a == b || b == c || c == a) {
      EXPECT_EQ(SYMBOLIC, prec);
    } else {
      stats.Tally(prec);
    }
  }
  ABSL_LOG(ERROR) << stats.ToString();
}

// Verifies that VoronoiSiteExclusion(a, b, x0, x1, r) == expected_result, and
// furthermore checks that the minimum required precision is "expected_prec".
void TestVoronoiSiteExclusion(
    S2Point a, S2Point b, S2Point x0, S2Point x1, S1ChordAngle r,
    Excluded expected_result, Precision expected_prec) {
  constexpr Excluded UNCERTAIN = Excluded::UNCERTAIN;

  // Don't normalize the arguments unless necessary (to allow testing points
  // that differ only in magnitude).
  if (!S2::IsUnitLength(a)) a = a.Normalize();
  if (!S2::IsUnitLength(b)) b = b.Normalize();
  if (!S2::IsUnitLength(x0)) x0 = x0.Normalize();
  if (!S2::IsUnitLength(x1)) x1 = x1.Normalize();

  // The internal methods (Triage, Exact, etc) require that site A is closer
  // to X0 and site B is closer to X1.  GetVoronoiSiteExclusion has special
  // code to handle the case where this is not true.  We need to duplicate
  // that code here.  Essentially, since the API requires site A to be closer
  // than site B to X0, then if site A is also closer to X1 then site B must
  // be excluded.
  if (s2pred::CompareDistances(x1, a, b) < 0) {
    EXPECT_EQ(expected_result, Excluded::SECOND);
    // We don't know what precision was used by CompareDistances(), but we
    // arbitrarily require the test to specify it as DOUBLE.
    EXPECT_EQ(expected_prec, DOUBLE);
  } else {
    Excluded dbl_result = TriageVoronoiSiteExclusion(a, b, x0, x1, r.length2());
    Excluded ld_result = TriageVoronoiSiteExclusion(
        ToLD(a), ToLD(b), ToLD(x0), ToLD(x1), ToLD(r.length2()));
    Excluded exact_result = ExactVoronoiSiteExclusion(
        ToExact(a), ToExact(b), ToExact(x0), ToExact(x1), r.length2());

    // Check that the results are correct (if not UNCERTAIN), and also that if
    // dbl_result is not UNCERTAIN then so is ld_result, etc.
    EXPECT_EQ(expected_result, exact_result);
    if (ld_result != UNCERTAIN) EXPECT_EQ(exact_result, ld_result);
    if (dbl_result != UNCERTAIN) EXPECT_EQ(ld_result, dbl_result);

    Precision actual_prec = (dbl_result != UNCERTAIN ? DOUBLE :
                             ld_result != UNCERTAIN ? LONG_DOUBLE : EXACT);
    EXPECT_EQ(expected_prec, actual_prec);
  }
  // Make sure that the top-level function returns the expected result.
  EXPECT_EQ(expected_result, GetVoronoiSiteExclusion(a, b, x0, x1, r));

  // If site B is closer to X1, then the same site should be excluded (if any)
  // when we swap the sites and the edge direction.
  Excluded swapped_result =
      expected_result == Excluded::FIRST ? Excluded::SECOND :
      expected_result == Excluded::SECOND ? Excluded::FIRST : expected_result;
  if (s2pred::CompareDistances(x1, b, a) < 0) {
    EXPECT_EQ(swapped_result, GetVoronoiSiteExclusion(b, a, x1, x0, r));
  }
}

TEST(VoronoiSiteExclusion, Coverage) {
  // Both sites are closest to edge endpoint X0.
  TestVoronoiSiteExclusion(
      S2Point(1, -1e-5, 0), S2Point(1, -2e-5, 0),
      S2Point(1, 0, 0), S2Point(1, 1, 0), S1ChordAngle::Radians(1e-3),
      Excluded::SECOND, DOUBLE);

  // Both sites are closest to edge endpoint X1.
  TestVoronoiSiteExclusion(
      S2Point(1, 1, 1e-30), S2Point(1, 1, -1e-20),
      S2Point(1, 0, 0), S2Point(1, 1, 0), S1ChordAngle::Radians(1e-10),
      Excluded::SECOND, DOUBLE);

  // Test cases where neither site is excluded.
  TestVoronoiSiteExclusion(
      S2Point(1, -1e-10, 1e-5), S2Point(1, 1e-10, -1e-5),
      S2Point(1, -1, 0), S2Point(1, 1, 0), S1ChordAngle::Radians(1e-4),
      Excluded::NEITHER, DOUBLE);
  TestVoronoiSiteExclusion(
      S2Point(1, -1e-10, 1e-5), S2Point(1, 1e-10, -1e-5),
      S2Point(1, -1, 0), S2Point(1, 1, 0), S1ChordAngle::Radians(1e-5),
      Excluded::NEITHER, kLongDoublePrecision);
  TestVoronoiSiteExclusion(
      S2Point(1, -1e-17, 1e-5), S2Point(1, 1e-17, -1e-5),
      S2Point(1, -1, 0), S2Point(1, 1, 0), S1ChordAngle::Radians(1e-4),
      Excluded::NEITHER, kLongDoublePrecision);
  TestVoronoiSiteExclusion(
      S2Point(1, -1e-20, 1e-5), S2Point(1, 1e-20, -1e-5),
      S2Point(1, -1, 0), S2Point(1, 1, 0), S1ChordAngle::Radians(1e-5),
      Excluded::NEITHER, EXACT);

  // Test cases where the first site is excluded.  (Tests where the second
  // site is excluded are constructed by TestVoronoiSiteExclusion.)
  TestVoronoiSiteExclusion(
      S2Point(1, -1e-6, 1.0049999999e-5), S2Point(1, 0, -1e-5),
      S2Point(1, -1, 0), S2Point(1, 1, 0), S1ChordAngle::Radians(1.005e-5),
      Excluded::FIRST, DOUBLE);
  TestVoronoiSiteExclusion(
      S2Point(1, -1.00105e-6, 1.0049999999e-5), S2Point(1, 0, -1e-5),
      S2Point(1, -1, 0), S2Point(1, 1, 0), S1ChordAngle::Radians(1.005e-5),
      Excluded::FIRST, kLongDoublePrecision);
  TestVoronoiSiteExclusion(
      S2Point(1, -1e-6, 1.005e-5), S2Point(1, 0, -1e-5),
      S2Point(1, -1, 0), S2Point(1, 1, 0), S1ChordAngle::Radians(1.005e-5),
      Excluded::FIRST, kLongDoublePrecision);
  TestVoronoiSiteExclusion(
      S2Point(1, -1e-31, 1.005e-30), S2Point(1, 0, -1e-30),
      S2Point(1, -1, 0), S2Point(1, 1, 0), S1ChordAngle::Radians(1.005e-30),
      Excluded::FIRST, EXACT);
  TestVoronoiSiteExclusion(
      S2Point(1, -1e-31, 1.005e-30), S2Point(1, 0, -1e-30),
      S2Point(1, -1, 0), S2Point(1, 1, 0), S1ChordAngle::Radians(1.005e-30),
      Excluded::FIRST, EXACT);

  // Test cases for the (d < 0) portion of the algorithm (see .cc file).  In
  // all of these cases A is closer to X0, B is closer to X1, and AB goes in
  // the opposite direction as edge X when projected onto it (since this is
  // what d < 0 means).

  // 1. Cases that require Pi/2 < d(X0,X1) + r < Pi.  Only one site is kept.
  //
  //    - A and B project to the interior of X.
  TestVoronoiSiteExclusion(
      S2Point(1, -1e-5, 1e-4), S2Point(1, -1.00000001e-5, 0),
      S2Point(-1, -1, 0), S2Point(1, 0, 0), S1ChordAngle::Radians(1),
      Excluded::FIRST, DOUBLE);
  //    - A and B project to opposite sides of X1.
  TestVoronoiSiteExclusion(
      S2Point(1, 1e-10, 0.1), S2Point(1, -1e-10, 1e-8),
      S2Point(-1, -1, 0), S2Point(1, 0, 0), S1ChordAngle::Radians(1),
      Excluded::FIRST, DOUBLE);
  //    - A and B both project to points past X1, and B is closer to the great
  //      circle through edge X.
  TestVoronoiSiteExclusion(
      S2Point(1, 2e-10, 0.1), S2Point(1, 1e-10, 0),
      S2Point(-1, -1, 0), S2Point(1, 0, 0), S1ChordAngle::Radians(1),
      Excluded::FIRST, DOUBLE);
  //    - Like the test above, but A is closer to the great circle through X.
  TestVoronoiSiteExclusion(
      S2Point(1, 1.1, 0), S2Point(1, 1.01, 0.01),
      S2Point(-1, -1, 0), S2Point(1, 0, 0), S1ChordAngle::Radians(1),
      Excluded::FIRST, DOUBLE);

  // 2. Cases that require d(X0,X1) + r > Pi and where only one site is kept.
  //
  //    - B is closer to edge X (in fact it's right on the edge), but when A
  //      and B are projected onto the great circle through X they are more
  //      than 90 degrees apart.  This case requires that the sin(d) < 0 case
  //      in the algorithm is handled *before* the cos(d) < 0 case.
  TestVoronoiSiteExclusion(
      S2Point(1, 1.1, 0), S2Point(1, -1, 0),
      S2Point(-1, 0, 0), S2Point(1, -1e-10, 0), S1ChordAngle::Degrees(70),
      Excluded::FIRST, DOUBLE);

  // 3. Cases that require d(X0,X1) + r > Pi and where both sites are kept.
  //
  //    - A projects to a point past X0, B projects to a point past X1,
  //      neither site should be excluded, and A is closer to the great circle
  //      through edge X.
  TestVoronoiSiteExclusion(
      S2Point(-1, 0.1, 0.001), S2Point(1, 1.1, 0),
      S2Point(-1, -1, 0), S2Point(1, 0, 0), S1ChordAngle::Radians(1),
      Excluded::NEITHER, DOUBLE);
  //    - Like the above, but B is closer to the great circle through edge X.
  TestVoronoiSiteExclusion(
      S2Point(-1, 0.1, 0), S2Point(1, 1.1, 0.001),
      S2Point(-1, -1, 0), S2Point(1, 0, 0), S1ChordAngle::Radians(1),
      Excluded::NEITHER, DOUBLE);


  // These two sites are exactly 60 degrees away from the point (1, 1, 0),
  // which is the midpoint of edge X.  This case requires symbolic
  // perturbations to resolve correctly.  Site A is closer to every point in
  // its coverage interval except for (1, 1, 0), but site B is considered
  // closer to that point symbolically.
  TestVoronoiSiteExclusion(
      S2Point(0, 1, 1), S2Point(1, 0, 1),
      S2Point(0, 1, 1), S2Point(1, 0, -1), S1ChordAngle::FromLength2(1),
      Excluded::NEITHER, EXACT);

  // This test is similar except that site A is considered closer to the
  // equidistant point (-1, 1, 0), and therefore site B is excluded.
  TestVoronoiSiteExclusion(
      S2Point(0, 1, 1), S2Point(-1, 0, 1),
      S2Point(0, 1, 1), S2Point(-1, 0, -1), S1ChordAngle::FromLength2(1),
      Excluded::SECOND, EXACT);
}

// Checks that the result at one level of precision is consistent with the
// result at the next higher level of precision.  Returns the minimum
// precision that yielded a non-zero result.
Precision TestVoronoiSiteExclusionConsistency(
    const S2Point& a, const S2Point& b, const S2Point& x0, const S2Point& x1,
    S1ChordAngle r) {
  constexpr Excluded UNCERTAIN = Excluded::UNCERTAIN;

  // The internal methods require this (see TestVoronoiSiteExclusion).
  if (s2pred::CompareDistances(x1, a, b) < 0) return DOUBLE;

  Excluded dbl_result = TriageVoronoiSiteExclusion(a, b, x0, x1, r.length2());
  Excluded ld_result = TriageVoronoiSiteExclusion(
      ToLD(a), ToLD(b), ToLD(x0), ToLD(x1), ToLD(r.length2()));
  Excluded exact_result = ExactVoronoiSiteExclusion(
      ToExact(a), ToExact(b), ToExact(x0), ToExact(x1), r.length2());
  EXPECT_EQ(exact_result, GetVoronoiSiteExclusion(a, b, x0, x1, r));

  EXPECT_NE(UNCERTAIN, exact_result);
  if (ld_result == UNCERTAIN) {
    EXPECT_EQ(UNCERTAIN, dbl_result);
    return EXACT;
  }
  EXPECT_EQ(exact_result, ld_result);
  if (dbl_result == UNCERTAIN) {
    return LONG_DOUBLE;
  }
  EXPECT_EQ(exact_result, dbl_result);
  return DOUBLE;
}

TEST(VoronoiSiteExclusion, Consistency) {
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "VORONOI_SITE_EXCLUSION", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  // This test chooses random a random edge X, a random point P on that edge,
  // and a random threshold distance "r".  It then choose two sites A and B
  // whose distance to P is almost exactly "r".  This ensures that the
  // coverage intervals for A and B will (almost) share a common endpoint.  It
  // then checks that the answer given by a method at one level of precision
  // is consistent with the answer given at higher levels of precision.
  PrecisionStats stats;
  for (int iter = 0; iter < absl::GetFlag(FLAGS_consistency_iters); ++iter) {
    S2Point x0 = ChoosePoint(bitgen);
    S2Point x1 = ChoosePoint(bitgen);
    if (x0 == -x1) continue;  // Not allowed by API.
    double f = s2random::LogUniform(bitgen, 1e-20, 1.0);
    S2Point p = ((1 - f) * x0 + f * x1).Normalize();
    S1Angle r1 =
        S1Angle::Radians(M_PI_2 * s2random::LogUniform(bitgen, 1e-20, 1.0));
    S2Point a = S2::GetPointOnLine(p, ChoosePoint(bitgen), r1);
    S2Point b = S2::GetPointOnLine(p, ChoosePoint(bitgen), r1);
    // Check that the other API requirements are met.
    S1ChordAngle r(r1);
    if (s2pred::CompareEdgeDistance(a, x0, x1, r) > 0) continue;
    if (s2pred::CompareEdgeDistance(b, x0, x1, r) > 0) continue;
    if (s2pred::CompareDistances(x0, a, b) > 0) std::swap(a, b);
    if (a == b) continue;

    Precision prec = TestVoronoiSiteExclusionConsistency(a, b, x0, x1, r);
    // Don't skew the statistics by recording degenerate inputs.
    if (x0 == x1) {
      EXPECT_EQ(DOUBLE, prec);
    } else {
      stats.Tally(prec);
    }
  }
  ABSL_LOG(ERROR) << stats.ToString();
}

}  // namespace s2pred
