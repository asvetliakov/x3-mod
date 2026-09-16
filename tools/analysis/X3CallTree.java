// Per-frame call-tree survey: for each requested function entry, print its body
// size, its outgoing calls in callsite order (site, target, target size, name),
// the imported/thunk targets it reaches, and the string literals it references.
// Addresses, sizes and names only: no decompiled or disassembled code is
// written, so the output is derived data (docs/reverse-engineering rules).
// Usage: -postScript X3CallTree.java <output.txt> <depth> <addr> [<addr> ...]
//   depth 1 = the named functions only, 2 = also each direct callee.
// @category X3
import java.io.PrintWriter;
import java.util.ArrayDeque;
import java.util.LinkedHashSet;
import java.util.Set;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;

public class X3CallTree extends GhidraScript {
    private String describe(Address target) {
        Function f = getFunctionAt(target);
        if (f == null) f = getFunctionContaining(target);
        if (f == null) return target + " size=? name=?";
        return f.getEntryPoint() + " size=0x" + Long.toHexString(f.getBody().getNumAddresses())
            + " name=" + f.getName() + (f.isThunk() ? " (thunk)" : "");
    }

    private void emit(PrintWriter out, Function f, Set<Address> discovered) {
        out.println("FUNCTION " + f.getEntryPoint() + " size=0x"
            + Long.toHexString(f.getBody().getNumAddresses()) + " name=" + f.getName()
            + " params=" + f.getParameterCount() + " conv=" + f.getCallingConventionName());
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            String m = ins.getMnemonicString();
            boolean call = m.equalsIgnoreCase("CALL");
            for (Reference ref : ins.getReferencesFrom()) {
                RefType t = ref.getReferenceType();
                Address to = ref.getToAddress();
                if (call && (t.isCall() || t.isIndirect())) {
                    out.println("  CALL " + ins.getAddress() + " " + ins + " -> " + describe(to));
                    Function callee = getFunctionAt(to);
                    if (callee != null) discovered.add(callee.getEntryPoint());
                } else if (t.isData() && to.isMemoryAddress()) {
                    Data d = getDataAt(to);
                    if (d != null && d.hasStringValue())
                        out.println("  STR  " + ins.getAddress() + " " + to + " " + d.getDefaultValueRepresentation());
                }
            }
            if (call && ins.getReferencesFrom().length == 0)
                out.println("  CALL " + ins.getAddress() + " " + ins + " -> (unresolved)");
        }
        out.println("END " + f.getEntryPoint());
    }

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) throw new IllegalArgumentException("output path, depth, addresses required");
        int depth = Integer.parseInt(args[1]);
        try (PrintWriter out = new PrintWriter(args[0])) {
            Set<Address> done = new LinkedHashSet<>();
            ArrayDeque<Address> level = new ArrayDeque<>();
            for (int i = 2; i < args.length; ++i) {
                Function f = getFunctionContaining(toAddr(args[i]));
                if (f == null) { out.println("MISSING " + args[i]); continue; }
                level.add(f.getEntryPoint());
            }
            for (int d = 0; d < depth && !level.isEmpty(); ++d) {
                Set<Address> next = new LinkedHashSet<>();
                while (!level.isEmpty()) {
                    Address a = level.poll();
                    if (!done.add(a)) continue;
                    Function f = getFunctionAt(a);
                    if (f == null) continue;
                    emit(out, f, next);
                }
                for (Address a : next) if (!done.contains(a)) level.add(a);
            }
        }
    }
}
