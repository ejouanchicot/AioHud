// Find every instruction whose operand (scalar immediate OR address reference) falls in
// [arg0, arg1] hex. Used to locate the code that loads a base pointer into a register for a
// computed-access global block (e.g. lea reg,[0x1063xxxx]) when no single offset/address hits.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;

public class FindImmRange extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        long lo = Long.parseLong(a[0], 16);
        long hi = Long.parseLong(a[1], 16);
        Listing l = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        java.util.LinkedHashSet<String> out = new java.util.LinkedHashSet<>();
        InstructionIterator it = l.getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            boolean hit = false; long val = 0;
            for (int op = 0; op < ins.getNumOperands() && !hit; op++) {
                for (Object o : ins.getOpObjects(op)) {
                    if (o instanceof Scalar) {
                        long v = ((Scalar) o).getUnsignedValue();
                        if (v >= lo && v <= hi) { hit = true; val = v; break; }
                    } else if (o instanceof Address) {
                        long v = ((Address) o).getOffset();
                        if (v >= lo && v <= hi) { hit = true; val = v; break; }
                    }
                }
            }
            if (!hit) {  // also catch references Ghidra attached to the instruction
                for (Reference r : ins.getReferencesFrom()) {
                    long v = r.getToAddress().getOffset();
                    if (v >= lo && v <= hi) { hit = true; val = v; break; }
                }
            }
            if (hit) {
                Function f = fm.getFunctionContaining(ins.getAddress());
                String fn = (f != null) ? (f.getName() + " @" + f.getEntryPoint()) : "?";
                out.add(String.format("0x%08X  %-40s  %s   in %s", val, ins.toString(), ins.getAddress(), fn));
            }
        }
        for (String s : out) println("IMM: " + s);
        println("===== end (" + out.size() + " hits) =====");
    }
}
