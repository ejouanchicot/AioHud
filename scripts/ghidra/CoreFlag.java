// Locate the "...manually unloaded" message and find the code that uses it by
// scanning instructions for an operand pointing near the string, then decompile
// those functions to see which IPlugin field/vtable call marks a plugin "core".
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.util.*;

public class CoreFlag extends GhidraScript {
    @Override
    public void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // window around the message string (cover a possible "%s - Core ..." start)
        long lo = 0x10183a80L, hi = 0x10183b40L;
        Set<Function> fns = new LinkedHashSet<>();

        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        int scanned = 0;
        while (it.hasNext()) {
            Instruction ins = it.next();
            scanned++;
            for (int op = 0; op < ins.getNumOperands(); op++) {
                for (Object o : ins.getOpObjects(op)) {
                    long v = -1;
                    if (o instanceof Scalar) v = ((Scalar) o).getUnsignedValue();
                    else if (o instanceof Address) v = ((Address) o).getOffset();
                    if (v >= lo && v <= hi) {
                        Function f = fm.getFunctionContaining(ins.getAddress());
                        println("operand 0x" + Long.toHexString(v) + " @ " + ins.getAddress() +
                                " in " + (f != null ? f.getName()+"@"+f.getEntryPoint() : "?"));
                        if (f != null) fns.add(f);
                    }
                }
            }
        }
        println("scanned " + scanned + " instructions");
        for (Function f : fns) {
            println("\n===== " + f.getName() + " @ " + f.getEntryPoint() + " =====");
            DecompileResults res = di.decompileFunction(f, 60, monitor);
            if (res != null && res.getDecompiledFunction() != null)
                for (String ln : res.getDecompiledFunction().getC().split("\n")) println(ln);
        }
        di.dispose();
        println("\n=== end ===");
    }
}
