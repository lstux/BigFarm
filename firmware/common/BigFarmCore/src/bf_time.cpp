#include "bf_time.h"

namespace bf {

static const time_t kValidAfter = 1700000000;  // novembre 2023 : en dessous, l'horloge n'est pas réglée
static const char *gTz = "";

void timeBegin(const char *tz) {
  gTz = tz;
  configTzTime(tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

bool timeValid() { return time(nullptr) > kValidAfter; }

String timeText(time_t t) {
  if (t <= kValidAfter) return String("—");
  struct tm tm;
  localtime_r(&t, &tm);
  char buf[24];
  strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
  return String(buf);
}

String nowText() { return timeText(time(nullptr)); }

String fileTimestamp() {
  const time_t t = time(nullptr);
  if (t <= kValidAfter) return String();
  struct tm tm;
  localtime_r(&t, &tm);
  char buf[20];
  strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &tm);
  return String(buf);
}

void addTime(JsonWriter &json) {
  json.beginObject("heure");
  json.add("synchronisee", timeValid());
  json.add("locale", nowText());
  json.add("fuseau", gTz);
  json.endObject();
}

}  // namespace bf
