// Decompile an address, CREATING the function first if Ghidra didn't (LAB_ targets of
// lua_pushcclosure are real functions but sometimes only labelled).
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;

public class DecompForce extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String arg : getScriptArgs()) {
            long a = Long.parseLong(arg, 16);
            Address ad = toAddr(a);
            Function f = currentProgram.getFunctionManager().getFunctionAt(ad);
            if (f == null) {
                disassemble(ad);
                CreateFunctionCmd cmd = new CreateFunctionCmd(ad);
                cmd.applyTo(currentProgram, monitor);
                f = currentProgram.getFunctionManager().getFunctionAt(ad);
            }
            println(">>>>> " + ad + "  " + (f != null ? f.getSignature().getPrototypeString() : "(FAILED)"));
            if (f != null) {
                DecompInterface di = new DecompInterface();
                di.openProgram(currentProgram);
                DecompileResults res = di.decompileFunction(f, 120, monitor);
                if (res != null && res.getDecompiledFunction() != null)
                    for (String ln : res.getDecompiledFunction().getC().split("\n")) println(ln);
                di.dispose();
            }
            println("===== end " + ad + " =====");
        }
    }
}
