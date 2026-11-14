// Headless post-script: decompile every function of the .text section to one C file and write the call graph.
// analyzeHeadless PROJECT_DIR NAME -import X.elf -processor x86:LE:32:default -cspec windows
//     -scriptPath tools/ghidra -postScript DumpDecomp.java OUT_DIR [START END]
// OUT_DIR/decomp.c    "// ==== name @ address (size)" then the C of each function, by address
// OUT_DIR/calls.tsv   caller address, caller name, callee address, callee name
// OUT_DIR/funcs.tsv   address, size, name
// OUT_DIR/strings.tsv address of the reference, function, string address, the string
//@category RampageRE

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

public class DumpDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args[0]);
        out.mkdirs();
        long start = args.length > 2 ? Long.decode(args[1]) : 0L;
        long end = args.length > 2 ? Long.decode(args[2]) : 0xFFFFFFFFL;
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        PrintWriter c = new PrintWriter(new File(out, "decomp.c"), "UTF-8");
        PrintWriter calls = new PrintWriter(new File(out, "calls.tsv"), "UTF-8");
        PrintWriter funcs = new PrintWriter(new File(out, "funcs.tsv"), "UTF-8");
        PrintWriter strs = new PrintWriter(new File(out, "strings.tsv"), "UTF-8");
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        int n = 0, failed = 0;
        while (it.hasNext() && !monitor.isCancelled()) {
            Function f = it.next();
            long a = f.getEntryPoint().getOffset();
            long size = f.getBody().getNumAddresses();
            funcs.println(String.format("%08x\t%d\t%s", a, size, f.getName()));
            InstructionIterator ins = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (ins.hasNext()) {
                Instruction i = ins.next();
                for (Reference r : i.getReferencesFrom()) {
                    Address to = r.getToAddress();
                    if (r.getReferenceType().isCall()) {
                        Function callee = getFunctionAt(to);
                        calls.println(String.format("%08x\t%s\t%08x\t%s", a, f.getName(), to.getOffset(),
                                callee != null ? callee.getName() : "?"));
                    } else if (r.getReferenceType().isData()) {
                        Data d = getDataAt(to);
                        if (d != null && d.hasStringValue()) {
                            String s = StringDataInstance.getStringDataInstance(d).getStringValue();
                            if (s != null)
                                strs.println(String.format("%08x\t%s\t%08x\t%s", i.getAddress().getOffset(), f.getName(),
                                        to.getOffset(), s.replace("\n", "\\n").replace("\t", " ")));
                        }
                    }
                }
            }
            if (a < start || a >= end)
                continue;
            DecompileResults res = dec.decompileFunction(f, 60, monitor);
            c.println(String.format("// ==== %s @ %08x (%d)", f.getName(), a, size));
            if (res != null && res.decompileCompleted())
                c.println(res.getDecompiledFunction().getC());
            else {
                c.println("// decompile failed");
                failed++;
            }
            if (++n % 500 == 0)
                println("decompiled " + n);
        }
        c.close();
        calls.close();
        funcs.close();
        strs.close();
        println("done: " + n + " functions, " + failed + " failed");
    }
}
