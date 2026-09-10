#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>

#include "GnssSkyView.h"
#include "MapGnssBars.h"

namespace {

// The box the acquisition screen actually gives it on a 480 px wide panel.
constexpr GnssSkyView::Box kBox{16, 240, 448, 200};

TEST(GnssSkyView, NorthSitsInTheMiddle) {
  const GnssSkyView::Dot north = GnssSkyView::plot(kBox, 45, 0, 30);
  EXPECT_NEAR(north.x, kBox.x + kBox.w / 2, 1);
}

TEST(GnssSkyView, SouthSitsAtBothEdges) {
  const GnssSkyView::Dot left = GnssSkyView::plot(kBox, 45, 180, 30);
  const GnssSkyView::Dot right = GnssSkyView::plot(kBox, 45, 179, 30);
  EXPECT_EQ(left.x, kBox.x);
  EXPECT_GE(right.x, kBox.x + kBox.w - 3);
}

TEST(GnssSkyView, EastIsRightOfNorthAndWestIsLeft) {
  const GnssSkyView::Dot east = GnssSkyView::plot(kBox, 45, 90, 30);
  const GnssSkyView::Dot north = GnssSkyView::plot(kBox, 45, 0, 30);
  const GnssSkyView::Dot west = GnssSkyView::plot(kBox, 45, 270, 30);
  EXPECT_GT(east.x, north.x);
  EXPECT_LT(west.x, north.x);
}

TEST(GnssSkyView, ZenithIsTheTopAndHorizonIsTheBottom) {
  const GnssSkyView::Dot zenith = GnssSkyView::plot(kBox, 90, 0, 30);
  const GnssSkyView::Dot horizon = GnssSkyView::plot(kBox, 0, 0, 30);
  EXPECT_EQ(zenith.y, kBox.y);
  EXPECT_EQ(horizon.y, kBox.y + kBox.h - 1);
}

// A receiver is not trusted to keep its own fields in range: an out-of-range
// elevation used to be a dot drawn above the box, over the header art.
TEST(GnssSkyView, OutOfRangeFieldsStayInsideTheBox) {
  for (uint16_t azimuth = 0; azimuth < 1000; azimuth += 7) {
    for (uint8_t elevation = 0; elevation < 200; elevation += 3) {
      const GnssSkyView::Dot dot = GnssSkyView::plot(kBox, elevation, azimuth, 20);
      EXPECT_GE(dot.x, kBox.x);
      EXPECT_LE(dot.x, kBox.x + kBox.w - 1);
      EXPECT_GE(dot.y, kBox.y);
      EXPECT_LE(dot.y, kBox.y + kBox.h - 1);
    }
  }
}

TEST(GnssSkyView, AzimuthWrapsWithoutAJump) {
  const GnssSkyView::Dot at359 = GnssSkyView::plot(kBox, 45, 359, 30);
  const GnssSkyView::Dot at360 = GnssSkyView::plot(kBox, 45, 360, 30);
  const GnssSkyView::Dot at0 = GnssSkyView::plot(kBox, 45, 0, 30);
  EXPECT_EQ(at360.x, at0.x);
  EXPECT_NEAR(at359.x, at0.x, 2);
}

// An untracked satellite is what the screen exists to distinguish, so the fill
// flag is the one field a redraw decision reads.
TEST(GnssSkyView, ZeroSnrIsAnOutline) {
  EXPECT_FALSE(GnssSkyView::plot(kBox, 45, 0, 0).filled);
  EXPECT_TRUE(GnssSkyView::plot(kBox, 45, 0, 1).filled);
}

TEST(GnssSkyView, SnrBucketsRiseAndSaturate) {
  EXPECT_EQ(GnssSkyView::snrBucket(0), 0);
  EXPECT_EQ(GnssSkyView::snrBucket(12), 1);  // heard, below the first rung
  EXPECT_EQ(GnssSkyView::snrBucket(30), 2);
  EXPECT_EQ(GnssSkyView::snrBucket(33), 3);
  EXPECT_EQ(GnssSkyView::snrBucket(38), 4);
  EXPECT_EQ(GnssSkyView::snrBucket(55), 4);

  uint8_t previous = 0;
  for (uint8_t snr = 1; snr < 60; ++snr) {
    const uint8_t bucket = GnssSkyView::snrBucket(snr);
    EXPECT_GE(bucket, previous);
    previous = bucket;
  }
}

// The dot ladder is the map header's calibrated C/N0 ladder, on purpose: two
// screens must not score the same sky differently. Asserted against the
// constants rather than against copies of them, so moving a rung in
// MapGnssBars fails here if this stops following it.
TEST(GnssSkyView, SnrBucketsFollowTheHeaderBarLadder) {
  const int cap = GnssSkyView::kMaxBucket;
  for (int step = 0; step < MapGnssBars::kHeightStepCount; ++step) {
    const uint8_t rung = MapGnssBars::kBestSnrForHeightStep[step];
    // Bucket 1 is "heard, below the first rung", so passing rung `step` lands
    // on step + 2, capped.
    const int atRung = std::min(step + 2, cap);
    const int justBelow = std::min(step + 1, cap);
    EXPECT_EQ(GnssSkyView::snrBucket(rung), atRung) << "at rung " << static_cast<int>(rung);
    EXPECT_EQ(GnssSkyView::snrBucket(static_cast<uint8_t>(rung - 1)), justBelow)
        << "just below rung " << static_cast<int>(rung);
  }
}

TEST(GnssSkyView, DotRadiusGrowsWithTheBucket) {
  int previous = 0;
  for (uint8_t bucket = 1; bucket <= 4; ++bucket) {
    const int radius = GnssSkyView::dotRadius(bucket);
    EXPECT_GE(radius, previous);
    previous = radius;
  }
}

// The ridge is drawn column by column, so a discontinuity is a visible notch in
// the silhouette rather than a wrong number.
TEST(GnssSkyView, RidgeIsContinuousAcrossTheBox) {
  int previous = GnssSkyView::ridgeHeight(kBox, 0);
  for (int column = 1; column < kBox.w; ++column) {
    const int height = GnssSkyView::ridgeHeight(kBox, column);
    EXPECT_LE(std::abs(height - previous), 3) << "notch at column " << column;
    EXPECT_GE(height, 0);
    EXPECT_LT(height, kBox.h);
    previous = height;
  }
}

TEST(GnssSkyView, RidgeClampsOutsideTheBox) {
  EXPECT_EQ(GnssSkyView::ridgeHeight(kBox, -50), GnssSkyView::ridgeHeight(kBox, 0));
  EXPECT_EQ(GnssSkyView::ridgeHeight(kBox, kBox.w + 50), GnssSkyView::ridgeHeight(kBox, kBox.w - 1));
}

TEST(GnssSkyView, LowSatellitesReadAsBehindTheRidgeAndHighOnesDoNot) {
  const GnssSkyView::Dot low = GnssSkyView::plot(kBox, 2, 0, 20);
  const GnssSkyView::Dot high = GnssSkyView::plot(kBox, 70, 0, 20);
  EXPECT_TRUE(GnssSkyView::behindRidge(kBox, low));
  EXPECT_FALSE(GnssSkyView::behindRidge(kBox, high));
}

// A degenerate box is what a panel narrower than the layout expects would
// produce, and a divide by zero there is a reset rather than a bad drawing.
TEST(GnssSkyView, DegenerateBoxDoesNotDivideByZero) {
  const GnssSkyView::Box empty{0, 0, 0, 0};
  const GnssSkyView::Dot dot = GnssSkyView::plot(empty, 45, 90, 20);
  EXPECT_EQ(dot.x, 0);
  EXPECT_EQ(dot.y, 0);
  EXPECT_EQ(GnssSkyView::ridgeHeight(empty, 5), 0);
}

}  // namespace
