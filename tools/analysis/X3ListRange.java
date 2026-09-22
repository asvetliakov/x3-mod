// Read-only helper: list instructions in ranges. Usage: out start:end ...
// @category X3
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
public class X3ListRange extends GhidraScript {
  @Override public void run() throws Exception {
    String[] a = getScriptArgs();
    try (PrintWriter o = new PrintWriter(a[0])) {
      for (int i = 1; i < a.length; ++i) {
        String[] r = a[i].split(":");
        AddressSet set = new AddressSet(toAddr(r[0]), toAddr(r[1]));
        o.println("RANGE " + a[i]);
        for (Instruction ins : currentProgram.getListing().getInstructions(set, true))
          o.println(ins.getAddress() + "  " + ins.toString());
      }
    }
  }
}
