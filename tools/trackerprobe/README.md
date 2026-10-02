# trackerprobe

What a Windows.UI.Composition InteractionTracker does, in a desktop session that draws frames
(`scripts/windesktop-launch.ps1` on Windows, a display with a window manager under Wine).  Office's scrolling layer
(AirSpace's WinComp back end) moves its content with one.

- `probe_tracker.c`: what the tracker answers there and then — request ids, results, what it takes as an animation
  of its position and scale, what StartAnimation on its own properties is told.  `results/probe_tracker.win.txt`.
- `clamp.c`: a position asked for without clamping beyond the bounds, bounds that move while it is idle, and Office's
  case — bounds that expressions compute from a visual whose size a key frame animation grows, with the position asked
  for where the bounds end up.  `results/clamp.win.txt` (batch 17): the position is clamped at once, to the bounds as
  they are (300 asked for in 0..100 is 100, and stays 100 when the bound moves to 200); an idle tracker is pushed by a
  bound that moves past it and reports it with the last request's id; in Office's case the position follows the growing
  bounds frame by frame and ends at the lower one, 899 of 899..900 — clamping disabled or not.  No state callbacks: a
  position update of an idle tracker is a ValuesChanged and nothing else.
- `inertia.c`: how it coasts, frame by frame, with every value change and the milliseconds since the request.
  `results/inertia.win.txt` (batch 17); `results/inertia.wine.txt` is altars-up `8c771a2ab5c`, which follows it
  (before, Wine kept custom animations within the bounds, stretched the curve to end at a bound and coasted on to
  v / k, and reported no modified resting position).  `results/clamp.wine.txt` is the same commit: the same states,
  ids and final positions as Windows, frame for frame in number.
- `requestid.c`: which setters and methods take a request id (the next request's id less the one before, less one).
  Needs the desktop session too (the compositor is access denied outside it).  `results/requestid.win.txt` (batch 24):
  the scale bounds, both decay rates, inertia modifiers and position adjustments do; position bounds and center point
  modifiers do not.  `results/requestid.wine.txt` is the same since `8c771a2ab5c`.

What `inertia.win.txt` says, Windows 11 build 29671 on a 60.03 Hz panel:

- Inertia steps once a frame: after n frames the position is `v / k * (1 - exp(-k * n / f))`, k = -ln(1 - decay rate),
  f the refresh rate (fits all of v = 100..3000 and decay 0.5, 0.95, 0.99 to within a thousandth of a pixel).
- It stops when the velocity falls below 30 px/s: the natural resting position is `(v - 30) / k` from the start, the
  last frame jumps there, idle comes the frame after.  Scale coasts the same way, linearly, stopping below 0.05 (5 %/s),
  and its velocity is capped at 5.
- InertiaStateEntered comes on the next frame; its modified resting position is always there, the natural one clamped
  into the bounds when nothing modifies it.  Velocity added while it coasts adds to the velocity it has then.
- A bound in the way is overshot: the curve runs past it, then a damped spring (about ζ 0.9, ω 15 rad/s) brings it back,
  ending on the bound.
- A custom animation is not kept within the bounds: it runs to 5000 past a maximum of 1000, then the tracker enters
  inertia for the same request, with no velocity, natural resting position 5000 and modified resting position (0,0,0),
  and springs back to the bound the same way.
- A key frame inserted with no easing function eases along the cubic bezier through (0.41, 0.52) and (0, 0.94): the
  progress of each frame, inverted through that curve, is a frame time 17 ms after the last, exactly.

Build (with the tree's headers and import libraries, `probe_tracker.res` for the manifest):
`scripts/build-probe.sh tools/trackerprobe/inertia.c tools/trackerprobe/inertia.exe combase user32 tools/trackerprobe/probe_tracker.res`
(WINE_BUILD pointing at wine-src-up's build tree).
