// Scan initialized data blocks for a run of pointers into .text using the DUMP-TIME base.
// Dump base ~0x05C00000 (pointers = 0x05C00000 + RVA). @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
public class FindPtrTable extends GhidraScript {
  @Override public void run() throws Exception {
    long minN = getScriptArgs().length>0 ? Long.parseLong(getScriptArgs()[0]) : 60;
    long B = 0x05C00000L;
    long textLo = B+0x1000, textHi = B+0x32816dL;
    Memory mem = currentProgram.getMemory();
    for (MemoryBlock b : mem.getBlocks()) {
      if (!b.isInitialized() || b.isExecute()) continue;
      long start = b.getStart().getOffset(), end = b.getEnd().getOffset()-3;
      long runStart=-1; int run=0, text=0;
      for (long a=start; a<end; a+=4) {
        long v; try { v=Integer.toUnsignedLong(mem.getInt(toAddr(a))); } catch(Exception e){ v=-1; }
        boolean isText=(v>=textLo && v<=textHi), isNull=(v==0);
        if (isText||isNull){ if(run==0){runStart=a;text=0;} run++; if(isText)text++; }
        else { if(text>=minN) println(String.format("TABLE @0x%08x len=%d text=%d",runStart,run,text)); run=0; text=0; }
      }
      if(text>=minN) println(String.format("TABLE @0x%08x len=%d text=%d",runStart,run,text));
    }
    println("===== end =====");
  }
}
