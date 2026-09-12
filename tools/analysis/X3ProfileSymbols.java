// Map sampling-profiler RVAs of X3AP.exe to their containing functions.
// Output is addresses, names and sizes only: no decompiled or disassembled code.
// Usage: -postScript X3ProfileSymbols.java <input> <output.json>
//   input: a text file with one RVA per line (hex, 0x optional) or a JSON
//   array of such strings / the `main_module_rvas` array written by
//   tools/analysis/summarize_profile.py (a JSON object with that key).
// @category X3
import java.io.FileReader;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class X3ProfileSymbols extends GhidraScript {
    private static long parseRva(String text) {
        String t = text.trim().toLowerCase();
        if (t.startsWith("0x")) t = t.substring(2);
        return Long.parseLong(t, 16);
    }

    private List<Long> readInput(String path) throws Exception {
        List<Long> rvas = new ArrayList<>();
        if (path.endsWith(".json")) {
            JsonElement root = JsonParser.parseReader(new FileReader(path));
            JsonArray array = root.isJsonArray() ? root.getAsJsonArray() : root.getAsJsonObject().getAsJsonArray("main_module_rvas");
            for (JsonElement e : array) rvas.add(e.isJsonPrimitive() && e.getAsJsonPrimitive().isNumber() ? e.getAsLong() : parseRva(e.getAsString()));
        } else {
            for (String line : Files.readAllLines(Paths.get(path))) {
                String t = line.trim();
                if (!t.isEmpty() && !t.startsWith("#")) rvas.add(parseRva(t));
            }
        }
        return rvas;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("Input list and output path required");
        long base = currentProgram.getImageBase().getOffset();
        JsonObject symbols = new JsonObject();
        int resolved = 0;
        List<Long> rvas = readInput(args[0]);
        for (long rva : rvas) {
            Address address = currentProgram.getImageBase().add(rva);
            Function function = getFunctionContaining(address);
            String key = String.format("0x%x", rva);
            if (function == null) { symbols.add(key, null); continue; }
            long start = function.getEntryPoint().getOffset();
            JsonObject entry = new JsonObject();
            entry.addProperty("address", String.format("0x%08x", address.getOffset()));
            entry.addProperty("function_start", String.format("0x%08x", start));
            entry.addProperty("function_start_rva", String.format("0x%x", start - base));
            entry.addProperty("function_name", function.getName());
            entry.addProperty("function_size", function.getBody().getNumAddresses());
            entry.addProperty("offset_in_function", address.getOffset() - start);
            symbols.add(key, entry);
            ++resolved;
        }
        JsonObject output = new JsonObject();
        output.addProperty("program", currentProgram.getName());
        output.addProperty("image_base", String.format("0x%08x", base));
        output.addProperty("requested", rvas.size());
        output.addProperty("resolved", resolved);
        output.add("symbols", symbols);
        try (PrintWriter writer = new PrintWriter(args[1])) {
            writer.println(new GsonBuilder().setPrettyPrinting().create().toJson(output));
        }
        println("X3ProfileSymbols resolved " + resolved + "/" + rvas.size() + " -> " + args[1]);
    }
}
