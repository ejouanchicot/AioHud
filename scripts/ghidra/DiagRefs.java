// Diagnostic: print the first N instructions that have an outgoing reference whose target
// is >= 0x10300000 (i.e. into .rdata/.data), showing the instruction text, the ref type and
// the target. Tells us how Ghidra represents global-data accesses in this program.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class DiagRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        int want = 25, n = 0;
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext() && n < want) {
            Instruction ins = it.next();
            for (Reference r : ins.getReferencesFrom()) {
                long to = r.getToAddress().getOffset();
                if (to >= 0x10329000L && to <= 0x109c6000L) {
                    println(String.format("REF @%s  %-32s -> 0x%08X  (%s)", ins.getAddress(), ins.toString(), to, r.getReferenceType()));
                    n++; break;
                }
            }
        }
        println("===== end (" + n + " shown) =====");
    }
}
