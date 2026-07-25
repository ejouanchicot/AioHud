// Find indirect CALL/JMP through a scaled memory operand [reg*4 + base] and report base + fn.
// @category Windower
import ghidra.app.script.GhidraScript; import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar; import ghidra.program.model.address.Address;
public class FindJmpTables extends GhidraScript {
  @Override public void run() throws Exception {
    Listing l=currentProgram.getListing(); var fm=currentProgram.getFunctionManager();
    InstructionIterator it=l.getInstructions(true); int n=0;
    while(it.hasNext()){ Instruction ins=it.next(); String m=ins.getMnemonicString();
      if(!m.equals("CALL")&&!m.equals("JMP")) continue;
      String s=ins.toString();
      if(!s.contains("*0x4")&&!s.contains("*4")) continue;
      // find a scalar >=0x10000000 in the operand
      long base=0; for(int op=0;op<ins.getNumOperands();op++) for(Object o:ins.getOpObjects(op)){
        if(o instanceof Scalar){ long v=((Scalar)o).getUnsignedValue(); if(v>=0x05c01000L&&v<=0x06600000L) base=v; }
        if(o instanceof Address){ long v=((Address)o).getOffset(); if(v>=0x05c01000L&&v<=0x06600000L) base=v; } }
      Function f=fm.getFunctionContaining(ins.getAddress());
      println(String.format("%s  %-40s base=0x%08x fn=%s", ins.getAddress(), s, base, f!=null?f.getEntryPoint():"?"));
      n++;
    }
    println("===== end ("+n+") =====");
  }
}
