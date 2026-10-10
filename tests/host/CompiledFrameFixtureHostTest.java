package com.github.ytlog.artdcevm.internal;
public final class CompiledFrameFixtureHostTest {
 private static native boolean initialize();
 private static native boolean blocked();
 private static native void failWait(boolean value);
 private static native void describe(Throwable original);
 private static native String diagnostic();
 private static final class BrokenMessage extends RuntimeException {
  @Override public String getMessage(){throw new AssertionError("secondary diagnostic failure");}
 }
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
  CompiledFrameFixture.Run failed=CompiledFrameFixture.prepareCold();failWait(true);
  try{failed.start();throw new AssertionError("start Throwable swallowed");}
  catch(OutOfMemoryError original){if(!original.getMessage().equals("host start Throwable after worker creation"))throw original;}
  finally{failWait(false);failed.close();}
  if(blocked())throw new AssertionError("Error left private worker/gate armed");
  Throwable[] exceptions={new IllegalStateException("worker failed",new UnsatisfiedLinkError("native bridge unavailable")),new BrokenMessage()};
  for(Throwable original:exceptions){try{describe(original);throw new AssertionError("pending Throwable lost");}catch(Throwable caught){if(caught!=original)throw new AssertionError("pending Throwable identity changed",caught);}}
  if(!diagnostic().contains("BrokenMessage")||!diagnostic().contains("diagnostic accessor threw"))throw new AssertionError(diagnostic());
  Throwable cause=new IllegalStateException("worker failed",new UnsatisfiedLinkError("native bridge unavailable"));
  try{describe(cause);}catch(Throwable caught){if(caught!=cause)throw new AssertionError(caught);}
  if(!diagnostic().contains("compiled-fixture.prepare.warmup")||!diagnostic().contains("java.lang.IllegalStateException: worker failed")||!diagnostic().contains("java.lang.UnsatisfiedLinkError: native bridge unavailable"))throw new AssertionError(diagnostic());
  System.out.println("Pending Throwable class/message/cause and throwing diagnostic accessor preserve exact identity; Error after worker creation releases/joins gate");
  System.out.println("Actual Java/native fixture: cold plus two warm/start/blocked/duplicate rejection/release/join/value/reference lifecycles passed; host JNI only");
 }
}
