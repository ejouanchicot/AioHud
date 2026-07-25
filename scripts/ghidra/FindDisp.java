// Find every function that contains an instruction operand equal to a given
// constant/displacement (arg0 hex). Used to locate all code touching the plugin
// list (e.g. 0xa14a8) so the command dispatcher surfaces.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;

public class FindDisp extends GhidraScript {
    @Override
    public void run() throws Exception {
        long disp = Long.parseLong(getScriptArgs()[0], 16);
        Listing l = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        java.util.LinkedHashSet<String> seen = new java.util.LinkedHashSet<>();
        InstructionIterator it = l.getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            for (int op = 0; op < ins.getNumOperands(); op++) {
                for (Object o : ins.getOpObjects(op)) {
                    if (o instanceof Scalar && ((Scalar) o).getUnsignedValue() == disp) {
                        Function f = fm.getFunctionContaining(ins.getAddress());
                        String fn = (f != null) ? (f.getName() + " @" + f.getEntryPoint()) : ("? @" + ins.getAddress());
                        seen.add(fn);
                    }
                }
            }
        }
        for (String s : seen) println("USES : " + s);
        println("===== end =====");
    }
}
