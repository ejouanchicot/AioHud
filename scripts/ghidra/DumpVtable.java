// Dumps the IPlugin vtable of a Windower 4 plugin and, for each virtual method,
// prints the recovered signature plus the external/thunk functions it calls
// (lets us label which slot is D3D / DirectInput / packet / text / command, etc).
//
// Run via analyzeHeadless ... -postScript DumpVtable.java
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.*;

public class DumpVtable extends GhidraScript {
    @Override
    public void run() throws Exception {
        long base = 0x100aea14L;   // FFXIDB IPlugin vtable (RVA 0xAEA14)
        int maxSlots = 48;

        Memory mem = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        long textMin = 0x10001000L, textMax = 0x1007e000L;

        println("=== IPlugin vtable dump @ 0x" + Long.toHexString(base) + " ===");
        for (int i = 0; i < maxSlots; i++) {
            Address slot = toAddr(base + (long) i * 4);
            long fa;
            try {
                fa = ((long) mem.getInt(slot)) & 0xffffffffL;
            } catch (Exception e) {
                println("[" + i + "] <unreadable slot> -- stop");
                break;
            }
            // stop when the entry is no longer a .text code pointer (end of vtable)
            if (fa < textMin || fa >= textMax) {
                println("[" + i + "] 0x" + Long.toHexString(fa) + " (not .text) -- VTABLE END, " + i + " methods");
                break;
            }
            Address fAddr = toAddr(fa);
            Function f = fm.getFunctionAt(fAddr);
            if (f == null) {
                f = createFunction(fAddr, null); // try to define it
            }
            String sig = (f != null) ? f.getSignature().getPrototypeString() : "(no func)";
            String cc = (f != null) ? f.getCallingConventionName() : "?";
            int purge = (f != null) ? f.getStackPurgeSize() : -999;
            // purge = bytes the callee pops (ret N). For __thiscall, N/4 = stack arg count (this is in ECX, not counted).
            String args = (purge >= 0) ? Integer.toString(purge / 4) : ("?(" + purge + ")");
            println(String.format("[%2d] +0x%02x -> 0x%08x  purge=%d (stackargs=%s) cc=%s  %s",
                    i, i * 4, fa, purge, args, cc, sig));

            if (f != null) {
                StringBuilder calls = new StringBuilder();
                InstructionIterator it = listing.getInstructions(f.getBody(), true);
                int guard = 0;
                while (it.hasNext() && guard++ < 4000) {
                    Instruction ins = it.next();
                    if (ins.getFlowType().isCall()) {
                        for (Reference r : ins.getReferencesFrom()) {
                            Function cf = fm.getFunctionAt(r.getToAddress());
                            if (cf != null) {
                                String n = cf.getName();
                                if (cf.isThunk() || cf.isExternal()
                                        || n.toLowerCase().contains("d3d")
                                        || n.toLowerCase().contains("direct")
                                        || n.toLowerCase().contains("input")) {
                                    if (calls.indexOf(n) < 0) calls.append(n).append(" ");
                                }
                            }
                        }
                    }
                }
                if (calls.length() > 0) println("        calls: " + calls);
            }
        }
        println("=== end ===");
    }
}
