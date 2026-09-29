// Export focused static evidence without renaming functions or changing types.
// Args: PE survey JSON, candidate TSV, output directory.
// Decompiled text and instruction listings belong in ignored local-work/ only.
//@category GoH.Research

import com.google.gson.*;
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public class ExportAiAnchors extends GhidraScript {
    private final Map<Address, Function> selected = new TreeMap<>();
    private final Map<Address, Set<String>> reasons = new HashMap<>();
    private Address base;

    private String rva(Address address) {
        return "0x" + Long.toHexString(address.subtract(base));
    }

    private Address address(String value) {
        return base.add(Long.decode(value));
    }

    private void select(Function function, String reason) {
        if (function == null || function.isExternal()) return;
        Address entry = function.getEntryPoint();
        selected.put(entry, function);
        reasons.computeIfAbsent(entry, key -> new TreeSet<>()).add(reason);
    }

    private JsonObject reference(Reference ref) {
        JsonObject item = new JsonObject();
        item.addProperty("from_rva", rva(ref.getFromAddress()));
        item.addProperty("kind", ref.getReferenceType().toString());
        Function owner = getFunctionContaining(ref.getFromAddress());
        if (owner != null) item.addProperty("owner_function_rva", rva(owner.getEntryPoint()));
        return item;
    }

    private JsonObject anchor(String label, Address target) {
        JsonObject item = new JsonObject();
        item.addProperty("label", label);
        item.addProperty("rva", rva(target));
        JsonArray refs = new JsonArray();
        ReferenceIterator iterator = currentProgram.getReferenceManager().getReferencesTo(target);
        int count = 0;
        while (iterator.hasNext() && count++ < 128) {
            Reference ref = iterator.next();
            refs.add(reference(ref));
            select(getFunctionContaining(ref.getFromAddress()), label);
        }
        item.add("references", refs);
        item.addProperty("references_truncated", iterator.hasNext());
        return item;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) throw new IllegalArgumentException(
            "Expected: survey.json candidates.tsv output-directory");
        Path output = Paths.get(args[2]);
        Files.createDirectories(output);
        base = currentProgram.getImageBase();
        JsonObject survey = JsonParser.parseString(Files.readString(Paths.get(args[0]))).getAsJsonObject();
        String expectedHash = survey.getAsJsonObject("binary").get("sha256").getAsString();
        if (!expectedHash.equalsIgnoreCase(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Survey hash does not match imported executable");
        if (!survey.getAsJsonObject("binary").get("image_base").getAsString().equals(
                "0x" + Long.toHexString(base.getOffset())))
            throw new IllegalArgumentException("Rebased program: import at preferred image base first");

        JsonObject report = new JsonObject();
        report.addProperty("schema_version", 1);
        report.add("binary", survey.get("binary"));
        report.addProperty("ghidra_function_count", currentProgram.getFunctionManager().getFunctionCount());
        report.addProperty("warning", "Function names and prototypes are Ghidra analysis output; " +
            "not verified ABI declarations. Indirect references/calls may be unresolved.");

        JsonArray candidates = new JsonArray();
        for (String line : Files.readAllLines(Paths.get(args[1]), StandardCharsets.UTF_8)) {
            if (line.isBlank() || line.startsWith("#")) continue;
            String[] fields = line.split("\t", 2);
            Address target = address(fields[0]);
            Function exact = getFunctionAt(target);
            Function containing = getFunctionContaining(target);
            JsonObject item = new JsonObject();
            item.addProperty("rva", fields[0]);
            item.addProperty("label", fields[1]);
            item.addProperty("exact_function_entry", exact != null);
            if (containing != null)
                item.addProperty("containing_function_rva", rva(containing.getEntryPoint()));
            candidates.add(item);
            // A containing function is NOT sufficient evidence to accept a target.
            select(exact, fields[1]);
        }
        report.add("candidates", candidates);

        JsonArray anchors = new JsonArray();
        for (Map.Entry<String, JsonElement> entry : survey.getAsJsonObject("string_anchors").entrySet())
            for (JsonElement value : entry.getValue().getAsJsonArray())
                anchors.add(anchor("string:" + entry.getKey(), address(value.getAsString())));
        for (Map.Entry<String, JsonElement> entry : survey.getAsJsonObject("types").entrySet()) {
            if (entry.getKey().equals("eActor")) continue;
            for (JsonElement descriptor : entry.getValue().getAsJsonArray())
                for (JsonElement locator : descriptor.getAsJsonObject().getAsJsonArray("locators"))
                    for (JsonElement table : locator.getAsJsonObject().getAsJsonArray("vtables"))
                        anchors.add(anchor("vtable:" + entry.getKey() + ":sub" +
                            locator.getAsJsonObject().get("subobject_offset").getAsInt(),
                            address(table.getAsJsonObject().get("rva").getAsString())));
        }
        report.add("anchors", anchors);

        DecompInterface decompiler = new DecompInterface();
        JsonArray functions = new JsonArray();
        try {
            if (!decompiler.openProgram(currentProgram))
                throw new IllegalStateException(decompiler.getLastMessage());
            for (Function function : selected.values()) {
                monitor.checkCancelled();
                Address entry = function.getEntryPoint();
                String stem = rva(entry);
                JsonObject item = new JsonObject();
                item.addProperty("rva", stem);
                item.addProperty("ghidra_name", function.getName(true));
                item.addProperty("ghidra_prototype", function.getPrototypeString(false, false));
                item.addProperty("body_address_count", function.getBody().getNumAddresses());
                JsonArray why = new JsonArray();
                for (String reason : reasons.get(entry)) why.add(reason);
                item.add("selection_reasons", why);
                JsonArray incoming = new JsonArray();
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(entry);
                int count = 0;
                while (refs.hasNext() && count++ < 256) incoming.add(reference(refs.next()));
                item.add("entry_references", incoming);
                item.addProperty("entry_references_truncated", refs.hasNext());
                JsonArray calls = new JsonArray();
                StringBuilder listing = new StringBuilder();
                InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
                while (instructions.hasNext()) {
                    Instruction instruction = instructions.next();
                    listing.append(rva(instruction.getAddress())).append("\t").append(instruction).append("\n");
                    if (instruction.getFlowType().isCall()) {
                        JsonObject call = new JsonObject();
                        call.addProperty("at_rva", rva(instruction.getAddress()));
                        JsonArray targets = new JsonArray();
                        for (Address target : instruction.getFlows()) targets.add(rva(target));
                        call.add("resolved_target_rvas", targets);
                        call.addProperty("computed", instruction.getFlowType().isComputed());
                        calls.add(call);
                    }
                }
                item.add("calls", calls);
                Files.writeString(output.resolve(stem + ".asm"), listing);
                DecompileResults result = decompiler.decompileFunction(function, 45, monitor);
                item.addProperty("decompile_completed", result.decompileCompleted());
                item.addProperty("decompile_message", result.getErrorMessage());
                if (result.decompileCompleted())
                    Files.writeString(output.resolve(stem + ".c"), result.getDecompiledFunction().getC());
                functions.add(item);
                println("Exported " + stem + " " + function.getName());
            }
        } finally {
            decompiler.dispose();
        }
        report.add("functions", functions);
        Files.writeString(output.resolve("export.json"),
            new GsonBuilder().setPrettyPrinting().create().toJson(report) + "\n");
        println("Exported " + functions.size() + " functions to " + output);
    }
}
