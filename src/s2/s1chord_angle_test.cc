// Copyright Google Inc. All Rights Reserved.
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

#include "s2/s1chord_angle.h"

#include <cfloat>
#include <cmath>
#include <limits>

#include <gtest/gtest.h>
#include "absl/log/log_streamer.h"
#include "absl/random/random.h"
#include "s2/s1angle.h"
#include "s2/s2edge_distances.h"
#include "s2/s2point.h"
#include "s2/s2predicates.h"
#include "s2/s2random.h"
#include "s2/s2testing.h"
#include "s2/util/math/matrix3x3.h"

using std::numeric_limits;

TEST(S1ChordAngle, ConstexprFunctionsWork) {
  // Test constexpr constructors.
  static constexpr auto kTestZero = S1ChordAngle::Zero();
  static constexpr auto kTestRight = S1ChordAngle::Right();
  static constexpr auto kTestStraight = S1ChordAngle::Straight();
  static constexpr auto kTestInfinity = S1ChordAngle::Infinity();
  static constexpr auto kTestNegative = S1ChordAngle::Negative();
  static constexpr auto kTestFastUpperBound =
      S1ChordAngle::FastUpperBoundFrom(S1Angle::Radians(1));
  static constexpr auto kTestFromLength2 = S1ChordAngle::FromLength2(2);

  EXPECT_EQ(kTestRight, S1ChordAngle::Right());
  EXPECT_EQ(kTestStraight, S1ChordAngle::Straight());
  EXPECT_EQ(kTestFromLength2.length2(), 2);

  constexpr bool kIsZero = kTestZero.is_zero();
  EXPECT_TRUE(kIsZero);

  constexpr bool kIsNegative = kTestNegative.is_negative();
  EXPECT_TRUE(kIsNegative);

  constexpr bool kIsInfinity = kTestInfinity.is_infinity();
  EXPECT_TRUE(kIsInfinity);

  constexpr bool kIsSpecial = kTestNegative.is_special();
  EXPECT_TRUE(kIsSpecial);

  constexpr bool kIsValid = kTestFastUpperBound.is_valid();
  EXPECT_TRUE(kIsValid);
}

TEST(S1ChordAngle, DefaultConstructor) {
  // Check that the default constructor returns an angle of 0.
  S1ChordAngle a;
  EXPECT_EQ(S1ChordAngle::Zero(), a);
}

TEST(S1ChordAngle, TwoPointConstructor) {
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "TWO_POINT_CONSTRUCTOR", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  for (int iter = 0; iter < 100; ++iter) {
    S2Point x, y, z;
    s2random::Frame(bitgen, x, y, z);
    EXPECT_EQ(S1Angle::Zero(), S1Angle(S1ChordAngle(z, z)));
    EXPECT_NEAR(M_PI, S1ChordAngle(-z, z).radians(), 1e-7);
    EXPECT_DOUBLE_EQ(M_PI_2, S1ChordAngle(x, z).radians());
    S2Point w = (y + z).Normalize();
    EXPECT_DOUBLE_EQ(M_PI_4, S1ChordAngle(w, z).radians());
  }
}

TEST(S1ChordAngle, FromLength2) {
  EXPECT_EQ(0, S1ChordAngle::FromLength2(0).degrees());
  EXPECT_DOUBLE_EQ(60, S1ChordAngle::FromLength2(1).degrees());
  EXPECT_DOUBLE_EQ(90, S1ChordAngle::FromLength2(2).degrees());
  EXPECT_EQ(180, S1ChordAngle::FromLength2(4).degrees());
  EXPECT_EQ(180, S1ChordAngle::FromLength2(5).degrees());
}

TEST(S1ChordAngle, Zero) {
  EXPECT_EQ(S1Angle::Zero(), S1Angle(S1ChordAngle::Zero()));
}

TEST(S1ChordAngle, Right) {
  EXPECT_DOUBLE_EQ(90, S1ChordAngle::Right().degrees());
}

TEST(S1ChordAngle, Straight) {
  EXPECT_EQ(S1Angle::Degrees(180), S1Angle(S1ChordAngle::Straight()));
}

TEST(S1ChordAngle, Infinity) {
  EXPECT_LT(S1ChordAngle::Straight(), S1ChordAngle::Infinity());
  EXPECT_EQ(S1ChordAngle::Infinity(), S1ChordAngle::Infinity());
  EXPECT_EQ(S1Angle::Infinity(), S1Angle(S1ChordAngle::Infinity()));
}

TEST(S1ChordAngle, Negative) {
  EXPECT_LT(S1ChordAngle::Negative(), S1ChordAngle::Zero());
  EXPECT_EQ(S1ChordAngle::Negative(), S1ChordAngle::Negative());
  EXPECT_LT(S1ChordAngle::Negative().ToAngle(), S1Angle::Zero());
}

TEST(S1ChordAngle, Predicates) {
  EXPECT_TRUE(S1ChordAngle::Zero().is_zero());
  EXPECT_FALSE(S1ChordAngle::Zero().is_negative());
  EXPECT_FALSE(S1ChordAngle::Zero().is_special());
  EXPECT_FALSE(S1ChordAngle::Straight().is_special());
  EXPECT_TRUE(S1ChordAngle::Negative().is_negative());
  EXPECT_TRUE(S1ChordAngle::Negative().is_special());
  EXPECT_TRUE(S1ChordAngle::Infinity().is_infinity());
  EXPECT_TRUE(S1ChordAngle::Infinity().is_special());
}

TEST(S1ChordAngle, ToFromS1Angle) {
  EXPECT_EQ(0, S1ChordAngle(S1Angle::Zero()).radians());
  EXPECT_EQ(4, S1ChordAngle(S1Angle::Radians(M_PI)).length2());
  EXPECT_EQ(M_PI, S1ChordAngle(S1Angle::Radians(M_PI)).radians());
  EXPECT_EQ(S1Angle::Infinity(), S1Angle(S1ChordAngle(S1Angle::Infinity())));
  EXPECT_LT(S1ChordAngle(S1Angle::Radians(-1)).radians(), 0);
  EXPECT_DOUBLE_EQ(1.0,
                   S1ChordAngle(S1Angle::Radians(1.0)).radians());
}

TEST(S1ChordAngle, Successor) {
  EXPECT_EQ(S1ChordAngle::Zero(), S1ChordAngle::Negative().Successor());
  EXPECT_EQ(S1ChordAngle::Infinity(), S1ChordAngle::Straight().Successor());
  EXPECT_EQ(S1ChordAngle::Infinity(), S1ChordAngle::Infinity().Successor());
  S1ChordAngle x = S1ChordAngle::Negative();
  for (int i = 0; i < 10; ++i) {
    EXPECT_LT(x, x.Successor());
    x = x.Successor();
  }
}

TEST(S1ChordAngle, Predecessor) {
  EXPECT_EQ(S1ChordAngle::Straight(), S1ChordAngle::Infinity().Predecessor());
  EXPECT_EQ(S1ChordAngle::Negative(), S1ChordAngle::Zero().Predecessor());
  EXPECT_EQ(S1ChordAngle::Negative(), S1ChordAngle::Negative().Predecessor());
  S1ChordAngle x = S1ChordAngle::Infinity();
  for (int i = 0; i < 10; ++i) {
    EXPECT_GT(x, x.Predecessor());
    x = x.Predecessor();
  }
}

TEST(S1ChordAngle, Arithmetic) {
  S1ChordAngle zero = S1ChordAngle::Zero();
  S1ChordAngle degree30 = S1ChordAngle::Degrees(30);
  S1ChordAngle degree60 = S1ChordAngle::Degrees(60);
  S1ChordAngle degree90 = S1ChordAngle::Degrees(90);
  S1ChordAngle degree120 = S1ChordAngle::Degrees(120);
  S1ChordAngle degree180 = S1ChordAngle::Straight();
  EXPECT_EQ(0, (zero + zero).degrees());
  EXPECT_EQ(0, (zero - zero).degrees());
  EXPECT_EQ(0, (degree60 - degree60).degrees());
  EXPECT_EQ(0, (degree180 - degree180).degrees());
  EXPECT_EQ(0, (zero - degree60).degrees());
  EXPECT_EQ(0, (degree30 - degree90).degrees());
  // `EXPECT_DOUBLE_EQ` compares with 4 ULP tolerance.
  EXPECT_DOUBLE_EQ(60, (degree60 + zero).degrees());
  EXPECT_DOUBLE_EQ(60, (degree60 - zero).degrees());
  EXPECT_DOUBLE_EQ(60, (zero + degree60).degrees());
  EXPECT_DOUBLE_EQ(90, (degree30 + degree60).degrees());
  EXPECT_DOUBLE_EQ(90, (degree60 + degree30).degrees());
  EXPECT_DOUBLE_EQ(60, (degree90 - degree30).degrees());
#ifndef _WIN32
  EXPECT_DOUBLE_EQ(30, (degree90 - degree60).degrees());
#else
  // TODO(b/387870507): Figure out if this is a bug or the tolerance is
  // wrong.
  EXPECT_DOUBLE_EQ(30.000000000000018, (degree90 - degree60).degrees());
#endif  // defined(_WIN32)
  EXPECT_EQ(180, (degree180 + zero).degrees());
  EXPECT_EQ(180, (degree180 - zero).degrees());
  EXPECT_EQ(180, (degree90 + degree90).degrees());
  EXPECT_EQ(180, (degree120 + degree90).degrees());
  EXPECT_EQ(180, (degree120 + degree120).degrees());
  EXPECT_EQ(180, (degree30 + degree180).degrees());
  EXPECT_EQ(180, (degree180 + degree180).degrees());
}

TEST(S1ChordAngle, ArithmeticPrecision) {
  // Verifies that S1ChordAngle is capable of adding and subtracting angles
  // extremely accurately up to Pi/2 radians.  (Accuracy continues to be good
  // well beyond this value but degrades as angles approach Pi.)
  S1ChordAngle kEps = S1ChordAngle::Radians(1e-15);
  S1ChordAngle k90 = S1ChordAngle::Right();
  S1ChordAngle k90MinusEps = k90 - kEps;
  S1ChordAngle k90PlusEps = k90 + kEps;
  double kMaxError = 2 * DBL_EPSILON;
  EXPECT_NEAR(k90MinusEps.radians(), M_PI_2 - kEps.radians(), kMaxError);
  EXPECT_NEAR(k90PlusEps.radians(), M_PI_2 + kEps.radians(), kMaxError);
  EXPECT_NEAR((k90 - k90MinusEps).radians(), kEps.radians(), kMaxError);
  EXPECT_NEAR((k90PlusEps - k90).radians(), kEps.radians(), kMaxError);
  EXPECT_NEAR((k90MinusEps + kEps).radians(), M_PI_2, kMaxError);
}

TEST(S1ChordAngle, Trigonometry) {
  static constexpr int kIters = 20;
  for (int iter = 0; iter <= kIters; ++iter) {
    double radians = M_PI * iter / kIters;
    S1ChordAngle angle(S1Angle::Radians(radians));
    EXPECT_NEAR(sin(radians), sin(angle), 1e-15) << radians;
    EXPECT_NEAR(cos(radians), cos(angle), 1e-15) << radians;
    // Since the tan(x) is unbounded near Pi/4, we map the result back to an
    // angle before comparing.  (The assertion is that the result is equal to
    // the tangent of a nearby angle.)
#ifndef _WIN32
    EXPECT_NEAR(atan(tan(radians)), atan(tan(angle)), 1e-15) << radians;
#else
    // TODO(b/387870507): On Windows, we get -pi/2 (instead of pi/2) for an
    // input of pi/2.  Figure out why this is happening.
    if (iter == kIters / 2) {
      EXPECT_NEAR(-M_PI_2, atan(tan(angle)), 1e-15) << radians;
    } else {
      EXPECT_NEAR(atan(tan(radians)), atan(tan(angle)), 1e-15) << radians;
    }
#endif  // defined(_WIN32)
  }

  // Unlike S1Angle, S1ChordAngle can represent 90 and 180 degrees exactly.
  S1ChordAngle angle90 = S1ChordAngle::FromLength2(2);
  S1ChordAngle angle180 = S1ChordAngle::FromLength2(4);
  EXPECT_EQ(1, sin(angle90));
  EXPECT_EQ(0, cos(angle90));
  EXPECT_EQ(numeric_limits<double>::infinity(), tan(angle90));
  EXPECT_EQ(0, sin(angle180));
  EXPECT_EQ(-1, cos(angle180));
  EXPECT_EQ(0, tan(angle180));
}

TEST(S1ChordAngle, PlusError) {
  EXPECT_EQ(S1ChordAngle::Negative(), S1ChordAngle::Negative().PlusError(5));
  EXPECT_EQ(S1ChordAngle::Infinity(), S1ChordAngle::Infinity().PlusError(-5));
  EXPECT_EQ(S1ChordAngle::Straight(), S1ChordAngle::Straight().PlusError(5));
  EXPECT_EQ(S1ChordAngle::Zero(), S1ChordAngle::Zero().PlusError(-5));
  EXPECT_EQ(S1ChordAngle::FromLength2(1.25),
            S1ChordAngle::FromLength2(1).PlusError(0.25));
  EXPECT_EQ(S1ChordAngle::FromLength2(0.75),
            S1ChordAngle::FromLength2(1).PlusError(-0.25));
}

TEST(S1ChordAngle, GetS2PointConstructorMaxError) {
  // Check that the error bound returned by GetS2PointConstructorMaxError() is
  // large enough.
  absl::BitGen bitgen(S2Testing::MakeTaggedSeedSeq(
      "GET_S2_POINT_CONSTRUCTOR_MAX_ERROR", absl::LogInfoStreamer(__FILE__, __LINE__).stream()));
  for (int iter = 0; iter < 100000; ++iter) {
    S2Point x = s2random::Point(bitgen);
    S2Point y = s2random::Point(bitgen);
    if (absl::Bernoulli(bitgen, 0.1)) {
      // Occasionally test a point pair that is nearly identical or antipodal.
      S1Angle r = S1Angle::Radians(1e-15 * absl::Uniform(bitgen, 0.0, 1.0));
      y = S2::GetPointOnLine(x, y, r);
      if (absl::Bernoulli(bitgen, 0.5)) y = -y;
    }
    S1ChordAngle dist = S1ChordAngle(x, y);
    double error = dist.GetS2PointConstructorMaxError();
    EXPECT_LE(s2pred::CompareDistance(x, y, dist.PlusError(error)), 0)
        << "angle=" << S1Angle(dist) << ", iter=" << iter;
    EXPECT_GE(s2pred::CompareDistance(x, y, dist.PlusError(-error)), 0)
        << "angle=" << S1Angle(dist) << ", iter=" << iter;
  }
}
