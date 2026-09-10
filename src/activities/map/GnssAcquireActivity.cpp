#include "GnssAcquireActivity.h"

#ifdef ENABLE_GNSS_CMD

#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "GnssAccess.h"
#include "MapGnssBars.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/HomeHeader.h"

namespace {

constexpr const char* kLogTag = "GNSSACQ";

// Never redraw faster than this. A windowed refresh on the T5 S3 Pro costs
// 1,081 ms measured (../docs/t5s3-partial-refresh.md), so a screen that
// redrew per fix would hold the panel busy for a third of every second of a
// ten-minute wait -- for a number that changes by one satellite.
constexpr uint32_t kMinRedrawMs = 5000;
// The clock's own resolution, matching the floor above: a coarse timer that
// moves when the screen redraws anyway costs nothing, and a fine one would
// force a redraw with nothing new in it.
constexpr uint32_t kClockStepMs = 5000;

// The cardinal ticks under the sky. Drawn as bare letters, not translated, the
// same choice the map's compass makes for its "N" (MapActivity::drawCompass()):
// it is a symbol on an instrument rather than a sentence.
constexpr const char* kCardinals[] = {"S", "W", "N", "E", "S"};
constexpr int kCardinalCount = 5;

}  // namespace

GnssAcquireActivity::GnssAcquireActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* routePath)
    : Activity("GnssAcquire", renderer, mappedInput) {
  if (routePath != nullptr && routePath[0] != '\0') {
    // Same refusal as MapActivity's: a truncated path opens the wrong file or
    // none, so it is dropped rather than shortened.
    const size_t len = std::strlen(routePath);
    if (len < sizeof(routePath_)) {
      std::memcpy(routePath_, routePath, len + 1);
    } else {
      LOG_ERR(kLogTag, "route path too long, ignored: %s", routePath);
    }
  }
}

void GnssAcquireActivity::onEnter() {
  Activity::onEnter();

  enteredMs_ = millis();
  lastRedrawMs_ = enteredMs_;

  // Whoever starts it, owns it. A receiver a host `CMD:GNSS ON` already brought
  // up is read here and left alone on the way out -- the same ownership rule
  // MapActivity::onEnter() follows, and the reason the handover into the map
  // carries a flag at all.
  if (gnss.running()) {
    LOG_INF(kLogTag, "gnss: already running, not started here");
  } else if (gnssStart()) {
    gnssStartedHere_ = true;
    LOG_INF(kLogTag, "gnss: started, rx ring %lu bytes", static_cast<unsigned long>(gnss.rxBufferSize()));
  } else {
    startFailed_ = true;
    LOG_ERR(kLogTag, "gnss: start failed, power rail or expander unavailable");
  }

  renderScreen();
}

void GnssAcquireActivity::onExit() {
  // Deliberately does NOT stop the receiver. Both exits decide that for
  // themselves: openMap(false) hands it to the map, openMap(true) and the Back
  // path drop it before leaving. Stopping it here would undo the handover a
  // moment after making it, and onExit() runs on every one of those paths.
  Activity::onExit();
}

void GnssAcquireActivity::loop() {
  Activity::loop();

  // The fix is what this screen is waiting for, so it is checked before input:
  // a rider whose fix lands while their thumb is moving should get the map, not
  // whatever row the thumb was on.
  //
  // Same acceptance test as the map's own (MapActivity::pollGnssFix): `valid`
  // latches on the first solution and never clears, so it says "has ever had
  // one"; `quality` is what says the receiver still has satellites, with 0 for
  // none and 6 for dead reckoning with nothing behind it.
  const GnssFix& fix = gnss.fix();
  if (gnss.running() && fix.valid && fix.quality != 0 && fix.quality != 6) {
    LOG_INF(kLogTag, "fix after %lu ms, %u sats used, quality %u", static_cast<unsigned long>(millis() - enteredMs_),
            static_cast<unsigned>(fix.satsUsed), static_cast<unsigned>(fix.quality));
    openMap(false);
    return;
  }

  bool selectionMoved = false;
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (selected_ != Action::OpenMap) {
      selected_ = Action::OpenMap;
      selectionMoved = true;
    }
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
      mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (selected_ != Action::UsePhone) {
      selected_ = Action::UsePhone;
      selectionMoved = true;
    }
  }

  // A direct tap on a row, which is the only input this board has in the
  // default touch mode: it draws no hint boxes, so Up/Down do not exist there
  // and the rows themselves have to be the buttons (../docs/touch-modes.md).
  for (int index = 0; index < kActionCount; ++index) {
    int x, y, w, h;
    actionRect(index, x, y, w, h);
    if (mappedInput.wasTapInRect(x, y, w, h)) {
      activate(static_cast<Action>(index));
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate(selected_);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Home, not the map. This screen is on the way to the map, and Back that
    // silently continued forwards would mean something different here than
    // everywhere else -- the same reasoning as the trip picker's Back.
    if (gnssStartedHere_) {
      gnss.end();
      gnssStartedHere_ = false;
      LOG_INF(kLogTag, "gnss: stopped, leaving for home");
    }
    onGoHome(HomeMenuItem::MAP);
    return;
  }

  if (selectionMoved) {
    drawActions();
    int x, y, w, h;
    actionsBand(x, y, w, h);
    refreshBand(x, y, w, h);
    return;
  }

  if (millis() - lastRedrawMs_ < kMinRedrawMs) return;
  if (!skyChanged()) return;
  lastRedrawMs_ = millis();
  drawSky();
  drawReadout();
  drawn_ = currentDrawn();
  int x, y, w, h;
  skyBand(x, y, w, h);
  refreshBand(x, y, w, h);
}

void GnssAcquireActivity::activate(Action action) {
  switch (action) {
    case Action::OpenMap:
      LOG_INF(kLogTag, "rider opened the map with the receiver still searching");
      openMap(false);
      return;
    case Action::UsePhone:
      LOG_INF(kLogTag, "rider chose the phone, dropping the receiver for this session");
      openMap(true);
      return;
  }
}

void GnssAcquireActivity::openMap(bool usePhone) {
  if (usePhone && gnssStartedHere_) {
    // The map session runs BLE, and one radio per session means this receiver
    // has no reader for as long as that map is up. Power it down here rather
    // than leaving a rail on with nobody listening -- the rail also feeds the
    // LoRa radio (main.cpp's gnssPowerEnable()).
    gnss.end();
    gnssStartedHere_ = false;
  }
  const char* route = routePath_[0] != '\0' ? routePath_ : nullptr;
  activityManager.goToMap(route, false, !usePhone && gnssStartedHere_, usePhone);
}

// ## Layout
//
// Bottom-anchored, then top-anchored, and the sky takes whatever is left. Every
// number below comes from the theme's metrics or the panel's own size: this
// screen has to hold on a 480x800 X4 and a 540x960 T5 S3 Pro, and a layout
// written against either one's pixels would be a defect on the other (parent
// repo's CLAUDE.md, "Styles must be universal").

int GnssAcquireActivity::readoutTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  int ax, ay, aw, ah;
  actionRect(0, ax, ay, aw, ah);
  (void)ax;
  (void)aw;
  (void)ah;
  // Four lines: the counts, the best signal with its meter, the clock, and the
  // one line of advice the screen exists to give.
  return ay - metrics.verticalSpacing - lineHeight * 4;
}

void GnssAcquireActivity::actionRect(int index, int& x, int& y, int& w, int& h) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  h = metrics.menuRowHeight;
  x = metrics.contentSidePadding;
  w = pageWidth - metrics.contentSidePadding * 2;
  const int blockBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int blockTop = blockBottom - (h * kActionCount + metrics.menuSpacing * (kActionCount - 1));
  y = blockTop + index * (h + metrics.menuSpacing);
}

GnssSkyView::Box GnssAcquireActivity::skyBox() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  GnssSkyView::Box box;
  box.x = metrics.contentSidePadding;
  box.w = pageWidth - metrics.contentSidePadding * 2;

  // The art is the top of the screen and the sky starts under it. Skipped
  // whole on a panel where keeping it would squeeze the sky into a strip --
  // the plot is the instrument, the art is not.
  const int artHeight = pageWidth >= HOMEHEADER_WIDTH ? HOMEHEADER_HEIGHT : 0;
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID) + lineHeight;
  const int top = metrics.topPadding + artHeight + titleHeight + metrics.verticalSpacing;
  // One line for the cardinal ticks under the horizon.
  const int bottom = readoutTop() - metrics.verticalSpacing - lineHeight;

  box.y = top;
  box.h = bottom - top;
  return box;
}

void GnssAcquireActivity::skyBand(int& x, int& y, int& w, int& h) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GnssSkyView::Box box = skyBox();
  int ax, ay, aw, ah;
  actionRect(0, ax, ay, aw, ah);
  (void)ax;
  (void)aw;
  (void)ah;
  // The sky, its ticks and the readout under it, as one rectangle: they change
  // together on every satellite update, and two windows cost two refreshes.
  x = 0;
  w = renderer.getScreenWidth();
  y = box.y;
  h = ay - metrics.verticalSpacing - box.y;
}

void GnssAcquireActivity::actionsBand(int& x, int& y, int& w, int& h) const {
  int x0, y0, w0, h0;
  int x1, y1, w1, h1;
  actionRect(0, x0, y0, w0, h0);
  actionRect(1, x1, y1, w1, h1);
  x = 0;
  w = renderer.getScreenWidth();
  y = y0;
  h = (y1 + h1) - y0;
}

void GnssAcquireActivity::refreshBand(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }
  // The driver refuses a window in some states and promotes it to a whole
  // frame in others (GfxRenderer::displayBufferWindow). A refusal is a false
  // return, and the panel would otherwise keep the old picture.
  if (!renderer.displayBufferWindow(x, y, w, h)) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

void GnssAcquireActivity::renderScreen() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();

  // The home screen's own header art: the logo, the wordmark and the mountain
  // line under it. Reused rather than redrawn so this screen reads as part of
  // the same device and not as a diagnostic panel -- and the mountains are what
  // the sky below is drawn against.
  int y = metrics.topPadding;
  if (pageWidth >= HOMEHEADER_WIDTH) {
    renderer.drawMono1bpp(HomeHeader, (pageWidth - HOMEHEADER_WIDTH) / 2, y, HOMEHEADER_WIDTH, HOMEHEADER_HEIGHT, true);
    y += HOMEHEADER_HEIGHT;
  }

  // Title and subtitle centred under the art, so the head of the screen reads
  // as one block with it rather than as a heading bolted underneath.
  const char* title = tr(STR_GNSS_ACQ_TITLE);
  const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, title);
  renderer.drawText(UI_12_FONT_ID, (pageWidth - titleWidth) / 2, y, title, true);
  const char* subtitle = tr(STR_GNSS_ACQ_SUB);
  const int subtitleWidth = renderer.getTextWidth(UI_10_FONT_ID, subtitle);
  renderer.drawText(UI_10_FONT_ID, (pageWidth - subtitleWidth) / 2, y + renderer.getLineHeight(UI_12_FONT_ID), subtitle,
                    true);

  drawSky();
  drawReadout();
  drawActions();

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  drawn_ = currentDrawn();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void GnssAcquireActivity::drawSky() {
  const GnssSkyView::Box box = skyBox();
  if (box.w <= 0 || box.h <= 0) return;

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int horizon = box.y + box.h - 1;

  // Repaint the band first: this runs on a redraw as well as on the first
  // frame, and a satellite that moved has to leave nothing behind.
  renderer.fillRect(box.x, box.y, box.w, box.h + lineHeight, false);

  // The ridge, one column at a time. A filled polygon would need a point per
  // sample and would still be interpolated by hand; the per-column fill is the
  // same arithmetic the host test checks (GnssSkyView::ridgeHeight).
  for (int column = 0; column < box.w; ++column) {
    const int height = GnssSkyView::ridgeHeight(box, column);
    if (height <= 0) continue;
    renderer.fillRect(box.x + column, horizon - height, 1, height, true);
  }
  renderer.drawLine(box.x, horizon, box.x + box.w - 1, horizon, true);

  // The satellites. Diamonds rather than circles because the renderer has no
  // circle primitive, and a diamond reads as a mark on an instrument rather
  // than as a map dot -- which matters on a device whose other screen is a map.
  const uint8_t count = gnss.satelliteCount();
  for (uint8_t i = 0; i < count; ++i) {
    const GnssSatellite& sat = gnss.satellite(i);
    // A satellite the receiver has an almanac for but has not located carries
    // no elevation or azimuth, and (0,0) is due north on the horizon -- a real
    // position, and the worst one there is. Those are counted in the readout
    // and not drawn.
    if (!sat.hasPosition) continue;

    const GnssSkyView::Dot dot = GnssSkyView::plot(box, sat.elevation, sat.azimuth, sat.snr);
    // White in the silhouette, black in the sky. A satellite low in a blocked
    // direction is exactly what a rider needs to see, so it is drawn either
    // way -- and in black it would vanish into the ridge.
    const bool ink = !GnssSkyView::behindRidge(box, dot);
    const int r = dot.radius;
    const int xs[4] = {dot.x, dot.x + r, dot.x, dot.x - r};
    const int ys[4] = {dot.y - r, dot.y, dot.y + r, dot.y};
    if (dot.filled) {
      renderer.fillPolygon(xs, ys, 4, ink);
    } else {
      for (int p = 0; p < 4; ++p) {
        const int q = (p + 1) % 4;
        renderer.drawLine(xs[p], ys[p], xs[q], ys[q], ink);
      }
    }
  }

  // Cardinal ticks under the horizon, so "which way do I move" has an answer.
  for (int i = 0; i < kCardinalCount; ++i) {
    const int x = box.x + (box.w - 1) * i / (kCardinalCount - 1);
    const int labelWidth = renderer.getTextWidth(UI_10_FONT_ID, kCardinals[i]);
    int labelX = x - labelWidth / 2;
    if (labelX < box.x) labelX = box.x;
    if (labelX + labelWidth > box.x + box.w) labelX = box.x + box.w - labelWidth;
    renderer.drawText(UI_10_FONT_ID, labelX, horizon + 2, kCardinals[i], true);
  }
}

void GnssAcquireActivity::drawReadout() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int pageWidth = renderer.getScreenWidth();
  int y = readoutTop();

  renderer.fillRect(0, y, pageWidth, lineHeight * 4, false);

  const uint8_t inView = gnss.satsInView();
  const uint8_t heard = gnss.satsWithSignal();
  const uint8_t best = gnss.bestSnr();

  char line[96];
  if (startFailed_) {
    snprintf(line, sizeof(line), "%s", tr(STR_GNSS_ACQ_NO_RECEIVER));
  } else if (inView == 0) {
    // Not the same thing as a weak sky: the receiver has not finished its first
    // GSV sweep, or it is hearing nothing at all. Either way there is no count
    // worth printing yet, and a "0 in view" reads as a broken antenna.
    snprintf(line, sizeof(line), "%s", tr(STR_GNSS_ACQ_SEARCHING));
  } else {
    snprintf(line, sizeof(line), tr(STR_GNSS_ACQ_COUNTS), static_cast<int>(inView), static_cast<int>(heard));
  }
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, line, true);
  y += lineHeight;

  // The best signal as a number, and next to it the map header's own GNSS block
  // at a readable size: how many bars are lit says how many satellites the
  // antenna hears, how tall they are says how strong the best one is
  // (MapGnssBars.h). **The same instrument on both screens, deliberately** --
  // this is where a rider learns to read it, and a wait screen that scored the
  // sky on its own ladder would teach them the wrong one.
  //
  // No hysteresis state: a default State() applies none, which is right here.
  // The block on the header wobbles because the map repaints per fix; this
  // screen redraws at most once every five seconds and has nothing to damp.
  snprintf(line, sizeof(line), tr(STR_GNSS_ACQ_BEST), static_cast<int>(best));
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, line, true);
  const MapGnssBars::Block block = MapGnssBars::resolve(heard, best, MapGnssBars::State{});
  const int meterX = metrics.contentSidePadding + renderer.getTextWidth(UI_10_FONT_ID, line) + lineHeight / 2;
  const int blockWidth = lineHeight / 2;
  const int blockGap = blockWidth / 3 + 1;
  const int barHeight = MapGnssBars::barHeightPx(block.heightStep, lineHeight);
  for (int bar = 0; bar < MapGnssBars::kBarCount; ++bar) {
    const int bx = meterX + bar * (blockWidth + blockGap);
    // The header draws nothing at all below the first rung. Here the empty
    // slots stay as outlines: this screen is up for minutes with nothing to
    // show, and an instrument that disappears reads as a broken one.
    const int bh = barHeight > 0 ? barHeight : 2;
    const int by = y + lineHeight - bh - 2;
    if (bar < block.bars && barHeight > 0) {
      renderer.fillRect(bx, by, blockWidth, bh, true);
    } else {
      renderer.drawRect(bx, by, blockWidth, bh, true);
    }
  }
  y += lineHeight;

  const uint32_t waitedS = (millis() - enteredMs_) / 1000;
  snprintf(line, sizeof(line), tr(STR_GNSS_ACQ_WAITED), static_cast<int>(waitedS / 60), static_cast<int>(waitedS % 60));
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, line, true);
  y += lineHeight;

  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, tr(STR_GNSS_ACQ_HINT), true);
}

void GnssAcquireActivity::drawActions() {
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  for (int index = 0; index < kActionCount; ++index) {
    int x, y, w, h;
    actionRect(index, x, y, w, h);
    const bool selected = static_cast<int>(selected_) == index;
    // Filled when selected, outlined otherwise, same as every other list on the
    // device (BaseTheme::drawHomeMenu). The label inverts with it.
    renderer.fillRect(x, y, w, h, selected);
    if (!selected) renderer.drawRect(x, y, w, h, true);

    const char* label =
        index == static_cast<int>(Action::OpenMap) ? tr(STR_GNSS_ACQ_OPEN_MAP) : tr(STR_GNSS_ACQ_USE_PHONE);
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label);
    renderer.drawText(UI_10_FONT_ID, x + (w - textWidth) / 2, y + (h - lineHeight) / 2, label, !selected);
  }
}

GnssAcquireActivity::Drawn GnssAcquireActivity::currentDrawn() const {
  Drawn now;
  now.inView = gnss.satsInView();
  now.heard = gnss.satsWithSignal();
  now.bestSnr = gnss.bestSnr();
  now.satellites = gnss.satelliteCount();
  now.waitedSteps = static_cast<uint16_t>((millis() - enteredMs_) / kClockStepMs);
  now.receiverUp = gnss.running();
  return now;
}

bool GnssAcquireActivity::skyChanged() const {
  const Drawn now = currentDrawn();
  return now.inView != drawn_.inView || now.heard != drawn_.heard || now.bestSnr != drawn_.bestSnr ||
         now.satellites != drawn_.satellites || now.waitedSteps != drawn_.waitedSteps ||
         now.receiverUp != drawn_.receiverUp;
}

#endif  // ENABLE_GNSS_CMD
