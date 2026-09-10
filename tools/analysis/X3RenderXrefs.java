// Report targeted render import/string cross-references from a local X3AP.exe.
// @category X3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class X3RenderXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        long[] targets = {0x532314L, 0x532324L, 0x532350L, 0x532354L,
            0x4faedcL, 0x4faeeeL, 0x4faf24L,
            0x563020L, 0x563358L, 0x563388L, 0x563390L,
            0x563480L, 0x5634e4L, 0x563570L, 0x563774L,
            0x5637bcL, 0x5637f0L};
        // Addresses are scoped to the documented executable SHA256.
        for (long value : targets) {
            Address address = toAddr(value);
            println("TARGET " + address);
            for (Reference ref : getReferencesTo(address)) {
                Function owner = getFunctionContaining(ref.getFromAddress());
                println("  " + ref.getFromAddress() + " " + ref.getReferenceType() +
                    " function=" + (owner == null ? "unknown" : owner.getEntryPoint()));
            }
        }
    }
}
