// Read-only helper: instructions reading a global pointer, then within N instructions a field access matching regex.
// Usage: out globalHex fieldRegex
// @category X3
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
public class X3FieldAfterGlobal extends GhidraScript {
  @Override public void run() throws Exception {
    String[] a = getScriptArgs();
    String g = "[0x" + a[1] + "]";
    java.util.regex.Pattern p = java.util.regex.Pattern.compile(a[2]);
    try (PrintWriter o = new PrintWriter(a[0])) {
      for (Instruction ins : currentProgram.getListing().getInstructions(true)) {
        if (!ins.toString().contains(g)) continue;
        Instruction n = ins;
        for (int i = 0; i < 10; ++i) { n = n.getNext(); if (n == null) break;
          String t = n.toString();
          if (p.matcher(t).find()) { Function f = getFunctionContaining(ins.getAddress());
            o.println((f==null?"?":f.getEntryPoint().toString()) + " " + ins.getAddress() + " -> " + n.getAddress() + " " + t); break; }
        }
      }
    }
  }
}
