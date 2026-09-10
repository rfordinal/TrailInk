#pragma once

#include <cstdint>

// Where a satellite lands on the acquisition screen's sky, and how strong its
// signal reads. Pure arithmetic, no renderer, no driver -- so it is host-tested
// (test/gnss_sky_view) and the activity above it only draws.
//
// ## The projection is a panorama, not a skyplot
//
// A GNSS skyplot is conventionally a circle seen from above: azimuth around the
// rim, elevation towards the centre. That is the right picture for an engineer
// checking geometry and the wrong one for a rider deciding where to stand,
// because the thing they are looking at is a horizon. **North up on a circle
// answers "which satellites"; a panorama answers "which way is the sky open",
// which is the only action available to someone waiting for a fix.**
//
// So: azimuth runs left to right across the box, elevation runs up from the
// ridge at its bottom edge. South is at both ends and north is in the middle,
// which is what a rider facing north sees. It is also cheap -- two divisions
// per satellite, no trigonometry, on a board that has a fix to wait for and no
// cycles to spare on drawing while it waits.
//
// ## The ridge is not decoration
//
// The bottom of the box is a mountain silhouette, and the same profile that
// makes the screen look like the home screen's art also states the physical
// fact behind a slow fix: a satellite low in the sky is behind terrain. A dot
// that sits in the ridge is one the rider cannot expect to help.
namespace GnssSkyView {

// The sky area in logical screen pixels. y is its top, y + h - 1 the horizon
// line the ridge is drawn on.
struct Box {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

struct Dot {
  int x = 0;
  int y = 0;
  int radius = 0;
  // Filled means tracked (a non-zero C/N0); an outline means the receiver knows
  // the satellite is up there and is not hearing it. The distinction is the
  // whole point of the screen -- a sky full of outlines is a sky full of
  // satellites the antenna cannot use, and no amount of waiting fixes it.
  bool filled = false;
};

// Elevation above which nothing is drawn any higher. 90 is the zenith and a
// receiver has been seen to report more than it should, so this clamps rather
// than trusting the field.
inline constexpr uint8_t kMaxElevation = 90;

// Signal buckets for the strength meter, and the dot radius ladder. Both are
// bucketed rather than continuous because the panel cannot show the difference:
// one device pixel of radius is the smallest step there is.
//
// The thresholds come from what the receiver here actually delivers. 24 dB-Hz
// is the number the firmware already treats as the line between a satellite
// that can carry a solution and one that cannot (MapActivity's marker trust,
// and Gnss::injectAidIni's comment on reading ephemeris off the air); indoors
// this antenna sits in the teens, and 40 is a clear sky.
inline uint8_t snrBucket(uint8_t snr) {
  if (snr == 0) return 0;
  if (snr < 18) return 1;
  if (snr < 24) return 2;
  if (snr < 34) return 3;
  return 4;
}

// Radius in device pixels for a bucket. Deliberately small and deliberately
// non-linear: the useful reading is "is this dot solid", not its exact size.
inline int dotRadius(uint8_t bucket) {
  switch (bucket) {
    case 0:
      return 3;  // outline, same size as a weak tracked one so the fill reads
    case 1:
      return 3;
    case 2:
      return 4;
    case 3:
      return 5;
    default:
      return 6;
  }
}

// Places one satellite. `azimuth` is degrees true, `elevation` degrees above
// the horizon; both come straight from GSV and are clamped here rather than at
// the call site, because a receiver that reports 91 or 400 must not draw
// outside the box.
//
// North is the centre of the box. The mapping is (azimuth + 180) mod 360, so
// the left edge is south, the middle north, the right edge south again.
inline Dot plot(const Box& box, uint8_t elevation, uint16_t azimuth, uint8_t snr) {
  Dot dot;
  const uint8_t bucket = snrBucket(snr);
  dot.filled = snr > 0;
  dot.radius = dotRadius(bucket);

  const int wrapped = static_cast<int>((azimuth % 360 + 180) % 360);
  const int usableWidth = box.w > 1 ? box.w - 1 : 1;
  dot.x = box.x + wrapped * usableWidth / 360;

  const uint8_t clamped = elevation > kMaxElevation ? kMaxElevation : elevation;
  const int horizon = box.y + (box.h > 1 ? box.h - 1 : 0);
  const int usableHeight = box.h > 1 ? box.h - 1 : 0;
  dot.y = horizon - clamped * usableHeight / kMaxElevation;
  return dot;
}

// The ridge profile: height above the box's bottom edge, in thousandths of the
// box height, sampled at eleven evenly spaced points across the width.
//
// Hand-drawn numbers, not a formula. They are the one place on this screen that
// is art rather than data, and they exist so the horizon under the satellites
// is the same mountain line the home screen's header carries -- one visual
// language, not a chart bolted under a logo.
inline constexpr int kRidgeSamples = 11;
inline constexpr int kRidgeProfile[kRidgeSamples] = {60, 110, 90, 190, 140, 250, 170, 120, 200, 100, 70};

// Ridge height at a pixel column, linearly interpolated between samples. Takes
// the column as an offset from box.x so a caller can walk the box's width.
inline int ridgeHeight(const Box& box, int column) {
  if (box.w <= 1) return 0;
  if (column < 0) column = 0;
  if (column > box.w - 1) column = box.w - 1;

  // Position along the profile in 1/256 steps of a sample interval, so the
  // interpolation is integer and monotonic. Fixed point rather than float
  // because this runs once per column of a ~460 px box on every redraw.
  const int span = (kRidgeSamples - 1) * 256;
  const int t = column * span / (box.w - 1);
  const int index = t / 256;
  if (index >= kRidgeSamples - 1) return kRidgeProfile[kRidgeSamples - 1] * box.h / 1000;
  const int frac = t % 256;
  const int a = kRidgeProfile[index];
  const int b = kRidgeProfile[index + 1];
  const int perMille = a + (b - a) * frac / 256;
  return perMille * box.h / 1000;
}

// True when a satellite is drawn inside the terrain rather than above it. Not
// used to hide it -- a rider needs to see that the sky is blocked in that
// direction, which is exactly the case the dot is reporting.
inline bool behindRidge(const Box& box, const Dot& dot) {
  const int horizon = box.y + (box.h > 1 ? box.h - 1 : 0);
  return dot.y >= horizon - ridgeHeight(box, dot.x - box.x);
}

}  // namespace GnssSkyView
