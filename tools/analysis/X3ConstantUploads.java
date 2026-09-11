// Locate COM vtable-slot call sites, dump candidate vtables and decompile owners.
// Research aid for "which code uploads shader constants to IDirect3DDevice9".
// Invoke headless with -readOnly -noanalysis against an already analyzed program.
//
// Usage: -postScript X3ConstantUploads.java /tmp/private-output.txt <request>...
//   disp:0x178            list every instruction that references that vtable
//                         displacement (whole program), with call-site context;
//                         X3 emits "mov reg,[vtbl+disp]" then "call reg", so the
//                         paired CALL/JMP is found in the printed context
//   owner:0x004c0150      restrict site listing to this containing function
//   tally                 print the reference count per displacement
//   vtable:0x408ac4:24    print 24 pointer slots starting at that data address
//   xref:0x00608b3c       print references to a data/code address
//   dec:0x004c0150        decompile the function containing the address
//
// Output is derived game implementation: keep it local and untracked.
// @category X3
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

public class X3ConstantUploads extends GhidraScript {
    private static final Pattern DISP =
        Pattern.compile("\\[[A-Z]+(?:\\s*\\+\\s*[A-Z]+\\*0x[0-9a-fA-F]+)?\\s*\\+\\s*(0x[0-9a-fA-F]+)\\]");

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("Private output path and at least one request required");

        LinkedHashSet<Long> wanted = new LinkedHashSet<>();
        LinkedHashSet<String> owners = new LinkedHashSet<>();
        boolean tally = false;
        for (int i = 1; i < args.length; ++i) {
            if (args[i].startsWith("disp:")) wanted.add(Long.decode(args[i].substring(5)));
            else if (args[i].startsWith("owner:")) owners.add(toAddr(args[i].substring(6)).toString());
            else if (args[i].equals("tally")) tally = true;
        }

        TreeMap<Long, Integer> counts = new TreeMap<>();
        TreeMap<Long, List<Address>> sites = new TreeMap<>();
        for (Long d : wanted) sites.put(d, new ArrayList<>());

        try (PrintWriter out = new PrintWriter(args[0])) {
            out.println("Program " + currentProgram.getName() + " base " + currentProgram.getImageBase());

            if (tally || !wanted.isEmpty()) {
                var it = currentProgram.getListing().getInstructions(true);
                while (it.hasNext()) {
                    Instruction ins = it.next();
                    String text = ins.toString();
                    Matcher m = DISP.matcher(text);
                    while (m.find()) {
                        long disp = Long.decode(m.group(1));
                        List<Address> list = sites.get(disp);
                        if (list == null && (disp < 0x8 || disp > 0x400)) continue;
                        counts.merge(disp, 1, Integer::sum);
                        if (list != null) list.add(ins.getAddress());
                    }
                }
            }

            if (tally) {
                out.println("=== INDIRECT CALL DISPLACEMENT TALLY ===");
                for (Map.Entry<Long, Integer> e : counts.entrySet())
                    out.println(String.format("DISP 0x%x slot %d count %d", e.getKey(),
                        e.getKey() / 4, e.getValue()));
            }

            for (Map.Entry<Long, List<Address>> e : sites.entrySet()) {
                out.println(String.format("=== SITES DISP 0x%x (slot %d) count %d ===",
                    e.getKey(), e.getKey() / 4, e.getValue().size()));
                for (Address a : e.getValue()) {
                    Function f = getFunctionContaining(a);
                    String owner = f == null ? "none" : f.getEntryPoint().toString();
                    if (!owners.isEmpty() && !owners.contains(owner)) continue;
                    out.println("SITE " + a + " owner " + owner);
                    Instruction ins = getInstructionAt(a);
                    Instruction back = ins;
                    for (int i = 0; i < 10 && back != null && back.getPrevious() != null; ++i) back = back.getPrevious();
                    for (int i = 0; i < 32 && back != null; ++i, back = back.getNext())
                        out.println("  " + back.getAddress() + " " + back);
                }
            }

            DecompInterface decompiler = new DecompInterface();
            boolean opened = false;
            try {
                for (int i = 1; i < args.length; ++i) {
                    String request = args[i];
                    if (request.startsWith("vtable:")) {
                        String[] parts = request.substring(7).split(":");
                        Address base = toAddr(parts[0]);
                        int n = parts.length > 1 ? Integer.parseInt(parts[1]) : 24;
                        out.println("=== VTABLE " + base + " slots " + n + " ===");
                        for (int s = 0; s < n; ++s) {
                            Address slot = base.add(4L * s);
                            long value = getInt(slot) & 0xffffffffL;
                            Address target = toAddr(value);
                            Function f = getFunctionAt(target);
                            out.println(String.format("  slot %-3d %s -> %s %s", s, slot, target,
                                f == null ? "(no function)" : f.getName()));
                        }
                    } else if (request.startsWith("xref:")) {
                        Address a = toAddr(request.substring(5));
                        out.println("=== XREFS " + a + " ===");
                        for (Reference r : getReferencesTo(a)) {
                            Function f = getFunctionContaining(r.getFromAddress());
                            out.println("  " + r.getFromAddress() + " " + r.getReferenceType()
                                + " owner=" + (f == null ? "?" : f.getEntryPoint().toString()));
                            Instruction ins = getInstructionAt(r.getFromAddress());
                            if (ins != null) out.println("    " + ins);
                        }
                    } else if (request.startsWith("dec:")) {
                        if (!opened) { decompiler.openProgram(currentProgram); opened = true; }
                        Address a = toAddr(request.substring(4));
                        Function f = getFunctionContaining(a);
                        out.println("=== DECOMPILE " + a + " owner "
                            + (f == null ? "none" : f.getEntryPoint().toString()) + " ===");
                        if (f == null) continue;
                        DecompileResults results = decompiler.decompileFunction(f, 60, monitor);
                        out.println(results.decompileCompleted()
                            ? results.getDecompiledFunction().getC() : results.getErrorMessage());
                    }
                }
            } finally { if (opened) decompiler.dispose(); }
        }
    }
}
