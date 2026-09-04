# AnimGraphLab

AnimGraphLab is a from-scratch Modern C++23 laboratory for an engine-independent,
data-oriented skeletal-animation runtime. The repository starts from a verified
construction baseline; capabilities are documented here only after executable
verification exists.

```text
Raw Clips + Skeleton
        ↓
Versioned Asset Compiler
        ↓
Compiled Animation Graph
        ↓
Blend / State / Layer / Root Motion / IK
        ↓
Batch Pose Evaluation
        ↓
Compression Metrics + Interactive Debugger
```

The project is AI-assisted engineering. The user specified the product direction,
scope, constraints, and acceptance bar; Codex GPT-5.6 Sol is responsible for the
implementation and evidence in this delivery cycle.

## Verified baseline

- MQB 5.4.0 discovers MSVC and builds/runs the CLI and dependency-free tests.
- CMake/Ninja builds the same source manifest with MSVC and runs CTest.
- GitHub CI exercises Windows MSVC plus Ubuntu Clang and GCC.

The runtime feature work is tracked under
`tasks/20260904-212459-animgraph-runtime-0.1/`.

## License

MIT. See `LICENSE`.
