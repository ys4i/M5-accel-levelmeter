// M5StickC Plus2 acceleration level meter with NeoPixel
// Implements bidirectional VU-style meter; calibration via BtnA long-press.

#include <M5Unified.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

// Set to 1 at build time to enable on-screen debug.
#ifndef ENABLE_DEBUG
#define ENABLE_DEBUG 1
#endif

// Hardware config
static const int LED_PIN = 32;         // NeoPixel data pin
static const int CAL_SAMPLES = 200;    // Calibration sample count
static const uint32_t CAL_HOLD_MS = 700; // BtnA long-press threshold

enum RenderMode {
  RENDER_CENTER = 0,
  RENDER_SHOULDER_MIRROR = 1,
};

struct Config {
  int led_count;
  uint8_t brightness_max;
  float g_range_pos;
  float g_range_neg;
  float alpha;
  int update_hz;
  bool dim_unused;
  RenderMode render_mode;
};

struct Calibration {
  bool ready;
  float g0[3];
  float n[3];
  float base_g;
};

// Default config (modifiable in code; not persisted)
Config config = {
  .led_count = 60,
  .brightness_max = 255,
  .g_range_pos = 1.0f,
  .g_range_neg = 1.0f,
  .alpha = 0.3f,
  .update_hz = 20,
  .dim_unused = false,
  .render_mode = RENDER_SHOULDER_MIRROR,
};

Adafruit_NeoPixel strip(config.led_count, LED_PIN, NEO_GRB + NEO_KHZ800);
// Center LED color; change this single line to adjust both renderers.
uint32_t center_color = strip.Color(255, 0, 0);
Calibration calib = {};

float delta_filt = 0.0f;
uint32_t last_update_ms = 0;
bool calibrating = false;
bool hold_handled = false;

// Simple color helpers
uint32_t makeColor(uint8_t r, uint8_t g, uint8_t b) {
  return strip.Color(r, g, b);
}

uint32_t lerpColor(uint32_t c1, uint32_t c2, float t) {
  t = constrain(t, 0.0f, 1.0f);
  uint8_t r1 = (c1 >> 16) & 0xFF;
  uint8_t g1 = (c1 >> 8) & 0xFF;
  uint8_t b1 = c1 & 0xFF;
  uint8_t r2 = (c2 >> 16) & 0xFF;
  uint8_t g2 = (c2 >> 8) & 0xFF;
  uint8_t b2 = c2 & 0xFF;
  uint8_t r = r1 + (uint8_t)((r2 - r1) * t);
  uint8_t g = g1 + (uint8_t)((g2 - g1) * t);
  uint8_t b = b1 + (uint8_t)((b2 - b1) * t);
  return strip.Color(r, g, b);
}

uint32_t scaleColor(uint32_t c, float s) {
  s = constrain(s, 0.0f, 1.0f);
  uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * s);
  uint8_t g = (uint8_t)(((c >> 8) & 0xFF) * s);
  uint8_t b = (uint8_t)((c & 0xFF) * s);
  return strip.Color(r, g, b);
}

// Positive side: green -> yellow -> red
uint32_t gradPositive(float t) {
  t = constrain(t, 0.0f, 1.0f);
  const uint32_t green = makeColor(0, 255, 0);
  const uint32_t yellow = makeColor(255, 255, 0);
  const uint32_t red = makeColor(255, 0, 0);
  if (t < 0.5f) {
    return lerpColor(green, yellow, t * 2.0f);
  }
  return lerpColor(yellow, red, (t - 0.5f) * 2.0f);
}

// Negative side: green -> blue -> purple
uint32_t gradNegative(float t) {
  t = constrain(t, 0.0f, 1.0f);
  const uint32_t green = makeColor(0, 255, 0);
  const uint32_t blue = makeColor(0, 0, 255);
  const uint32_t purple = makeColor(128, 0, 255);
  if (t < 0.5f) {
    return lerpColor(green, blue, t * 2.0f);
  }
  return lerpColor(blue, purple, (t - 0.5f) * 2.0f);
}

float vecNorm(const float v[3]) {
  return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void normalize(float v[3]) {
  float n = vecNorm(v);
  if (n > 1e-6f) {
    v[0] /= n;
    v[1] /= n;
    v[2] /= n;
  }
}

void renderLevelCenter(float level_pos, float level_neg) {
  strip.clear();

  const float dim_factor = config.dim_unused ? 0.08f : 0.0f;

  const bool even = (config.led_count % 2 == 0);
  const int center_right = config.led_count / 2;
  const int center_left = even ? center_right - 1 : center_right;

  const int neg_span = center_left; // number of LEDs on negative side
  const int pos_span = config.led_count - center_right - 1; // number on positive side

  const int num_neg = (int)floorf(constrain(level_neg, 0.0f, 1.0f) * neg_span + 1e-4f);
  const int num_pos = (int)floorf(constrain(level_pos, 0.0f, 1.0f) * pos_span + 1e-4f);

  // Center LEDs (one or two) always on
  strip.setPixelColor(center_right, center_color);
  if (even) {
    strip.setPixelColor(center_left, center_color);
  }

  // Negative side: from center_left-1 down to 0
  for (int i = 0; i < neg_span; ++i) {
    int idx = center_left - 1 - i;
    float t = (float)(i + 1) / (float)neg_span; // 0->1 toward end
    uint32_t c = (i < num_neg) ? gradNegative(t) : scaleColor(gradNegative(t), dim_factor);
    strip.setPixelColor(idx, c);
  }

  // Positive side: from center_right+1 up to end
  for (int i = 0; i < pos_span; ++i) {
    int idx = center_right + 1 + i;
    float t = (float)(i + 1) / (float)pos_span;
    uint32_t c = (i < num_pos) ? gradPositive(t) : scaleColor(gradPositive(t), dim_factor);
    strip.setPixelColor(idx, c);
  }

  strip.show();
}

void renderHalf(int start, int len, bool start_is_positive, float level_pos, float level_neg) {
  const float dim_factor = config.dim_unused ? 0.08f : 0.0f;

  const bool even = (len % 2 == 0);
  const int center_right = start + len / 2;
  const int center_left = even ? center_right - 1 : center_right;

  const int left_span = center_left - start;
  const int right_span = start + len - 1 - center_right;

  strip.setPixelColor(center_right, center_color);
  if (even) {
    strip.setPixelColor(center_left, center_color);
  }

  for (int i = 0; i < left_span; ++i) {
    int idx = center_left - 1 - i;
    float t = (left_span > 0) ? (float)(i + 1) / (float)left_span : 0.0f;
    bool pos_side = start_is_positive;
    bool lit = pos_side ? (i < (int)floorf(level_pos * left_span + 1e-4f))
                        : (i < (int)floorf(level_neg * left_span + 1e-4f));
    uint32_t base = pos_side ? gradPositive(t) : gradNegative(t);
    strip.setPixelColor(idx, lit ? base : scaleColor(base, dim_factor));
  }

  for (int i = 0; i < right_span; ++i) {
    int idx = center_right + 1 + i;
    float t = (right_span > 0) ? (float)(i + 1) / (float)right_span : 0.0f;
    bool pos_side = !start_is_positive;
    bool lit = pos_side ? (i < (int)floorf(level_pos * right_span + 1e-4f))
                        : (i < (int)floorf(level_neg * right_span + 1e-4f));
    uint32_t base = pos_side ? gradPositive(t) : gradNegative(t);
    strip.setPixelColor(idx, lit ? base : scaleColor(base, dim_factor));
  }
}

void renderLevelShoulder(float level_pos, float level_neg) {
  strip.clear();

  // Split tape into two blocks so both faces show the same pattern.
  const int first_len = config.led_count / 2;
  const int second_len = config.led_count - first_len;

  // Front block: negative -> center -> positive
  renderHalf(0, first_len, false, level_pos, level_neg);
  // Back block: positive -> center -> negative
  renderHalf(first_len, second_len, true, level_pos, level_neg);

  strip.show();
}

void renderLevel(float level_pos, float level_neg) {
  if (config.render_mode == RENDER_SHOULDER_MIRROR) {
    renderLevelShoulder(level_pos, level_neg);
  } else {
    renderLevelCenter(level_pos, level_neg);
  }
}

void showCalibratingDisplay() {
#if ENABLE_DEBUG
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_GREEN);
  M5.Display.println("Calibrating...");
  M5.Display.println("Hold still");
#endif
}

void showStatusDisplay(float delta, float level_pos, float level_neg, int num_pos, int num_neg) {
#if ENABLE_DEBUG
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.printf("delta: %.3f\n", delta);
  M5.Display.printf("pos: %.2f (%d)\n", level_pos, num_pos);
  M5.Display.printf("neg: %.2f (%d)\n", level_neg, num_neg);
#endif
}

void calibrate() {
  calibrating = true;
  strip.clear();
  strip.show();
  showCalibratingDisplay();

  float acc[3] = {0, 0, 0};
  float sum[3] = {0, 0, 0};

  for (int i = 0; i < CAL_SAMPLES; ++i) {
    M5.Imu.getAccelData(&acc[0], &acc[1], &acc[2]);
    sum[0] += acc[0];
    sum[1] += acc[1];
    sum[2] += acc[2];
    delay(2); // small wait between samples
  }

  calib.g0[0] = sum[0] / (float)CAL_SAMPLES;
  calib.g0[1] = sum[1] / (float)CAL_SAMPLES;
  calib.g0[2] = sum[2] / (float)CAL_SAMPLES;
  calib.base_g = vecNorm(calib.g0);
  calib.n[0] = calib.g0[0];
  calib.n[1] = calib.g0[1];
  calib.n[2] = calib.g0[2];
  normalize(calib.n);
  calib.ready = true;

  calibrating = false;
}

void setup() {
  auto cfg = M5.config();
  cfg.external_imu = false;
  M5.begin(cfg);

  strip.begin();
  strip.setBrightness(config.brightness_max);
  strip.clear();
  strip.show();

#if ENABLE_DEBUG
  M5.Display.setRotation(3);
  M5.Display.setTextSize(2);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.println("Hold BtnA");
  M5.Display.println("to calibrate");
#endif
}

void loop() {
  M5.update();

  // Long-press detection for calibration
  if (!calibrating && M5.BtnA.pressedFor(CAL_HOLD_MS) && !hold_handled) {
    calibrate();
    hold_handled = true;
  }
  if (M5.BtnA.wasReleased()) {
    hold_handled = false;
  }

  if (!calib.ready) {
    // Keep LEDs off until initial calibration completes
    strip.clear();
    strip.show();
    delay(10);
    return;
  }

  const uint32_t interval_ms = 1000 / config.update_hz;
  const uint32_t now = millis();
  if (now - last_update_ms < interval_ms) {
    delay(1);
    return;
  }
  last_update_ms = now;

  float ax, ay, az;
  M5.Imu.getAccelData(&ax, &ay, &az);

  // Projection onto gravity direction
  float a_par = ax * calib.n[0] + ay * calib.n[1] + az * calib.n[2];
  float delta = a_par - calib.base_g;

  // EMA smoothing
  delta_filt = (1.0f - config.alpha) * delta_filt + config.alpha * delta;

  float level_pos = 0.0f;
  float level_neg = 0.0f;
  if (delta_filt > 0.0f) {
    level_pos = constrain(delta_filt / config.g_range_pos, 0.0f, 1.0f);
  } else {
    level_neg = constrain(-delta_filt / config.g_range_neg, 0.0f, 1.0f);
  }

  // Precompute counts for debug display
  const bool even = (config.led_count % 2 == 0);
  const int center_right = config.led_count / 2;
  const int center_left = even ? center_right - 1 : center_right;
  const int neg_span = center_left;
  const int pos_span = config.led_count - center_right - 1;
  const int num_neg = (int)floorf(level_neg * neg_span + 1e-4f);
  const int num_pos = (int)floorf(level_pos * pos_span + 1e-4f);

  renderLevel(level_pos, level_neg);
  showStatusDisplay(delta_filt, level_pos, level_neg, num_pos, num_neg);
}
