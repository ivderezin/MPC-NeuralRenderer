# DLSS FG capture investigation

The reported camera-pan artifacts remain unresolved. These observations are from
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

Replay v2 therefore probes sensitivity to zero/reversed/unnormalized motion,
optional HUD-less color and transparent UI inputs, and video metadata. Live
playback defaults remain unchanged. Deliberately incorrect controls must not be
adopted as production settings merely because a photometric error improves.
We lack game depth, camera pose and a true reference intermediate frame.

Private captures are not committed. The source SDK definitions are in the pinned
NGX dependencies; normalization and scene/UI recommendations are also documented
in NVIDIA's [DLSS-G guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md).
