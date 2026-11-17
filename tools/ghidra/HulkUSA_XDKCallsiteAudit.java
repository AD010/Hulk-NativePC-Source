// Hulk USA XEX / Ghidra 12 headless or GUI audit.
// @category Hulk.NativeEoR
// @author Hulk NativePC source-only project
//
// Imports require an Xbox 360 XEX loader such as XEXLoaderWV. This is a
// READ-ONLY caller witness, not a patch, host function hook or decompiler
// proof of EoR draw/mesh/material/camera ABI. Never emits original code bytes.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.Locale;

public class HulkUSA_XDKCallsiteAudit extends GhidraScript {
    private static final String USA_SHA =
        "346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0";
    private static final long LOW = 0x82000000L, HIGH = 0x84000000L;
    private static final int MAX_EDGES = 100000;
    private static final long[] TARGETS = {
        0x8288DF30L, 0x8288E318L, 0x828897F8L, 0x8287F2C0L,
        0x8287F628L, 0x8288A900L, 0x8288AD98L, 0x82888D68L,
        0x82888C40L, 0x82880778L
    };
    private static final String[] NAMES = {
        "DrawVertices", "DrawIndexedVertices", "Resolve", "SetRenderTarget",
        "SetDepthStencilSurface", "BeginTiling", "EndTiling",
        "Clear", "ClearF", "Present"
    };

    private static String hex(long value) {
        return String.format(Locale.ROOT, "0x%08X", value & 0xFFFFFFFFL);
    }

    private static String digest(File original) throws Exception {
        if (!original.isFile() || original.length() < 100000 ||
            original.length() > 128L * 1024 * 1024)
            throw new IllegalArgumentException("Expected original USA default.xex");
        MessageDigest sha = MessageDigest.getInstance("SHA-256");
        try (BufferedInputStream in = new BufferedInputStream(
                new FileInputStream(original))) {
            byte[] chunk = new byte[65536];
            int n;
            while ((n = in.read(chunk)) != -1) sha.update(chunk, 0, n);
        }
        StringBuilder result = new StringBuilder(64);
        for (byte b : sha.digest())
            result.append(String.format(Locale.ROOT, "%02x", b & 0xff));
        return result.toString();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2)
            throw new IllegalArgumentException(
                "Arguments: [private USA default.xex] [output address-only JSON]");
        File xex = new File(args[0]);
        if (!USA_SHA.equals(digest(xex)))
            throw new IllegalArgumentException("This is NOT the pinned USA XEX.");
        if (currentProgram == null ||
            !currentProgram.getLanguage().getProcessor().toString()
                .toLowerCase(Locale.ROOT).contains("powerpc"))
            throw new IllegalArgumentException(
                "Import pinned XEX with XEXLoaderWV as PowerPC first.");

        ArrayList<String> records = new ArrayList<String>();
        HashSet<String> seen = new HashSet<String>();
        for (int i = 0; i < TARGETS.length; i++) {
            monitor.checkCancelled();
            Address xdk = toAddr(TARGETS[i]);
            // No false positives for an unimported or incorrectly rebased XEX.
            if (!currentProgram.getMemory().contains(xdk))
                throw new IllegalArgumentException(
                    "XDK target not mapped in Ghidra XEX: " + hex(TARGETS[i]));
            ReferenceIterator refs =
                currentProgram.getReferenceManager().getReferencesTo(xdk);
            while (refs.hasNext()) {
                monitor.checkCancelled();
                Reference ref = refs.next();
                if (!ref.getReferenceType().isCall()) continue;
                Address origin = ref.getFromAddress();
                Instruction ins =
                    currentProgram.getListing().getInstructionAt(origin);
                if (ins == null || ins.getLength() != 4 ||
                    !ins.getFlowType().isCall() ||
                    ins.getFlowType().isComputed() ||
                    !ins.getMnemonicString().toLowerCase(Locale.ROOT)
                        .startsWith("bl")) continue;
                long site = origin.getOffset();
                if (site < LOW || site + 4 >= HIGH) continue;
                boolean actualFlow = false;
                for (Address dst : ins.getFlows()) {
                    if (dst.equals(xdk)) { actualFlow = true; break; }
                }
                if (!actualFlow) continue;
                Function caller =
                    currentProgram.getFunctionManager().getFunctionContaining(origin);
                if (caller == null) continue;
                long start = caller.getEntryPoint().getOffset();
                if (start < LOW || start >= HIGH) continue;
                String unique = hex(TARGETS[i]) + ":" + hex(site);
                if (!seen.add(unique)) continue;
                records.add("    {\"xdk_guest_address\":\"" + hex(TARGETS[i]) +
                    "\",\"verified_xdk_method\":\"" + NAMES[i] +
                    "\",\"static_direct_bl_site\":\"" + hex(site) +
                    "\",\"observed_guest_lr\":\"" + hex(site + 4) +
                    "\",\"source_defined_guest_caller\":\"" + hex(start) +
                    "\",\"direct_bl\":true,\"engine_draw_abi_verified\":false}");
                if (records.size() > MAX_EDGES)
                    throw new IllegalArgumentException("Excessive XEX references");
            }
        }
        Collections.sort(records);
        String body = "{\n  \"schema\": \"hulk_ghidra_usa_xdk_direct_v1\",\n" +
            "  \"source_xex_sha256\": \"" + USA_SHA + "\",\n" +
            "  \"powerpc_import_verified\": true,\n" +
            "  \"engine_draw_abi_verified\": false,\n" +
            "  \"edges\": [\n" + String.join(",\n", records) + "\n  ]\n}\n";
        Path output = new File(args[1]).toPath().toAbsolutePath().normalize();
        if (output.equals(xex.toPath().toAbsolutePath().normalize()))
            throw new IllegalArgumentException("Refusing to overwrite original XEX");
        if (output.getParent() != null)
            Files.createDirectories(output.getParent());
        Files.write(output, body.getBytes(StandardCharsets.UTF_8));
        println("[HULK/GHIDRA] Original USA XDK direct callsites: " +
                records.size() + " (address-only; no engine Draw ABI inferred).");
    }
}
