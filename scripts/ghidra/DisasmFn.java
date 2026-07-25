// Disassemble a function (arg0 hex entry) as raw instructions.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;

public class DisasmFn extends GhidraScript {
    @Override
    public void run() throws Exception {
        long e = Long.parseLong(getScriptArgs()[0], 16);
        Function f = currentProgram.getFunctionManager().getFunctionAt(toAddr(e));
        if (f == null) { println("no func"); return; }
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            println(ins.getAddress() + ":  " + ins.toString());
        }
        println("===== end =====");
    }
}
