mil1553/
├── docs/
│   ├── PSAC.md
│   ├── SDP.md
│   ├── SVP.md
│   ├── SCMP.md
│   ├── SQAP.md
│   ├── SAS.md
│   ├── objectives_matrix.md
│   └── mil1553_notes.md
├── requirements/
│   ├── HLR.md
│   ├── LLR.md
│   └── requirements.csv
├── design/
│   ├── SDD.md
│   ├── architecture.puml
│   ├── state_machine_bc.puml
│   ├── state_machine_rt.puml
│   ├── state_machine_bit.puml
│   ├── message_sequence_bc_rt.puml
│   ├── message_sequence_rt_rt.puml
│   ├── mode_code_table.md
│   ├── dataflow.dot
│   ├── bus_transport_contract.md      # NEW — the protocol the two binaries speak over
│   └── timing_budget.md
├── traceability/
│   ├── hlr_to_llr.csv
│   ├── llr_to_code.csv
│   ├── code_to_test.csv
│   ├── test_to_hlr.csv
│   └── trace_graph.dot
├── include/
│   ├── c/
│   │   ├── mil1553_types.h
│   │   ├── mil1553_word.h
│   │   ├── manchester.h
│   │   ├── crc_parity.h
│   │   ├── bc_scheduler.h
│   │   ├── rt_service.h
│   │   ├── bm_monitor.h
│   │   ├── bus_model.h
│   │   ├── bit_pbit.h
│   │   ├── bit_cbit.h
│   │   └── bit_ibit.h
│   │   # error_inject.h intentionally NOT here — lives with its own lib, see src/c/errinject/
│   └── ada/
│       ├── mil1553_types.ads
│       ├── mil1553_word.ads
│       ├── manchester.ads
│       ├── crc_parity.ads
│       ├── bc_scheduler.ads
│       ├── rt_service.ads
│       ├── bm_monitor.ads
│       ├── bus_model.ads
│       ├── bit_pbit.ads
│       ├── bit_cbit.ads
│       └── bit_ibit.ads
│       # error_inject.ads lives in src/ada/errinject/, not here
├── src/
│   ├── c/
│   │   ├── mil1553_word.c
│   │   ├── manchester.c
│   │   ├── crc_parity.c
│   │   ├── bc_scheduler.c
│   │   ├── rt_service.c
│   │   ├── bm_monitor.c
│   │   ├── bus_model.c
│   │   ├── bit_pbit.c
│   │   ├── bit_cbit.c
│   │   ├── bit_ibit.c
│   │   ├── main_bc.c                  # BC binary entry point
│   │   ├── main_rt.c                  # RT binary entry point (RT1/RT2 selectable)
│   │   ├── main_bm.c                  # BM binary entry point
│   │   └── errinject/                 # separate lib, test-profile only
│   │       ├── error_inject.h
│   │       ├── error_inject.c
│   │       └── config_signature.c
│   └── ada/
│       ├── mil1553_word.adb
│       ├── manchester.adb
│       ├── crc_parity.adb
│       ├── bc_scheduler.adb
│       ├── rt_service.adb
│       ├── bm_monitor.adb
│       ├── bus_model.adb
│       ├── bit_pbit.adb
│       ├── bit_cbit.adb
│       ├── bit_ibit.adb
│       ├── main_bc.adb
│       ├── main_rt.adb
│       ├── main_bm.adb
│       └── errinject/
│           ├── error_inject.ads
│           ├── error_inject.adb
│           └── config_signature.adb
├── tests/
│   ├── c/                             # Unity/Ceedling
│   ├── ada/                           # AUnit
│   ├── hsi/                           # cross-language: C-BC vs Ada-RT etc.
│   ├── error_injection/
│   └── coverage/
│       ├── c_coverage.xml
│       └── ada_coverage.xml
├── tools/
│   ├── do330_tool_qual.md
│   ├── build_and_verify.sh
│   ├── run_error_campaign.sh
│   └── viz/
│       ├── render_diagrams.sh
│       ├── traceability_graph.py
│       ├── coverage_dashboard.py
│       ├── objectives_dashboard.py
│       ├── bus_timeline.py
│       ├── state_machine_viewer.py
│       └── bit_dashboard.py
├── config/
│   ├── bus_config.yaml
│   ├── error_scenarios.yaml
│   ├── error_scenarios.sig            # NEW — hash manifest checked at runtime
│   └── problem_report_template.md
├── viz/
│   ├── data/                          # NEW — generated JSON feeding all dashboards
│   ├── architecture.svg
│   ├── state_machine_bc.svg
│   ├── state_machine_rt.svg
│   ├── state_machine_bit.svg
│   ├── sequence_bc_rt.svg
│   ├── sequence_rt_rt.svg
│   ├── dataflow.svg
│   ├── v_model.svg
│   ├── traceability.html
│   ├── coverage.html
│   ├── objectives.html
│   ├── bus_timeline.html
│   ├── bit_dashboard.html
│   ├── error_campaign.html
│   ├── spark_proof.html
│   └── index.html
├── CMakeLists.txt                     # NEW — C build (release + test profiles)
├── mil1553_release.gpr                # NEW — GNAT project, error injection excluded
├── mil1553_test.gpr                   # NEW — GNAT project, error injection linked in
└── README.md