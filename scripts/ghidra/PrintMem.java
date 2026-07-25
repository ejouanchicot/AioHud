// Print the program's memory blocks (name, start, end, initialized) + image base +
// total instruction/function counts. Sanity-checks that analysis produced code and that
// addresses are where we think. @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.*;
import ghidra.program.model.listing.*;

public class PrintMem extends GhidraScript {
    @Override
    public void run() throws Exception {
        println("IMAGEBASE: " + currentProgram.getImageBase());
        for (MemoryBlock b : currentProgram.getMemory().getBlocks())
            println(String.format("BLOCK %-10s %s - %s  init=%s exec=%s", b.getName(),
                    b.getStart(), b.getEnd(), b.isInitialized(), b.isExecute()));
        long ni = 0; InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) { it.next(); ni++; }
        println("INSTRUCTIONS: " + ni);
        println("FUNCTIONS: " + currentProgram.getFunctionManager().getFunctionCount());
        println("===== end =====");
    }
}
