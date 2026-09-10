// Read-only, targeted loading cross-references for the documented X3AP.exe hash.
// @category X3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class X3LoadingXrefs extends GhidraScript {
    private void show(Address address, int depth) {
        for (Reference ref : getReferencesTo(address)) {
            Function owner = getFunctionContaining(ref.getFromAddress());
            println("  ".repeat(depth) + ref.getFromAddress() + " " + ref.getReferenceType() +
                " function=" + (owner == null ? "unknown" : owner.getEntryPoint()));
            if (depth == 1 && owner != null && owner.isThunk()) show(owner.getEntryPoint(), depth + 1);
        }
    }
    @Override public void run() throws Exception {
        String[] labels = {"ReadFile", "CreateFileA", "SetFilePointer", "gzopen", "gzread",
            "gzseek", "inflate", "inflateInit2_", "D3DXCreateEffect", "D3DXCreateTexture",
            "D3DXCreateCubeTexture", "D3DXLoadSurface", "xmlReadMemory", "resource_loader"};
        long[] targets = {0x532158L, 0x5320ccL, 0x5320acL, 0x5323d8L, 0x5323e4L,
            0x5323fcL, 0x5323e8L, 0x5323dcL, 0x532324L, 0x532334L,
            0x532338L, 0x532330L, 0x5323acL, 0x4e8e10L};
        for (int i = 0; i < targets.length; ++i) {
            Address address = toAddr(targets[i]);
            println("TARGET " + labels[i] + " " + address);
            show(address, 1);
        }
    }
}
