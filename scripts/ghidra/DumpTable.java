// Print pointer-table entries. args: baseGhidraAddrHex countDec [idx1 idx2 ...]
// Converts dump-base(0x05C00000) stored values to Ghidra addr (+0x0A400000). @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.*;
public class DumpTable extends GhidraScript {
  @Override public void run() throws Exception {
    String[] a=getScriptArgs();
    long base=Long.parseLong(a[0],16); int n=Integer.parseInt(a[1]);
    Memory mem=currentProgram.getMemory();
    long CONV=0x0A400000L;
    if(a.length>2){ for(int i=2;i<a.length;i++){ int idx=Integer.parseInt(a[i],16);
        long v=Integer.toUnsignedLong(mem.getInt(toAddr(base+idx*4L)));
        println(String.format("[0x%x] raw=0x%08x ghidra=0x%08x", idx, v, v+CONV)); }
      println("===== end ====="); return; }
    for(int i=0;i<n;i++){ long v=Integer.toUnsignedLong(mem.getInt(toAddr(base+i*4L)));
      println(String.format("[0x%02x] raw=0x%08x ghidra=0x%08x", i, v, v+CONV)); }
    println("===== end =====");
  }
}
