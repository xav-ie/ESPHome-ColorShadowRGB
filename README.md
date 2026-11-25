# ESPHome ColorShadowRGB
[ESPHome](https://esphome.io/) config for the RCTESTFLIGHT Color Shadow LED Lamp (ESP32-C3), so it can be controlled by [Home Assistant](https://www.home-assistant.io/).

RCTESTFLIGHT product page: https://www.rctestflight.com/store/p/color-shadow-lamp

Youtube: https://youtu.be/R5_rlyRI9C4?t=1037

## Files
- `package.yaml`: everything about the lamp (pins, knobs, button, light, effects). Include it as an [ESPHome package](https://esphome.io/components/packages/).
- `components/colorshadow`: a light platform (the rgb platform with hue transitions) and the effects, in C++. `package.yaml` pulls it in as an external component from this repo at `colorshadowrgb_ref`.
- `colorshadowrgb.yaml`: an example device config. It holds only the name, the `!secret` values for api, ota and wifi, and the package include. Copy it, add a `secrets.yaml`, and pin `colorshadowrgb_ref` to a commit for repeatable builds. The one substitution pins both the YAML and the C++:

```yaml
substitutions:
  colorshadowrgb_ref: <commit sha>

packages:
  colorshadowrgb:
    url: https://github.com/xav-ie/ESPHome-ColorShadowRGB
    ref: ${colorshadowrgb_ref}
    files: [package.yaml]
```

## Physical controls
- Button press toggles the lamp on and off.
- Holding the button for 0.8 s steps to the next effect while still held, in the order of the effects list below, then back to none. From off it turns on with the first effect, Fire.
- Each knob sets the absolute level of its LED, from off at the zero end to full at the other.

## How the knobs work
- Each knob is sampled every 40 ms (4 ADC samples averaged), then a median of the last 5 readings removes spikes, then the voltage is mapped onto 0 to 1 between `knob_raw_zero` and `knob_raw_full`. Anything at or below `knob_raw_zero` is exactly 0.
- A knob at rest does nothing. It only takes over the light once it moves by `knob_turn_threshold` within `knob_settle_ms`; slower drift and ADC noise are ignored. While turning, it follows changes of `knob_follow_step` or more, and the turn ends after `knob_settle_ms` without movement.
- A turn writes all three knob positions at once: the brightest knob becomes the light's brightness and the colour is each knob divided by it, so every LED shows its knob's absolute level. All three at zero turns the lamp off; turning any knob up from off turns it on.
- Knob writes use no transition, so they track the knob without fading. Commands from Home Assistant keep their own transition (or the 1.5 s default).
- The last deliberate input wins. A colour set from Home Assistant or the button stays until a knob is turned again, even mid turn, and the next turn applies all three knob positions. A turn stops a running effect, since the effect would otherwise override the knobs.
- At boot the light restores its last state (off by default); the knobs wait to be turned.

## Colour transitions
ESPHome's rgb light fades by moving each channel in a straight line, so blue to orange dips through a dim, muddy grey halfway. The `colorshadow` light platform keeps ESPHome's transition (the same eased curve, length, brightness and on and off fades) but moves the colour round the hue circle the short way instead, with saturation and the brightest channel interpolated alongside. Blue to orange now swings through violet, magenta and red, bright and saturated all the way.

Hue rather than OKLab or OKLCH, because ESPHome already stores a colour as channels scaled so the brightest is 100% with brightness kept apart, which is HSV's value, so holding it keeps at least one LED at full throughout. The three LEDs' primaries are unknown and not sRGB, so a perceptual space would be modelling the wrong gamut and would still need gamut clipping on the way out. Gamma 2.8 is applied after the transition, as before.

- A transition of 0 is an instant cut, as before.
- An explicit transition length from Home Assistant is used as given.
- Without one, the light fades over 1.5 s (`default_transition_length`).

## Effects
In Home Assistant's effect list and the button's long press cycle, in this order. They drive red, green and blue on their own smooth noise or curves, so each LED's shadow moves on its own, and fade in over 0.8 s from what the lamp showed. Home Assistant's brightness scales them all, and the outputs' power cap bounds their peak.

Effects marked *base colour* take their colour from the light's colour in Home Assistant. Changing the colour while one runs retints it live over about half a second without stopping it: ESPHome keeps an effect running through a colour change and stores the new colour in the light's remote values, which the effect follows. The others have a fixed palette.

- Fire (*base colour*): the flame flares as a whole while each LED flickers on its own, cooling to deeper, darker embers of the base hue and peaking in white tips. Orange or amber is a wood fire, blue or white an icy fire, purple a purple fire.
- Candle (*base colour*): the same flame, calm: a slow sway, a faint flicker and now and then a gutter that dips it for a few seconds.
- Pulse (*base colour*): a slow eased breath every 6 s from near off up to the current colour at the current brightness, never above it, each LED a little behind the last so the shadows ripple.
- Twinkle (*base colour*): a soft glow where each LED swells into its own sparkle now and then, the brightest glinting white.
- Waves (*base colour*): slow swells drifting a little either side of the base hue; blue makes an ocean.
- Storm (*base colour*): a dim rumbling sky in the base colour, struck every 4 to 18 s by one to three lightning flashes, each LED at its own strength so the shadows split.
- Shadow Dance: red, green and blue swell in turn, a third of an 8 s cycle apart, so each shadow steps forward in turn.
- Rainbow: a slow, fully saturated trip round the colour wheel once a minute.
- Party: a new colour on every beat at 120 beats a minute, eased over a quarter beat, with each beat's glow swelling and fading.
- Christmas: red and green trade places every 9 s, with an occasional soft white sparkle.
- Aurora: slow green curtains over blue with a faint violet edge.
- TV Glow: a television's cool, flickering light with a quick cut to a new tint and level every 1 to 5 s, for presence simulation.
- Sunrise: a wake light ramping from a dim deep red through orange to warm white over `ramp_minutes` (20), then holding.
- Sunset: the same ramp backwards, down to a dim deep red, then holding there.

## Pins and outputs
The LED pins below are verified on a lamp (pure red, green and blue each showed their own colour). All pins are substitutions at the top of `package.yaml`, so a lamp wired differently is a one line override in the device config:

```yaml
substitutions:
  red_pin: GPIO7
  green_pin: GPIO6
  blue_pin: GPIO5
```

`red_knob_pin`, `green_knob_pin`, `blue_knob_pin` and `button_pin` work the same way.

Each LED output is a 1220 Hz LEDC channel with:
- `max_power: 60%` (`led_max_power`): this firmware has no thermal shutdown (stock firmware stops at 63°C and defaults to 50%) and the base is the only heatsink.
- `min_power` (`led_min_power`) with `zero_means_zero`: any level above zero starts at the LED's turn on duty, so the low end of a knob or a dim colour still lights, while zero is fully off.

## Fixed
- A single press turned the lamp on and also cycled the effect, and a double press toggled. Now a press toggles and a long press cycles effects.
- The effect cycle kept its own counter, which went out of step with effects set from Home Assistant or restored at boot. It now steps from the running effect.
- The knobs set percentages: the light renormalized the brightest channel to 100%, so a knob could not dim its LED. They now set absolute levels.
- A knob at zero still sent 12 to 15% and never turned its LED off. Zero is now off; the LED turn on threshold is handled at the output.
- ADC noise could write over a colour just set from Home Assistant, and every boot overwrote the restored colour with the knob positions. With every knob at zero, one noise reading on blue after Home Assistant set pure blue wrote the green and blue floors, which normalized to a green cyan. Knobs now only act on a deliberate turn.
- Every knob step started a fresh 1 s fade, so turning stuttered. Knob writes are now instant.
- Effects kept overriding knob writes. A turn now stops the effect.

## Todo
- Support the original direct and mixed modes.

Feel free to submit issues or pull requests.
