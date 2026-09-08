#include "BootLog.h"

#include <Arduino.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_sleep.h>
#include <esp_system.h>

namespace {

constexpr const char* kLogTag = "BOOTLOG";

// Column order is the file format, same contract as power.csv: appending a
// column is fine, reordering one breaks every earlier row a script reads.
//
// There is deliberately no timestamp. The reference board (LilyGo T5 S3 Pro)
// carries no RTC -- its profile is NO_SENSORS -- and nothing has a wall clock
// this early in boot anyway. Rows are therefore ordered but not dated, and they
// cannot be joined to a power.csv run automatically: a boot that parks writes a
// row here and no block there, so the two files can drift apart by a row. Read
// them by eye, newest row last.
constexpr const char* kHeader = "reset_reason,wake_cause,resolved,parked,batt_mv,build\n";

// esp_reset_reason_t as text. Named here rather than printed as an integer
// because the whole point of the file is to be read on a phone at a hotel,
// away from the enum.
const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:
      return "UNKNOWN";
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "OTHER";
  }
}

// The two watchdog reasons above are the ones this file exists to catch. A
// watchdog reset writes no crash_report.txt at all and does not trip the crash
// screen, because HalSystem::isRebootFromPanic() tests only PANIC and
// CPU_LOCKUP (docs/crash-reporting.md, "Gap 3"; T-234). Until this row existed
// such a reset left no trace the person carrying it could find.
const char* wakeupCauseName(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "UNDEFINED";
    case ESP_SLEEP_WAKEUP_EXT0:
      return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "TIMER";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      return "TOUCHPAD";
    case ESP_SLEEP_WAKEUP_ULP:
      return "ULP";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "GPIO";
    case ESP_SLEEP_WAKEUP_UART:
      return "UART";
    default:
      return "OTHER";
  }
}

}  // namespace

void BootLog::record(const char* resolved, bool parked) {
  const char* reset = resetReasonName(esp_reset_reason());
  const char* wake = wakeupCauseName(esp_sleep_get_wakeup_cause());
  const unsigned battMv = static_cast<unsigned>(powerManager.getBatteryMillivolts());

  // Logged as well as written, because a session with a serial capture open
  // wants it without mounting the card, and because the write below can fail.
  LOG_INF(kLogTag, "boot: reset=%s wake=%s resolved=%s parked=%d batt=%umV", reset, wake, resolved,
          static_cast<int>(parked), battMv);

  if (!Storage.ready()) {
    // The one case worth saying out loud: no card means this boot leaves no
    // record, which is the failure this file was added to end.
    LOG_ERR(kLogTag, "no SD card -- this boot leaves no record");
    return;
  }

  const bool fresh = !Storage.exists(kPath);
  // A card that has never had tiles pushed to it has no /trailink yet, and
  // O_CREAT does not create the parent. Same trap PowerLog hit.
  if (fresh) Storage.ensureDirectoryExists(kDir);

  HalFile file = Storage.open(kPath, O_WRITE | O_CREAT | O_APPEND);
  if (!file.isOpen()) {
    LOG_ERR(kLogTag, "cannot open %s", kPath);
    return;
  }
  // Only on a fresh file, unlike power.csv. That file repeats its header every
  // boot because it writes many rows per boot and a reader needs the boot
  // marker; here every row *is* a boot, so a repeated header would double the
  // file and mark nothing.
  if (fresh) file.print(kHeader);

  // TRAILINK_VERSION goes through %s and is never concatenated into the format
  // string: it carries a branch name, and a '%' in one would make printf read
  // an argument that was never passed.
  file.printf("%s,%s,%s,%d,%u,%s\n", reset, wake, resolved, static_cast<int>(parked), battMv, TRAILINK_VERSION);

  // Explicit, and the reason this function exists: the caller may be about to
  // call startDeepSleep(), which does not return. An unflushed row would be
  // lost in exactly the case worth recording.
  file.flush();
}
