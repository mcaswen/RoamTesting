# CBT 2024 official-semantics baseline v1

- Baseline ID: cbt-2024-official-baseline-v1
- Algorithm key: cbt_2024_official_baseline_v1
- Git commit: b0bdd5d0c25a523f5a15221dfb90eaf4db829b4c
- Benchmark tag: benchmark/cbt-2024-official-baseline-v1
- GPU: NVIDIA GeForce RTX 5090 D
- Graphics runtime: Direct3D 12 (feature level 12_0); requested Agility SDK 614; runtime 6.2.26100.8972 (C:\WINDOWS\SYSTEM32\D3D12Core.dll); driver 32.0.15.9186
- Raw report directory: raw/20260825-211546 (generated locally and excluded from Git)
- Repeats: 3; validation=Off; geometry=ModifiedOnly; VSync=Off
- License status: upstream has no declared license; public redistribution remains blocked

| Path | Capacity | Repeats | Samples/run | Mean triangles | Repeat SD | Mean GPU stages ms | Repeat SD | Mean remaining | Min remaining | Max depth | Issues |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| default | 128K | 3 | 600 | 18732.1417 | 0.0000 | 0.0834 | 0.0008 | 112345.8583 | 111050.0000 | 20 | 0 |
| default | 256K | 3 | 600 | 18732.1417 | 0.0000 | 0.0844 | 0.0000 | 243417.8583 | 242122.0000 | 20 | 0 |
| default | 512K | 3 | 600 | 18732.1417 | 0.0000 | 0.0878 | 0.0008 | 505561.8583 | 504266.0000 | 20 | 0 |
| default | 1M | 3 | 600 | 18732.1417 | 0.0000 | 0.0938 | 0.0004 | 1029849.8583 | 1028554.0000 | 20 | 0 |
| extreme | 128K | 3 | 64 | 120771.5573 | 54.5173 | 0.1470 | 0.0002 | 10306.4427 | 355.0000 | 20 | 0 |
| extreme | 256K | 3 | 64 | 201811.1719 | 0.0000 | 0.1588 | 0.0005 | 60338.8281 | 45427.0000 | 20 | 0 |
| extreme | 512K | 3 | 64 | 201811.1719 | 0.0000 | 0.1639 | 0.0006 | 322482.8281 | 307571.0000 | 20 | 0 |
| extreme | 1M | 3 | 64 | 201811.1719 | 0.0000 | 0.1694 | 0.0002 | 846770.8281 | 831859.0000 | 20 | 0 |

Per-run data: [run-summary.csv](run-summary.csv); aggregate data: [capacity-summary.csv](capacity-summary.csv)
