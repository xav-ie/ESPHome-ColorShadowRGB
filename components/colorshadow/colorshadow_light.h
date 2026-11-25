#pragma once

#include <algorithm>
#include <cmath>
#include <memory>

#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/transformers.h"
#include "esphome/components/output/float_output.h"

namespace esphome::colorshadow {

/// Hue in sextants [0, 6), saturation and value in [0, 1].
struct Hsv {
  float h, s, v;
};

inline Hsv to_hsv(float r, float g, float b) {
  const float v = std::max({r, g, b});
  const float d = v - std::min({r, g, b});
  Hsv out{0.0f, v > 0.0f ? d / v : 0.0f, v};
  if (d <= 0.0f)
    return out;
  if (v == r) {
    out.h = std::fmod((g - b) / d + 6.0f, 6.0f);
  } else if (v == g) {
    out.h = (b - r) / d + 2.0f;
  } else {
    out.h = (r - g) / d + 4.0f;
  }
  return out;
}

inline void from_hsv(Hsv c, float &r, float &g, float &b) {
  float h = std::fmod(c.h, 6.0f);
  if (h < 0.0f)
    h += 6.0f;
  const int sextant = static_cast<int>(h) % 6;
  const float f = h - std::floor(h);
  const float p = c.v * (1.0f - c.s);
  const float q = c.v * (1.0f - c.s * f);
  const float t = c.v * (1.0f - c.s * (1.0f - f));
  switch (sextant) {
    case 0: r = c.v, g = t, b = p; break;
    case 1: r = q, g = c.v, b = p; break;
    case 2: r = p, g = c.v, b = t; break;
    case 3: r = p, g = q, b = c.v; break;
    case 4: r = t, g = p, b = c.v; break;
    default: r = c.v, g = p, b = q; break;
  }
}

/// ESPHome's default transition but with the colour moved through hue instead of
/// straight across RGB. The colour keeps its brightest channel at full, so a change
/// such as blue to orange swings round the hue circle the short way, bright and
/// saturated, instead of dipping through a dim grey. Brightness, on and off fades,
/// the eased progress and the length all stay as ESPHome does them.
class HueTransition : public light::LightTransitionTransformer {
 public:
  optional<light::LightColorValues> apply() override {
    optional<light::LightColorValues> out = light::LightTransitionTransformer::apply();
    if (!out.has_value() || this->changing_color_mode_)
      return out;

    const float p = light::LightTransformer::smoothed_progress(this->get_progress_());
    const light::LightColorValues &from = this->start_values_;
    const light::LightColorValues &to = this->end_values_;
    Hsv a = to_hsv(from.get_red(), from.get_green(), from.get_blue());
    Hsv z = to_hsv(to.get_red(), to.get_green(), to.get_blue());
    // White and grey have no hue of their own; borrow the other end's, so only the
    // saturation changes.
    if (a.s < 0.01f)
      a.h = z.h;
    if (z.s < 0.01f)
      z.h = a.h;
    float dh = z.h - a.h;
    if (dh > 3.0f)
      dh -= 6.0f;
    else if (dh < -3.0f)
      dh += 6.0f;

    float r, g, b;
    from_hsv({a.h + dh * p, a.s + (z.s - a.s) * p, a.v + (z.v - a.v) * p}, r, g, b);
    out->set_red(r);
    out->set_green(g);
    out->set_blue(b);
    return out;
  }
};

/// The rgb platform's output, with HueTransition as the light's transition.
class ColorShadowLightOutput : public light::LightOutput {
 public:
  void set_red(output::FloatOutput *red) { this->red_ = red; }
  void set_green(output::FloatOutput *green) { this->green_ = green; }
  void set_blue(output::FloatOutput *blue) { this->blue_ = blue; }

  light::LightTraits get_traits() override {
    light::LightTraits traits;
    traits.set_supported_color_modes({light::ColorMode::RGB});
    return traits;
  }

  std::unique_ptr<light::LightTransformer> create_default_transition() override {
    return std::make_unique<HueTransition>();
  }

  void write_state(light::LightState *state) override {
    float red, green, blue;
    state->current_values_as_rgb(&red, &green, &blue);
    this->red_->set_level(red);
    this->green_->set_level(green);
    this->blue_->set_level(blue);
  }

 protected:
  output::FloatOutput *red_;
  output::FloatOutput *green_;
  output::FloatOutput *blue_;
};

}  // namespace esphome::colorshadow
