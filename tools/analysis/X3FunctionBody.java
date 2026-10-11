// Read-only helper: print the address ranges of a function's body and the
// direct calls it contains. Usage: -postScript X3FunctionBody.java <output.txt> <addr> [<addr> ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;

public class X3FunctionBody extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("Output path and addresses required");
        try (PrintWriter out = new PrintWriter(args[0])) {
            for (int i = 1; i < args.length; ++i) {
                Address a = toAddr(args[i]);
                Function f = getFunctionContaining(a);
                if (f == null) { out.println("ADDR " + args[i] + " no function"); continue; }
                out.println("FUNCTION " + f.getEntryPoint() + " size=" + f.getBody().getNumAddresses() + " name=" + f.getName());
                for (AddressRange r : f.getBody().getAddressRanges())
                    out.println("  range " + r.getMinAddress() + ":" + r.getMaxAddress() + " len=" + r.getLength());
                for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    String mn = ins.getMnemonicString();
                    if (!mn.equals("CALL")) continue;
                    for (Reference ref : ins.getReferencesFrom())
                        if (ref.getReferenceType().isCall())
                            out.println("  call " + ins.getAddress() + " -> " + ref.getToAddress() + " return=" + ins.getAddress().add(ins.getLength()));
                }
            }
        }
    }
}
