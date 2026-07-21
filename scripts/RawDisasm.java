// Dump instructions in an address RANGE (arg0 lo, arg1 hi hex), disassembling if needed.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
public class RawDisasm extends GhidraScript {
  @Override public void run() throws Exception {
    Address lo = toAddr(Long.parseLong(getScriptArgs()[0],16));
    Address hi = toAddr(Long.parseLong(getScriptArgs()[1],16));
    Address a = lo;
    while (a.compareTo(hi) < 0) {
      Instruction ins = currentProgram.getListing().getInstructionAt(a);
      if (ins == null) { disassemble(a); ins = currentProgram.getListing().getInstructionAt(a); }
      if (ins == null) { println(a+":  (undisassembled)"); a = a.add(1); continue; }
      println(a + ":  " + ins.toString());
      a = a.add(ins.getLength());
    }
    println("===== end =====");
  }
}
