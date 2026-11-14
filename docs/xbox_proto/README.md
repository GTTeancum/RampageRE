# How the Xbox prototype does it

Notes reverse-engineered from the October 2005 Pipeworks Xbox build (`Rampage Xbox prototype/RampageD.xbe`, the
debug build: unoptimised, linked against XDK 5849 debug libraries), as the reference for the port's Xbox work
(docs/XBOX_PERF_PLAN.md). Addresses are virtual addresses in `RampageD.xbe` unless a note says otherwise.

Tooling (all read-only on the prototype):

- `tools/xbe_libmatch.py`: names the XDK library functions in an XBE by matching the XDK's .lib files.
- `tools/xbe2elf.py`: wraps an XBE as an ELF with those names, for Ghidra.
- `tools/ghidra/DumpDecomp.java`: Ghidra headless script, every function to C plus the call graph and strings.
- `tools/xbe_rtti.py`: class names, bases and virtual tables from the RTTI.
- `tools/re_query.py`: show / callers / callees / grep / strings / class / vtslot over the dump.
- `tools/xbe_findvsh.py`, `tools/xbe_vshdis.py`: find and disassemble compiled vertex shaders.
