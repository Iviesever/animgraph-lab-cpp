# Trace and viewer

`generate_demo_trace` evaluates the real compiled graph and records each frame's
local/model poses, current node/state/transition, clip clocks, blend weights, cache
hits/misses, events, available sync markers, root delta/accumulation, IK target/error,
compression summary, evaluation microseconds, versions, Git SHA, and success. Node,
blend weights, state/transition, crossed markers, Root accumulator, and IK data are
produced by `evaluate` and copied by Trace rather than reconstructed there.
Timing and machine observations never enter asset or graph identity. Allocation
count is `null` because no reliable allocator hook is claimed.

`generate_viewer_html` embeds that JSON into one file with CSS and JavaScript—no
CDN, Node runtime, or server. It draws Skeleton and Root Motion canvases and exposes
play/pause, step, timeline scrub, local/model switch, state/transition, blend/cache,
events, markers, Graph node highlighting, IK, compression, and a narrow breakpoint.

The committed harness was prevented from loading local `file://` URLs by browser
security policy and explicitly prohibited workarounds. Static tests verify controls,
embedded real Trace, responsive CSS, and absence of network references. Interactive
browser console and screenshot are therefore recorded as unverified, not passed.
