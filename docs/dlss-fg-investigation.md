# DLSS FG capture investigation

The user confirmed radically improved live playback with the motion-scale fix,
with small residual artifacts. These observations are from
one 3840×2160 SDR pair on the user's RTX 5080 with DLSS-G 310.9.16.0; they are
not a general quality result or proof of a runtime defect.

- Native EchoStorm vectors explain the pair: sampled RGB error after backward
  warping is about 1.6/255, versus about 33 without compensation. Reversing or
  multiplying the field by 0.5 or 2 worsens the alignment. The earlier Fast sample
  saturated its ±16 pixel search while this pan needs about 117 pixels vertically.
- The raw NVIDIA output already contains double contours, before our output
  guard or presentation. The standalone captured-pair replay reproduces almost
  the same artifact (RGB MAE versus the live capture about 0.18/255), despite a
  fresh feature, different executable and short repeated-frame warm-up.
- All six lifetime/prepared variants returned an enabled interpolation and
  byte-identical PNGs. Retaining separate color and/or motion textures and
  toggling `motionVectorsDilated` did not change this pair's output. This does not
  establish whether the runtime uses the motion input correctly.
- The pinned SDK explicitly numbers generated frames from 1 through N and
  defines N as the number of additional frames. The existing indexing matches.

The physical-GPU v2 result identified the motion-scale mismatch in this direct
NGX path. The historical `1/width,1/height` baseline has midpoint diagnostic error
8.77799/255. Zero motion is nearly identical (RGB MAE 0.189394 from baseline),
as is reversed motion (0.232754). With the same captured field and scale `1,1`,
the midpoint error falls to 1.76973/255, and visual inspection shows a substantial
reduction of the double shelf contours. HUD-less, transparent UI and video
metadata each produce byte-identical baseline output. None of those metadata
experiments is adopted as a quality fix.

The live DLSS FG wrapper therefore now uses pixel displacement with unit scale.
This is an observed direct-NGX integration correction on DLSS-G 310.9.16.0,
not a claim that Streamline's documented common-constant convention is wrong.
FSR, XeSS and upscaler conventions are unchanged. Live playback, other resolutions,
HDR and additional MFG fractions still need hardware testing. Some residual
occlusion/edge defects are visible even in the improved sample; we lack game
depth, camera pose and a true reference intermediate frame.

Replay v2 preserves its historical normalized `baseline` for reproducibility;
`pixel_scale_control` now corresponds to the live wrapper's corrected scale.
Reversed motion remains a deliberately wrong sensitivity control.

## Residual SDR impulses

An offline pass over the improved replay identifies sparse color impulses.
A broad motion-warped endpoint replacement was rejected after visual inspection:
it creates new patches near the upper frame boundary. The adopted guard instead
changes only an isolated generated-pixel outlier when at least six of eight
surrounding samples agree, both warped endpoints confirm their color, and local
motion is coherent. It uses each actual MFG fraction, checks both source bounds,
and preserves the SDK alpha. On this single replay the CPU model changes 2,336
of 8,294,400 pixels (about 0.028%); this is a coverage count, not a quality score.

The pass was initially SDR-only. It now supports HDR10 with PQ decoding,
linear BT.2020 reconstruction and display-referred comparisons. Relative light
agreement is also required to avoid treating compressed highlights as equal.
It remains DLSS-only, controlled by the existing saved guard
toggle, now labeled "protect interpolation". It is integrated into the existing
post-FG shader with no extra full-frame GPU dispatch. Sparse impulse correction
does not repair all disocclusions or larger edge defects. Physical-GPU temporal
quality and GPU cost still require live testing. WARP regressions exercise moving
2x2 defects at all three 4x fractions, genuine thin moving detail, inconsistent
motion, disagreeing endpoints, odd sizes, bypass, unchanged legacy output and
HDR10 colors at 20, 203, 1000 and 4000 nits, 10000-nit real details,
alpha preservation and bit-exact preservation of untouched HDR pixels.
The user confirmed the SDR impulse guard in live playback; the new HDR
extension is validated with WARP, separately from physical HDR testing.

Private captures are not committed. The source SDK definitions are in the pinned
NGX dependencies; normalization and scene/UI recommendations are also documented
in NVIDIA's [DLSS-G guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md).
