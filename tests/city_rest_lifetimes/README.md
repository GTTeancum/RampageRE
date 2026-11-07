# Building rest matrix ownership regression

Run `python3 tests/city_rest_lifetimes/run.py` or append `--sanitize`.
Set TMPDIR to an available writable directory; artifacts are retained there.
CITY_SOURCE overrides src/city.c; RAMPAGE_ROOT overrides the repository path.

Extracts the actual allocation/initialization/publication block by source boundaries, not historical line numbers. Tests 2,000 allocation failures and 2,000 successes, including zero and nonzero instance spans, allocation sizes, initialized matrix bytes, and no retained allocations. Fails/crashes on the prior unchecked code. ASan/UBSan mode disables LSan because this environment cannot support it; exact test allocation accounting remains enabled.

This focused synthetic test does not test city_build propagation or scene_free. Real-asset whole-source failure/recovery/equivalence evidence is maintained separately; do not infer full constructor safety from this test.
