# Full CI and sanitizer GREEN evidence

- Workflow run: `33888003456`
- URL: `https://github.com/Iviesever/animgraph-lab-cpp/actions/runs/33888003456`
- Commit: `0a549eb42e345ea8a477181b8de6dbf5f69446a3`
- Windows/MSVC: success in 1 minute 5 seconds.
- Ubuntu/Clang: success in 39 seconds.
- Ubuntu/GCC: success in 30 seconds.
- Ubuntu/Clang ASan+UBSan: success in 1 minute 12 seconds.

The sanitizer job configured and built with AddressSanitizer and
UndefinedBehaviorSanitizer, then passed all three CTest targets: 46 unit/integration
tests, 10,000 valid + 10,000 invalid property cases with 1,000 batch cases, and
100,000 three-format fuzz inputs. Leak detection and halt-on-error were enabled.
