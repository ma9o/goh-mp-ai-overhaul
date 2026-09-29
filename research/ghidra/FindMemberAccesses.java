// Find literal memory displacements as leads; this is not type-aware field analysis.
// Args: PE survey JSON, output JSON, one or more hex/decimal displacements.
//@category GoH.Research

import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

public class FindMemberAccesses extends GhidraScript {
    private Address base;

    private String rva(Address address) {
        return "0x" + Long.toHexString(address.subtract(base));
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) throw new IllegalArgumentException(
            "Expected: survey.json output.json displacement...");
        base = currentProgram.getImageBase();
        JsonObject survey = JsonParser.parseString(Files.readString(Paths.get(args[0]))).getAsJsonObject();
        JsonObject binary = survey.getAsJsonObject("binary");
        if (!binary.get("sha256").getAsString().equalsIgnoreCase(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Survey hash does not match imported executable");
        if (!binary.get("image_base").getAsString().equals("0x" + Long.toHexString(base.getOffset())))
            throw new IllegalArgumentException("Import at preferred image base first");
        Set<Long> offsets = new TreeSet<>();
        for (int i = 2; i < args.length; i++) offsets.add(Long.decode(args[i]));
        JsonObject report = new JsonObject();
        report.addProperty("schema_version", 1);
        report.add("binary", binary);
        report.addProperty("warning", "Literal-displacement leads only. Unrelated objects/stack slots can match; " +
            "aliased pointers, bulk accesses, adjusted this pointers, and undisassembled code can be missed.");
        JsonArray requested = new JsonArray();
        for (long value : offsets) requested.add("0x" + Long.toHexString(value));
        report.add("requested_offsets", requested);
        Pattern displacement = Pattern.compile("([+-])\\s*0x([0-9a-fA-F]+)\\b");
        JsonArray matches = new JsonArray();
        InstructionIterator instructions = currentProgram.getListing().getInstructions(true);
        long visited = 0;
        while (instructions.hasNext()) {
            monitor.checkCancelled();
            Instruction instruction = instructions.next();
            visited++;
            for (int index = 0; index < instruction.getNumOperands(); index++) {
                String operand = instruction.getDefaultOperandRepresentation(index);
                if (!operand.contains("[") || !operand.contains("]")) continue;
                Matcher matcher = displacement.matcher(operand);
                while (matcher.find()) {
                    long value = Long.parseUnsignedLong(matcher.group(2), 16);
                    if (!offsets.contains(value)) continue;
                    JsonObject item = new JsonObject();
                    item.addProperty("at_rva", rva(instruction.getAddress()));
                    item.addProperty("displacement", "0x" + Long.toHexString(value));
                    item.addProperty("sign", matcher.group(1));
                    item.addProperty("operand_index", index);
                    item.addProperty("operand_reference_type", instruction.getOperandRefType(index).toString());
                    item.addProperty("instruction", instruction.toString());
                    Function owner = getFunctionContaining(instruction.getAddress());
                    if (owner != null) {
                        item.addProperty("function_rva", rva(owner.getEntryPoint()));
                        item.addProperty("ghidra_name", owner.getName(true));
                    }
                    matches.add(item);
                }
            }
        }
        report.addProperty("instructions_visited", visited);
        report.add("matches", matches);
        Path output = Paths.get(args[1]);
        if (output.getParent() != null) Files.createDirectories(output.getParent());
        Files.writeString(output, new GsonBuilder().setPrettyPrinting().create().toJson(report) + "\n");
        println("Member scan completed: " + matches.size() + " leads from " + visited + " instructions");
    }
}
