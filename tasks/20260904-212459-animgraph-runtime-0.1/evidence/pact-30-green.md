# PACT-30 local GREEN evidence

- MQB/MSVC: 22/22 tests passed.
- CMake/MSVC: 6 incremental steps; CTest 1/1 passed in 0.09 seconds.
- Source drift: 7 production sources match.
- Oracle: 129 uniform plus 128 deterministic random times per clip; position,
  quaternion angle, and scale errors stayed within configured thresholds.

Raw test output (`name raw_keys compressed_keys raw_bytes compressed_bytes
max_translation max_rotation_radians max_scale`):

```text
static          33   3    704    64  0          0           0
walk           147  29   3136   600  0.0174266  0.000690534 0.0075469
rapid_rotation 147  29   3136   684  0          0.000976562 0.00999999
tiny_motion    147   4   3136    84  0.0001     0.00629107  0.0000100136
long           771  54  16448  1136  0.0172569  0.000976562 0.00987756
nonuniform      18  13    384   272  0.000562219 0.000690534 0.0000562668
```

The algorithm is deterministic and explainable; it does not claim globally optimal
compression and does not use entropy coding or unmeasured quantization.
