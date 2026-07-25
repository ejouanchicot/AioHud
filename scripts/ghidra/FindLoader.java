// Finds Hook.dll's plugin-loading code by locating the "GetInterfaceVersion" /
// "CreateInstance" strings, following xrefs to the containing function(s), and
// decompiling them. Reveals the post-CreateInstance vtable call sequence so we
// know which IPlugin slots the host invokes at load (and which must return a
// valid pointer instead of 0).
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*;
import ghidra.program.util.DefinedDataIterator;
import java.util.*;

public class FindLoader extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] needles = {"GetInterfaceVersion", "CreateInstance"};
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        Set<Function> loaders = new LinkedHashSet<>();
        for (String n : needles) {
            // scan all occurrences of the ASCII bytes in memory
            Address start = currentProgram.getMinAddress();
            while (start != null) {
                Address hit = findBytes(start, n);
                if (hit == null) break;
                println("string \"" + n + "\" @ " + hit);
                // references can point at the string addr; also scan instructions that load this address
                for (Reference r : rm.getReferencesTo(hit)) {
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    println("   xref from " + r.getFromAddress() + "  in " +
                            (f != null ? f.getName() + " @ " + f.getEntryPoint() : "(no func)"));
                    if (f != null) loaders.add(f);
                }
                start = hit.add(1);
            }
        }

        for (Function f : loaders) {
            println("\n===== decompile " + f.getName() + " @ " + f.getEntryPoint() + " =====");
            DecompileResults res = di.decompileFunction(f, 60, monitor);
            if (res != null && res.getDecompiledFunction() != null) {
                String[] lines = res.getDecompiledFunction().getC().split("\n");
                for (String ln : lines) println(ln);
            }
        }
        di.dispose();
        println("\n=== end ===");
    }
}
