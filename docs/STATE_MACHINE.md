# State machine

States have stable ids, names, and durations. Transitions contain source/target,
parameter comparison, priority, blend duration, optional exit phase and sync marker,
and an explicit interruption flag.

When several transitions qualify, higher priority wins and lower target StateId is
the tie-break. Zero-duration transitions complete immediately. Nonzero transitions
retain clear source/target ownership and expose blend alpha. Exit phase is evaluated
against the source state's local loop phase.

An interruptible active transition treats its target as the source of the new
transition. Non-interruptible transitions finish normally. The event policy emits
`exit:<source>` and `enter:<target>` once when a transition is selected; subsequent
crossfade frames do not repeat them. Graph evaluation stable-deduplicates identical
clip occurrences by propagating active/crossfade pose branches; occurrence records
retain absolute Tick, cycle, source node, and clip. Configured sync markers adjust
the exact compile-bound target ClipPlayer before that frame is sampled. The selected
transition index is carried by the state update, local marker time is lifted to a
monotonic absolute clock, and the synchronization jump is a discontinuity that does
not synthesize skipped Event or Root Motion intervals.
