package com.github.ytlog.artdcevm.internal;
public final class CompiledFrameFixtureHostTest {
 private static native boolean initialize();
 private static native boolean blocked();
 public static void main(String[] args) throws Exception {
  System.load(args[0]);
  if(!initialize()||!initialize())throw new AssertionError("named bridge initialization");
  for(int round=0;round<3;++round) {
   CompiledFrameFixture.Run run=round==0?CompiledFrameFixture.prepareCold():CompiledFrameFixture.prepare();
   if(blocked())throw new AssertionError("warmup was armed");
   run.start();
   if(!blocked())throw new AssertionError("actual JNI worker was not blocked");
   try{run.start();throw new AssertionError("duplicate start accepted");}catch(IllegalStateException expected){}
   run.close();run.close();
   if(blocked())throw new AssertionError("worker survived close");
  }
  System.out.println("Actual Java/native fixture: cold plus two warm/start/blocked/duplicate rejection/release/join/value/reference lifecycles passed; host JNI only");
 }
}
