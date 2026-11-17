// Host unit tests for src/filters/burst_filter.h.
// Run: bash test/unit/run.sh   (needs g++)

#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../../src/filters/burst_filter.h"

static int failures = 0;
#define CHECK(cond, ...)                                     \
  do {                                                       \
    if (!(cond)) {                                           \
      failures++;                                            \
      std::printf("FAIL %s:%d: ", __FILE__, __LINE__);       \
      std::printf(__VA_ARGS__);                              \
      std::printf("\n");                                     \
    }                                                        \
  } while (0)

using Filter = BurstFilter<5, 9>;

static std::vector<int16_t> run(Filter& f, const std::vector<int16_t>& in) {
  std::vector<int16_t> out;
  for (int16_t v : in) out.push_back(f.push(v));
  return out;
}

static void testWifiSpikesRemoved() {
  // -97 dBm floor with 1-4 ms Wi-Fi bursts to -70 every 24 ms.
  Filter f;
  f.reset(-97);
  std::vector<int16_t> in;
  for (int i = 0; i < 2000; i++) {
    int phase = i % 24;
    int width = 1 + (i / 24) % 4;  // 1..4 ms
    in.push_back(phase < width ? -70 : -97);
  }
  auto out = run(f, in);
  for (size_t i = 0; i < out.size(); i++) {
    CHECK(out[i] == -97, "spike leaked at %zu: %d", i, out[i]);
    if (out[i] != -97) break;
  }
}

static void testVtxDipsBridged() {
  // Steady -50 dBm VTX with 1-8 ms dips of 14 dB.
  Filter f;
  f.reset(-50);
  std::vector<int16_t> in;
  for (int i = 0; i < 2000; i++) {
    int phase = i % 40;
    int width = 1 + (i / 40) % 8;  // 1..8 ms
    in.push_back(phase < width ? -64 : -50);
  }
  auto out = run(f, in);
  for (size_t i = 0; i < out.size(); i++) {
    CHECK(out[i] == -50, "dip leaked at %zu: %d", i, out[i]);
    if (out[i] != -50) break;
  }
}

static void testPassShapeAndDelay() {
  // A 300 ms pass: floor -95, ramp to -40 and back. Peak must survive and be
  // delayed by exactly kDelaySamples.
  Filter f;
  f.reset(-95);
  std::vector<int16_t> in;
  for (int i = 0; i < 100; i++) in.push_back(-95);
  for (int i = 0; i <= 55; i++) in.push_back(-95 + i);       // rise
  for (int i = 54; i >= 0; i--) in.push_back(-95 + i);       // fall
  for (int i = 0; i < 100; i++) in.push_back(-95);
  auto out = run(f, in);
  int inPeak = 0, outPeak = 0;
  for (size_t i = 0; i < in.size(); i++) {
    if (in[i] > in[inPeak]) inPeak = (int)i;
    if (out[i] > out[outPeak]) outPeak = (int)i;
  }
  // A strict 1-sample peak is shaved by the opening (it is a 1 ms "spike"
  // at the top). The plateau of the output must be within 2 dB of it.
  CHECK(out[outPeak] >= in[inPeak] - 2, "peak lost: in %d out %d", in[inPeak], out[outPeak]);
  int delay = outPeak - inPeak;
  CHECK(delay >= 0 && delay <= Filter::kDelaySamples,
        "peak delay %d outside 0..%d", delay, Filter::kDelaySamples);
  CHECK(out.back() == -95, "did not return to floor: %d", out.back());
}

static void testStepEdgesSymmetric() {
  Filter f;
  f.reset(-95);
  std::vector<int16_t> in(50, -95);
  in.insert(in.end(), 100, -45);
  in.insert(in.end(), 100, -95);
  auto out = run(f, in);
  int rise = -1, fall = -1;
  for (size_t i = 1; i < out.size(); i++) {
    if (rise < 0 && out[i] == -45) rise = (int)i;
    if (rise >= 0 && fall < 0 && out[i] == -95) fall = (int)i;
  }
  CHECK(rise - 50 == Filter::kDelaySamples, "rise delay %d", rise - 50);
  CHECK(fall - 150 == Filter::kDelaySamples, "fall delay %d", fall - 150);
}

static void testScale() {
  CHECK(dbmToRssi(-100, -100, -20) == 0, "floor");
  CHECK(dbmToRssi(-120, -100, -20) == 0, "below floor");
  CHECK(dbmToRssi(-20, -100, -20) == 255, "ceiling");
  CHECK(dbmToRssi(0, -100, -20) == 255, "above ceiling");
  CHECK(dbmToRssi(-60, -100, -20) == 128, "midpoint %d", dbmToRssi(-60, -100, -20));
  CHECK(dbmToRssi(-60, -20, -100) == 0, "bad calibration");
  uint8_t prev = 0;
  for (int d = -100; d <= -20; d++) {
    uint8_t v = dbmToRssi(d, -100, -20);
    CHECK(v >= prev, "not monotonic at %d", d);
    prev = v;
  }
}

int main() {
  testWifiSpikesRemoved();
  testVtxDipsBridged();
  testPassShapeAndDelay();
  testStepEdgesSymmetric();
  testScale();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("burst_filter: all checks passed\n");
  return 0;
}
