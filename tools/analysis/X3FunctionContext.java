// For each requested address print the containing function entry, its size, its
// callers, and its called targets. Addresses and names only; no code is written.
// Usage: -postScript X3FunctionContext.java <output.txt> <addr> [<addr> ...]
// @category X3
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class X3FunctionContext extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("Output path and addresses required");
        try (PrintWriter out = new PrintWriter(args[0])) {
            for (int i = 1; i < args.length; ++i) {
                Address a = toAddr(args[i]);
                Function f = getFunctionContaining(a);
                if (f == null) { out.println("ADDR " + args[i] + " no function"); continue; }
                out.println("ADDR " + args[i] + " in " + f.getEntryPoint() + " size=" + f.getBody().getNumAddresses() + " name=" + f.getName());
                for (Reference r : getReferencesTo(f.getEntryPoint())) {
                    out.println("  caller_site " + r.getFromAddress() + " " + r.getReferenceType());
                }
            }
        }
    }
}
