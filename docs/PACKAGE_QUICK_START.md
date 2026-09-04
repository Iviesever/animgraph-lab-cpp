# Packaged Win64 quick start

From the extracted package directory:

```powershell
./bin/animgraph_lab.exe verify
./bin/animgraph_lab.exe evaluate --sample locomotion --trace trace.json --git-sha packaged
./bin/animgraph_lab.exe generate-viewer --trace trace.json --out viewer.html
./bin/animgraph_lab.exe benchmark --out benchmark.json
./bin/animc.exe inspect samples/assets/sample.agskel
```

Open `viewer.html` manually in a modern browser. The file is self-contained and
does not require a server, Node, or a CDN. The executable uses only data shipped in
the package or generated procedurally; it does not depend on the development tree.
