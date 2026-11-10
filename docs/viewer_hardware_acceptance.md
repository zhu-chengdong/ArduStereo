# Scoped hardware acceptance and future replication

Status as of2026-11-10: the exact candidate implementation has author-run ARM and real-camera evidence on the documented rig. See [numeric results](../validation/README.md). This replaces the old documentation status that the GUI had only offline tests; the frozen UI development banner itself is retained.

For replication on an already configured compatible rig, use this order:

1. Read-only identity, video format and control query; preserve outputs. Stop if the rig differs or required interfaces are absent.
2. Build in a separate workspace with existing dependencies. Do not upgrade or run vendor installation scripts as part of acceptance.
3. Start the viewer without capture, check configuration, then explicitly start.
4. Check packed GREY2560×800 and both1280×800mono8 outputs, readback20/681/100, common message associations and actual rates.
5. In a fixed scene, change a single supported control and restore it; assess image response as well as readback. Keep raw images/logs locally.
6. Save a paired snapshot and new YAML; verify image sizes, association, checksums and retained inputs.
7. Record the agreed continuous interval with a monotonic clock and resource samples. Define criteria before running, preserve every low-rate warning and keep shutdown boundaries separate.
8. Stop naturally, verify resources/owned processes are released, restart and recheck baseline. If required, use an external recorder and replay after capture is stopped.

Log sampling rules and pass/fail criteria should be stated before each new run. The published hour used mean19–21FPS, no gap>2s and no measurement-window read/format faults; low-rate warnings were reported separately. Those scoped criteria are not a guarantee of exact20Hz. Save both failed and successful attempts with distinct identities; never overwrite the old test folder or relabel model checks as hardware tests.
