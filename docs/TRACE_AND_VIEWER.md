# Trace and viewer

`generate_demo_trace` evaluates the real compiled graph and records each frame's
local/model poses, current node/state/transition, clip clocks, blend weights, cache
hits/misses, events, available sync markers, root delta/accumulation, IK target/error,
compression summary, evaluation microseconds, versions, Git SHA, and success. Node,
blend weights, state/transition, crossed markers, Root accumulator, and IK data are
produced by `evaluate` and copied by Trace rather than reconstructed there.
Timing and machine observations never enter asset or graph identity. Allocation
count is `null` because no reliable allocator hook is claimed.

`generate_viewer_html` embeds Base64-encoded JSON into one file with CSS and
JavaScript—no
CDN, Node runtime, or server. It draws Skeleton and Root Motion canvases and exposes
play/pause, step, timeline scrub, hierarchy-correct local/model switch,
state/transition, blend/cache, occurrence events, crossed markers, Runtime-executed
Graph node highlighting, Foot/Hand IK target/pole overlays, compression bytes/error
canvas, and a narrow breakpoint. Dynamic text is escaped before DOM insertion.

The committed harness was prevented from loading local `file://` URLs by browser
security policy and explicitly prohibited workarounds. Static tests verify controls,
embedded real Trace, responsive CSS, and absence of network references. Interactive
browser console and screenshot are therefore recorded as unverified, not passed.
