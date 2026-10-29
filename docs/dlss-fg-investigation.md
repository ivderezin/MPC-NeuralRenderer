# DLSS FG capture investigation

Live playback with the motion-scale fix still needs confirmation. These observations are from
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

Private captures are not committed. The source SDK definitions are in the pinned
NGX dependencies; normalization and scene/UI recommendations are also documented
in NVIDIA's [DLSS-G guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md).
