#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "esphome/components/light/light_state.h"
#include "esphome/core/hal.h"

// Effects that drive the red, green and blue LEDs on their own smooth noise, so
// each LED's coloured shadow moves independently. Every value is a light level
// before gamma, so the outputs' max_power cap still bounds the peak.
namespace esphome::colorshadow {

enum class Scene {
  CANDLE,
  FIRE,
  ICY_FIRE,
  PURPLE_FIRE,
  CHRISTMAS,
  AURORA,
  OCEAN,
  SUNSET,
  LAVA,
  DRIFT,
};

/// A repeatable random height in [0, 1] for each whole step of noise.
inline float lattice(int64_t i, uint32_t seed) {
  uint32_t h = static_cast<uint32_t>(i) * 0x9E3779B1u ^ static_cast<uint32_t>(i >> 32) ^ seed * 0x85EBCA77u;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

/// Value noise in [0, 1] at x, eased between lattice points so it never jumps.
inline float noise(double x, uint32_t seed) {
  const double whole = std::floor(x);
  const int64_t i = static_cast<int64_t>(whole);
  const float f = static_cast<float>(x - whole);
  const float e = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
  const float a = lattice(i, seed);
  return a + (lattice(i + 1, seed) - a) * e;
}

/// Two octaves of noise at `hz` slow swells per second, centred on 0 in [-1, 1].
inline float wobble(uint32_t ms, float hz, uint32_t seed) {
  const double x = static_cast<double>(ms) * hz / 1000.0;
  return 2.0f * (0.65f * noise(x, seed) + 0.35f * noise(x * 2.3 + 17.0, seed + 101u)) - 1.0f;
}

/// Light the LEDs at one frame of `scene`. Call it from a lambda effect.
inline void render(light::LightState &state, Scene scene, bool initial_run) {
  // The effect fades in from whatever the light showed before it started.
  static constexpr uint32_t FADE_IN_MS = 800;
  static uint32_t started_ms = 0;
  static float before[3] = {0.0f, 0.0f, 0.0f};

  const uint32_t ms = millis();
  if (initial_run) {
    started_ms = ms;
    const light::LightColorValues &now = state.current_values;
    // In the same units as the frames below, which the set brightness scales later.
    const float level =
        now.is_on() ? std::min(now.get_brightness() / std::max(state.remote_values.get_brightness(), 0.001f), 1.0f)
                    : 0.0f;
    before[0] = now.get_red() * level;
    before[1] = now.get_green() * level;
    before[2] = now.get_blue() * level;
  }

  auto w = [ms](float hz, uint32_t seed) { return wobble(ms, hz, seed); };
  float r = 0.0f, g = 0.0f, b = 0.0f;
  switch (scene) {
    case Scene::CANDLE: {
      // A calm amber flame that sways slowly and sometimes gutters.
      const float sway = 0.88f + 0.12f * w(0.5f, 1);
      r = sway * (0.92f + 0.08f * w(1.3f, 2));
      g = sway * (0.30f + 0.10f * w(1.9f, 3));
      break;
    }
    case Scene::FIRE: {
      // Flames flare together while red and green flicker apart, so the yellow
      // and red shadows dance against each other.
      const float flare = 0.78f + 0.22f * w(0.7f, 11);
      r = flare * (0.85f + 0.15f * w(2.3f, 12));
      g = flare * (0.34f + 0.28f * w(3.1f, 13));
      break;
    }
    case Scene::ICY_FIRE: {
      // A cold blue flame with cyan licks and white tips when all three peak.
      const float flare = 0.78f + 0.22f * w(0.7f, 21);
      b = flare * (0.86f + 0.14f * w(2.0f, 22));
      g = flare * (0.40f + 0.30f * w(2.8f, 23));
      r = flare * (0.16f + 0.16f * w(3.4f, 24));
      break;
    }
    case Scene::PURPLE_FIRE: {
      // Magenta flames: red and blue flicker apart, so the shadows split violet and pink.
      const float flare = 0.78f + 0.22f * w(0.7f, 31);
      b = flare * (0.82f + 0.18f * w(2.1f, 32));
      r = flare * (0.60f + 0.30f * w(2.7f, 33));
      break;
    }
    case Scene::CHRISTMAS: {
      // Red and green trade places every few seconds, with an occasional soft white sparkle.
      const float swap = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * (ms % 9000u) / 9000.0f);
      const float sparkle = std::clamp((w(0.35f, 41) - 0.55f) * 2.2f, 0.0f, 1.0f);
      r = 0.12f + 0.88f * swap;
      g = 0.12f + 0.88f * (1.0f - swap);
      r += sparkle * (1.0f - r);
      g += sparkle * (1.0f - g);
      b = 0.9f * sparkle;
      break;
    }
    case Scene::AURORA: {
      // Slow green curtains over blue, with a faint violet edge now and then.
      g = 0.55f + 0.40f * w(0.12f, 51) + 0.05f * w(0.9f, 52);
      b = 0.40f + 0.35f * w(0.09f, 53);
      r = 0.08f + 0.20f * w(0.07f, 54);
      break;
    }
    case Scene::OCEAN: {
      // Deep blue with slow teal swells.
      b = 0.78f + 0.22f * w(0.15f, 61);
      g = 0.32f + 0.28f * w(0.22f, 62);
      r = 0.02f + 0.02f * w(0.3f, 63);
      break;
    }
    case Scene::SUNSET: {
      // Warm red sky drifting between orange and pink.
      r = 0.90f + 0.10f * w(0.05f, 71);
      g = 0.28f + 0.20f * w(0.07f, 72);
      b = 0.14f + 0.14f * w(0.045f, 73);
      break;
    }
    case Scene::LAVA: {
      // Dark red that slowly surges brighter, glowing orange at the peaks.
      const float surge = 0.5f + 0.5f * w(0.08f, 81);
      r = 0.35f + 0.65f * surge;
      g = 0.30f * surge * surge * (0.8f + 0.2f * w(0.3f, 82));
      break;
    }
    case Scene::DRIFT: {
      // Every LED wanders on its own over its whole range, so the three shadows
      // mix into ever changing colours.
      r = 0.5f + 0.5f * w(0.06f, 91);
      g = 0.5f + 0.5f * w(0.075f, 92);
      b = 0.5f + 0.5f * w(0.09f, 93);
      break;
    }
  }

  float rgb[3] = {std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f)};
  const uint32_t since = ms - started_ms;
  if (since < FADE_IN_MS) {
    float t = static_cast<float>(since) / FADE_IN_MS;
    t = t * t * (3.0f - 2.0f * t);
    for (int i = 0; i < 3; i++)
      rgb[i] = before[i] + (rgb[i] - before[i]) * t;
  }

  // The brightness set in Home Assistant scales the whole effect.
  const float level = std::max({rgb[0], rgb[1], rgb[2], 0.001f});
  auto call = state.make_call();
  call.set_publish(false);
  call.set_save(false);
  call.set_transition_length_if_supported(0);
  call.set_state(true);
  call.set_rgb(rgb[0] / level, rgb[1] / level, rgb[2] / level);
  call.set_brightness(level * state.remote_values.get_brightness());
  call.perform();
}

}  // namespace esphome::colorshadow
