# Validation overview

The executed source implementation was frozen before publication. The original working directories, failures and raw logs remain in the author's experiment archive. This repository provides de-identified timing data and explicit result qualifications in [validation/README.md](../validation/README.md).

| Layer | Executed evidence |
|---|---|
| Pure C++ | 32 core and20 diagnostic cases passed |
| ROS | 8 existing and5 companion-related test methods passed |
| File/pair store | 9 cases passed on Windows and Linux; these are the same9 cases, not18 distinct tests |
| Target ARM | Build and the corresponding tests passed with existing dependencies |
| Companion real use | Two launches,30s then15s,3 fresh paired snapshots; exact stamp/content/checksum and subscription release passed |
| Long real capture | 3600s,71822 complete groups,19.9506FPS, no measurement-period read faults/incomplete groups,1.57s natural exit |
| Rate qualification | 20 low-rate diagnostic samples; strict no-WARN long-run criterion failed; minimum5s16.8FPS |
| Comparable case | Same GREY and20/681/100;10min each; legacyGREY adapter19.927FPS, current package19.972FPS; both natural cleanup |
| Offline statistical audit | Frozen C++8 numeric replay scenarios and12600 snapshots agreed with independent counts; ideal20Hz-hour had0WARN |

The old same-format delivered stream also contained short rate dips. Its original PASS checks did not include an equivalent native short-window diagnostic. Old/new labels therefore cannot be used as a stability ranking. Raw-driver sequence, sensor loss counts and exposure timestamps were unavailable.

Startup attempts with Y16 input are retained as not comparable. A legacy pilot overlapped sealed-log retrieval and was excluded from the final resource baseline; a clean repeat reused the identical GREY-only legacy binary. Both baseline modifications and comparison limits are documented in the public evidence summary.

CPU/RSS are deployment-cost observations: the legacy build had empty CXX_FLAGS and the tested current build used `-O2 -g -DNDEBUG`; the current package also publishes more interfaces and runs a metadata companion. No attribution to the refactor alone or small-sample significance claim is justified.
