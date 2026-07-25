// Scans Hook.dll for virtual-dispatch call sites of the form  CALL [reg + disp]
// with disp a multiple of 4 in [0, 0x84] (the IPlugin vtable range, 34 slots).
// Groups by displacement and by containing function, so the plugin-dispatch
// functions (command, render, packet loops) surface and we can label each slot.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.util.*;

public class FindVtableCalls extends GhidraScript {
    @Override
    public void run() throws Exception {
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        // disp -> list of "func@addr"
        TreeMap<Integer, List<String>> byDisp = new TreeMap<>();

        InstructionIterator it = listing.getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            if (!ins.getFlowType().isCall()) continue;
            if (!ins.getMnemonicString().equalsIgnoreCase("CALL")) continue;
            // operand 0 should be a dynamic memory ref [reg + disp]
            int n = ins.getNumOperands();
            if (n < 1) continue;
            // look for a register base + scalar displacement in the operand objects
            Object[] objs = ins.getOpObjects(0);
            Register base = null;
            Integer disp = null;
            boolean dynamic = false;
            for (Object o : objs) {
                if (o instanceof Register) { base = (Register) o; dynamic = true; }
                else if (o instanceof Scalar) { disp = (int) ((Scalar) o).getUnsignedValue(); }
            }
            // pure register-indirect "call [reg]" => disp 0
            if (dynamic && disp == null) disp = 0;
            if (!dynamic || disp == null) continue;
            if (disp < 0 || disp > 0x84 || (disp % 4) != 0) continue;
            // skip esp/ebp-relative (those are locals, not vtables)
            String bn = base.getName().toLowerCase();
            if (bn.equals("esp") || bn.equals("ebp")) continue;

            Function f = fm.getFunctionContaining(ins.getAddress());
            String fname = (f != null) ? f.getName() : "?";
            byDisp.computeIfAbsent(disp, k -> new ArrayList<>())
                  .add(fname + "@" + ins.getAddress());
        }

        println("=== IPlugin vtable call sites in Hook.dll (disp -> slot) ===");
        for (Map.Entry<Integer, List<String>> e : byDisp.entrySet()) {
            int slot = e.getKey() / 4;
            List<String> sites = e.getValue();
            println(String.format("slot %2d (+0x%02x) : %d site(s)", slot, e.getKey(), sites.size()));
            // print up to 4 sites so we can locate the dispatcher
            for (int i = 0; i < Math.min(4, sites.size()); i++) println("        " + sites.get(i));
        }
        println("=== end ===");
    }
}
