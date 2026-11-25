#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "colorshadow_light.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

// Effects for a lamp whose red, green and blue LEDs each cast their own coloured
// shadow. Most drive the three LEDs on separate smooth noise, so the shadows move
// independently. Effects marked "base colour" take their colour from the light's
// colour in Home Assistant and retint live when it changes; the rest have a fixed
// palette. Home Assistant's brightness scales them all. Every value is a light
// level before gamma, at most 1, so the outputs' max_power cap bounds the peak.
namespace esphome::colorshadow {

enum class Scene {
  FIRE,
  CANDLE,
  BREATHE,
  TWINKLE,
  WAVES,
  STORM,
  SHADOW_DANCE,
  RAINBOW,
  PARTY,
  CHRISTMAS,
  AURORA,
  TV_GLOW,
  SUNRISE,
  SUNSET,
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

inline float smoothstep(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

/// Position `t` in [0, 1) round a loop, as an eased swell from 0 up to 1 and back.
inline float swell(float t) { return 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * t); }

/// Fraction of `period_ms` that has passed, wrapping round.
inline float cycle(uint32_t ms, uint32_t period_ms) {
  return static_cast<float>(ms % period_ms) / static_cast<float>(period_ms);
}

/// One channel of a flame tinted by `base` (that channel of the base colour) at
/// `heat` in [0, 1]. Cool embers raise the base channel to a higher power, which
/// pushes the weaker channels down, so they sink to a deeper, darker shade of the
/// base hue; the hottest moments add the missing channels back as white tips.
inline float flame(float base, float heat, float tip) {
  return heat * std::pow(base, 1.0f + 2.0f * (1.0f - heat)) + tip * (1.0f - base);
}

/// Colour stops for the sunrise and sunset ramps, from night to day.
struct Stop {
  float at, r, g, b, level;
};
static constexpr Stop DAWN[] = {
    {0.00f, 1.00f, 0.04f, 0.00f, 0.02f},
    {0.35f, 1.00f, 0.22f, 0.00f, 0.25f},
    {0.70f, 1.00f, 0.55f, 0.15f, 0.65f},
    {1.00f, 1.00f, 0.80f, 0.55f, 1.00f},
};

inline void dawn_at(float p, float rgb[3]) {
  const Stop *a = &DAWN[0], *z = &DAWN[0];
  for (const Stop &s : DAWN) {
    z = &s;
    if (s.at >= p)
      break;
    a = &s;
  }
  const float t = z->at > a->at ? smoothstep((p - a->at) / (z->at - a->at)) : 1.0f;
  const float level = a->level + (z->level - a->level) * t;
  rgb[0] = (a->r + (z->r - a->r) * t) * level;
  rgb[1] = (a->g + (z->g - a->g) * t) * level;
  rgb[2] = (a->b + (z->b - a->b) * t) * level;
}

/// Light the LEDs at one frame of `scene`. Call it from a lambda effect.
/// `ramp_s` is how long the sunrise and sunset ramps take.
inline void render(light::LightState &state, Scene scene, bool initial_run, float ramp_s = 1200.0f) {
  // Only one effect runs at a time, so one set of state serves them all.
  static constexpr uint32_t FADE_IN_MS = 800;
  static constexpr float RETINT_S = 0.6f;
  static uint32_t started_ms = 0, last_ms = 0;
  static float before[3] = {0.0f, 0.0f, 0.0f};
  static float base[3] = {1.0f, 1.0f, 1.0f};
  static float brightness = 1.0f;
  // Storm strikes and television cuts.
  static uint32_t next_event_ms = 0, event_ms = 0;
  static uint8_t flashes = 0;
  static uint32_t flash_ms = 0;
  static float flash_rgb[3] = {0.0f, 0.0f, 0.0f};
  static float scene_from[3] = {0.0f, 0.0f, 0.0f}, scene_to[3] = {0.0f, 0.0f, 0.0f};
  // Party hue jumps.
  static float hue_from = 0.0f, hue_to = 0.0f;

  const uint32_t ms = millis();
  const light::LightColorValues &remote = state.remote_values;
  const float target[3] = {remote.get_red(), remote.get_green(), remote.get_blue()};
  if (initial_run) {
    started_ms = ms;
    last_ms = ms;
    // The first lightning waits a few seconds; the first television scene is now.
    next_event_ms = ms + (scene == Scene::STORM ? 3000u : 0u);
    event_ms = ms;
    flashes = 0;
    brightness = remote.get_brightness();
    const light::LightColorValues &now = state.current_values;
    // In the same units as the frames below, which the set brightness scales later.
    const float level =
        now.is_on() ? std::min(now.get_brightness() / std::max(brightness, 0.001f), 1.0f) : 0.0f;
    before[0] = now.get_red() * level;
    before[1] = now.get_green() * level;
    before[2] = now.get_blue() * level;
    for (int i = 0; i < 3; i++) {
      base[i] = target[i];
      scene_from[i] = scene_to[i] = before[i];
    }
    hue_from = hue_to = 0.0f;
  }

  // A colour or brightness set while the effect runs lands in remote_values (the
  // effect keeps running, and its next frame cuts short the light's own
  // transition), so ease towards it here instead.
  const float dt = static_cast<float>(ms - last_ms) / 1000.0f;
  last_ms = ms;
  const float k = 1.0f - std::exp(-dt / RETINT_S);
  for (int i = 0; i < 3; i++)
    base[i] += (target[i] - base[i]) * k;
  brightness += (remote.get_brightness() - brightness) * k;

  const uint32_t elapsed = ms - started_ms;
  auto w = [ms](float hz, uint32_t seed) { return wobble(ms, hz, seed); };
  auto due = [ms](uint32_t at) { return static_cast<int32_t>(ms - at) >= 0; };
  float rgb[3] = {0.0f, 0.0f, 0.0f};
  switch (scene) {
    case Scene::FIRE: {
      // Base colour. The flame flares as a whole while each LED flickers on its
      // own, so the shadows dance against each other.
      const float flare = 0.72f + 0.28f * w(0.7f, 11);
      for (int i = 0; i < 3; i++) {
        const float heat = std::clamp(flare * (0.62f + 0.38f * w(2.2f + 0.6f * i, 12 + i)), 0.0f, 1.0f);
        const float tip = 0.4f * std::pow(std::clamp((heat - 0.8f) / 0.2f, 0.0f, 1.0f), 2.0f);
        rgb[i] = flame(base[i], heat, tip);
      }
      break;
    }
    case Scene::CANDLE: {
      // Base colour. A calm flame with a slow sway, a faint flicker and, now and
      // then, a gutter that dips it for a few seconds.
      const float gutter = smoothstep((w(0.06f, 21) - 0.55f) / 0.35f);
      const float sway = (0.9f + 0.1f * w(0.4f, 22)) * (1.0f - 0.45f * gutter);
      for (int i = 0; i < 3; i++) {
        const float heat = std::clamp(sway * (0.9f + 0.1f * w(1.1f + 0.3f * i, 23 + i)), 0.0f, 1.0f);
        rgb[i] = flame(base[i], heat, 0.0f);
      }
      break;
    }
    case Scene::BREATHE: {
      // Base colour. A slow eased breath, each LED a little behind the last, so
      // the shadows ripple as it swells.
      for (int i = 0; i < 3; i++)
        rgb[i] = base[i] * (0.18f + 0.82f * swell(cycle(ms + 6000u - 300u * i, 6000)));
      break;
    }
    case Scene::TWINKLE: {
      // Base colour. A soft glow where each LED now and then swells into a
      // sparkle on its own, the brightest ones glinting white.
      for (int i = 0; i < 3; i++) {
        const float s = std::pow(smoothstep((w(0.45f, 31 + i) - 0.25f) / 0.6f), 2.0f);
        rgb[i] = base[i] * (0.3f + 0.7f * s) + 0.45f * (1.0f - base[i]) * s * s * s;
      }
      break;
    }
    case Scene::WAVES: {
      // Base colour. Slow swells that drift a little either side of the base hue,
      // like light through water (blue makes an ocean).
      Hsv c = to_hsv(base[0], base[1], base[2]);
      c.h += 0.35f * w(0.05f, 41);
      c.s = std::clamp(c.s * (0.85f + 0.15f * w(0.08f, 42)), 0.0f, 1.0f);
      from_hsv(c, rgb[0], rgb[1], rgb[2]);
      for (int i = 0; i < 3; i++)
        rgb[i] *= 0.6f + 0.4f * w(0.13f + 0.04f * i, 43 + i);
      break;
    }
    case Scene::STORM: {
      // Base colour. A dim, rumbling sky in the base colour, struck by lightning
      // every few seconds: one to three white flashes, each LED at its own
      // strength so the shadows split.
      auto start_flash = [&]() {
        event_ms = ms;
        flash_ms = 140u + random_uint32() % 160u;
        for (float &f : flash_rgb)
          f = 0.6f + 0.4f * random_float();
      };
      if (flashes == 0 && due(next_event_ms)) {
        flashes = 1 + random_uint32() % 3;
        start_flash();
      }
      float flash = 0.0f;
      if (flashes > 0) {
        const uint32_t since = ms - event_ms;
        flash = std::exp(-static_cast<float>(since) / 70.0f);
        if (since > flash_ms) {
          if (--flashes > 0)
            start_flash();
          else
            next_event_ms = ms + 4000u + random_uint32() % 14000u;
        }
      }
      const float sky = 0.12f + 0.06f * w(0.2f, 51);
      for (int i = 0; i < 3; i++)
        rgb[i] = base[i] * sky + (1.0f - base[i] * sky) * flash * flash_rgb[i];
      break;
    }
    case Scene::SHADOW_DANCE: {
      // Fixed: all three. Each LED swells in turn, a third of a cycle apart, so
      // the red, green and blue shadows take turns to step forward.
      for (int i = 0; i < 3; i++) {
        const float s = swell(cycle(ms + 8000u - 2667u * i, 8000));
        rgb[i] = 0.12f + 0.88f * s * s;
      }
      break;
    }
    case Scene::RAINBOW: {
      // Fixed: every hue. A slow, fully saturated trip round the colour wheel,
      // once a minute.
      from_hsv({6.0f * cycle(ms, 60000), 1.0f, 1.0f}, rgb[0], rgb[1], rgb[2]);
      break;
    }
    case Scene::PARTY: {
      // Fixed: every hue. A new colour on each beat at 120 beats a minute, eased
      // across a quarter beat, with each beat's glow swelling and fading.
      static constexpr uint32_t BEAT_MS = 500;
      if (ms - event_ms >= BEAT_MS) {
        event_ms = ms;
        hue_from = hue_to;
        hue_to = std::fmod(hue_to + 1.5f + 3.0f * random_float(), 6.0f);
      }
      const float beat = static_cast<float>(ms - event_ms) / BEAT_MS;
      float dh = hue_to - hue_from;
      if (dh > 3.0f)
        dh -= 6.0f;
      else if (dh < -3.0f)
        dh += 6.0f;
      from_hsv({hue_from + dh * smoothstep(beat * 4.0f), 1.0f, 1.0f}, rgb[0], rgb[1], rgb[2]);
      const float glow = 0.55f + 0.45f * std::pow(std::sin(static_cast<float>(M_PI) * beat), 2.0f);
      for (float &c : rgb)
        c *= glow;
      break;
    }
    case Scene::CHRISTMAS: {
      // Fixed: red and green. They trade places every few seconds, with an
      // occasional soft white sparkle.
      const float s = swell(cycle(ms, 9000));
      const float sparkle = std::clamp((w(0.35f, 61) - 0.55f) * 2.2f, 0.0f, 1.0f);
      rgb[0] = 0.12f + 0.88f * s;
      rgb[1] = 0.12f + 0.88f * (1.0f - s);
      rgb[0] += sparkle * (1.0f - rgb[0]);
      rgb[1] += sparkle * (1.0f - rgb[1]);
      rgb[2] = 0.9f * sparkle;
      break;
    }
    case Scene::AURORA: {
      // Fixed: green, blue and violet. Slow green curtains over blue, with a
      // faint violet edge now and then.
      rgb[1] = 0.55f + 0.40f * w(0.12f, 71) + 0.05f * w(0.9f, 72);
      rgb[2] = 0.40f + 0.35f * w(0.09f, 73);
      rgb[0] = 0.08f + 0.20f * w(0.07f, 74);
      break;
    }
    case Scene::TV_GLOW: {
      // Fixed: a television's cool light, for presence. A new scene every one to
      // five seconds (a quick cut between tints and levels) under a fast flicker.
      if (due(next_event_ms)) {
        for (int i = 0; i < 3; i++)
          scene_from[i] = scene_to[i];
        Hsv c{3.2f + 1.6f * random_float(), 0.1f + 0.35f * random_float(), 0.35f + 0.65f * random_float()};
        if (random_float() < 0.25f)
          c.h = 0.3f + 0.6f * random_float();  // a warm scene now and then
        from_hsv(c, scene_to[0], scene_to[1], scene_to[2]);
        event_ms = ms;
        next_event_ms = ms + 1000u + random_uint32() % 4000u;
      }
      const float cut = smoothstep(static_cast<float>(ms - event_ms) / 120.0f);
      for (int i = 0; i < 3; i++)
        rgb[i] = (scene_from[i] + (scene_to[i] - scene_from[i]) * cut) * (0.92f + 0.08f * w(6.0f, 81 + i));
      break;
    }
    case Scene::SUNRISE:
      // Fixed: dawn. A slow ramp from a dim deep red through orange to warm
      // white over ramp_s, then held. A wake light.
      dawn_at(std::min(elapsed / 1000.0f / ramp_s, 1.0f), rgb);
      break;
    case Scene::SUNSET:
      // Fixed: dusk. The sunrise ramp backwards, from warm white down to a dim
      // deep red over ramp_s, then held there.
      dawn_at(1.0f - std::min(elapsed / 1000.0f / ramp_s, 1.0f), rgb);
      break;
  }

  for (float &c : rgb)
    c = std::clamp(c, 0.0f, 1.0f);
  if (elapsed < FADE_IN_MS) {
    const float t = smoothstep(static_cast<float>(elapsed) / FADE_IN_MS);
    for (int i = 0; i < 3; i++)
      rgb[i] = before[i] + (rgb[i] - before[i]) * t;
  }

  const float level = std::max({rgb[0], rgb[1], rgb[2], 0.001f});
  auto call = state.make_call();
  call.set_publish(false);
  call.set_save(false);
  call.set_transition_length_if_supported(0);
  call.set_state(true);
  call.set_rgb(rgb[0] / level, rgb[1] / level, rgb[2] / level);
  call.set_brightness(level * brightness);
  call.perform();
}

}  // namespace esphome::colorshadow
