// Decompile the functions containing the given addresses (dedup), with callers. Local research only.
// @category X3
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
public class X3DecContaining extends GhidraScript {
  @Override public void run() throws Exception {
    String[] a = getScriptArgs();
    DecompInterface d = new DecompInterface();
    d.openProgram(currentProgram);
    Set<String> done = new HashSet<>();
    try (PrintWriter o = new PrintWriter(a[0])) {
      for (int i = 1; i < a.length; ++i) {
        boolean ctxOnly = a[i].startsWith("c");
        String s = ctxOnly ? a[i].substring(1) : a[i];
        Function f = getFunctionContaining(toAddr(s));
        if (f == null) { o.println("// ADDR " + s + " no function"); continue; }
        o.println("// ADDR " + s + " in " + f.getEntryPoint());
        if (!done.add(f.getEntryPoint().toString())) continue;
        StringBuilder cs = new StringBuilder();
        for (Reference r : getReferencesTo(f.getEntryPoint())) cs.append(" ").append(r.getFromAddress());
        o.println("// Function " + f.getEntryPoint() + " callers:" + cs);
        if (ctxOnly) continue;
        DecompileResults r = d.decompileFunction(f, 60, monitor);
        o.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : r.getErrorMessage());
      }
    } finally { d.dispose(); }
  }
}
