// Read-only helper: list references to given addresses. Usage: -postScript X3XrefsTo.java out addr...
// @category X3
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
public class X3XrefsTo extends GhidraScript {
  @Override public void run() throws Exception {
    String[] a = getScriptArgs();
    try (PrintWriter o = new PrintWriter(a[0])) {
      for (int i = 1; i < a.length; ++i) {
        o.println("TARGET " + a[i]);
        for (Reference r : getReferencesTo(toAddr(a[i]))) {
          Function f = getFunctionContaining(r.getFromAddress());
          o.println("  " + r.getFromAddress() + " " + r.getReferenceType() + " fn=" + (f == null ? "?" : f.getEntryPoint()));
        }
      }
    }
  }
}
