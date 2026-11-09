# ArduStereo0.2.2

Initial public research release for the validated B0266/Jetson/Noetic combination. The existing tested acquisition implementation, controls, launch/YAML and product tests are unchanged. Public documentation, citation metadata and de-identified numeric validation evidence were prepared after the implementation freeze.

Normal operation remains one launch with raw20/681/100 defaults and explicitGREY. Standard stereo messages, optional composite, runtime diagnostics, automatic metadata records and exact paired snapshots are provided. Actual long-run rate variation and all material comparison limitations are documented; there is no claim of perfect20Hz, sensor-level zero loss or broad Jetson compatibility.

The60-minute test completed and cleaned up, while its strict no-WARN condition failed. An offline audit confirmed that the warnings reflected observed host-side cadence and that legacy output also varied. See validation/README.md and the supplied numeric evidence. No platform upgrade or driver reinstall was used.
