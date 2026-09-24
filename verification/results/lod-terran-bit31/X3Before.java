// Read-only: for each address print the N preceding and 3 following instructions in address order.
// Usage: -postScript X3Before.java out N addr...
// @category X3
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
public class X3Before extends GhidraScript {
  @Override public void run() throws Exception {
    String[] a = getScriptArgs();
    int n = Integer.parseInt(a[1]);
    Listing l = currentProgram.getListing();
    try (PrintWriter o = new PrintWriter(a[0])) {
      for (int i = 2; i < a.length; ++i) {
        Instruction ins = l.getInstructionAt(toAddr(a[i]));
        o.println("=== " + a[i]);
        if (ins == null) { o.println("no insn"); continue; }
        Instruction s = ins;
        for (int k = 0; k < n; ++k) { Instruction p = l.getInstructionBefore(s.getAddress()); if (p == null) break; s = p; }
        for (int k = 0; k < n + 4 && s != null; ++k) {
          o.println((s.equals(ins) ? "=> " : "   ") + s.getAddress() + " " + s);
          s = s.getNext();
        }
      }
    }
  }
}
