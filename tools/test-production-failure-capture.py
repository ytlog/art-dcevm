#!/usr/bin/env python3
"""Exercise the real Java diagnostic helper with independent JSON/JNI boundaries."""
import pathlib, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    base=pathlib.Path(tmp); json=base/'org/json'; json.mkdir(parents=True)
    (json/'JSONObject.java').write_text('''package org.json;
import java.util.HashMap;
public class JSONObject {
 public final HashMap<String,Object> values=new HashMap<>();
 public JSONObject(){} public JSONObject(String s){if(!s.startsWith("{"))throw new IllegalArgumentException("invalid JSON boundary"); values.put("raw",s);}
 public JSONObject put(String key,Object value){values.put(key,value);return this;}
}''')
    package=base/'com/github/ytlog/artdcevm/testing'; package.mkdir(parents=True)
    source=root/'app/src/debug/java/com/github/ytlog/artdcevm/testing/ProductionFailureCapture.java'
    (package/source.name).write_text(source.read_text())
    (package/'CaptureTest.java').write_text('''package com.github.ytlog.artdcevm.testing;
import org.json.JSONObject;
public class CaptureTest {
 static final Throwable ORIGINAL=new NoClassDefFoundError("original boot failure");
 static void scenario(JSONObject report,ProductionFailureCapture.Probe probe)throws Throwable {
  try{throw ORIGINAL;}catch(Throwable error){ProductionFailureCapture.record(report,error,probe);throw error;}
 }
 public static void main(String[]args){
  for(int mode=0;mode<3;mode++){
   final int m=mode; JSONObject report=new JSONObject();Object old=new Object();report.put("capabilities",old);
   try{scenario(report,new ProductionFailureCapture.Probe(){
    public String capabilities(){if(m==1)throw new LinkageError("diagnostic failure");return m==2?"bad":"{fresh:true}";}
    public String errorName(int code){if(code!=98)throw new AssertionError();if(m==1)throw new LinkageError("name failure");return "JVMTI_ERROR_NOT_AVAILABLE";}
   });throw new AssertionError("original swallowed");}catch(Throwable error){if(error!=ORIGINAL)throw new AssertionError("original replaced",error);}
   if(report.values.get("capabilities")!=old||!"java.lang.NoClassDefFoundError".equals(report.values.get("failureType")))throw new AssertionError("old snapshot/identity changed");
   if(m==0&&!report.values.containsKey("capabilitiesAtFailure"))throw new AssertionError("fresh report absent");
   if(m!=0&&!report.values.containsKey("failureCapabilitiesDiagnostic"))throw new AssertionError("diagnostic error absent");
   if(m==1&&!report.values.containsKey("failureNativeErrorDiagnostic"))throw new AssertionError("error-name failure absent");
   if(report.values.containsKey("lastApplyCode"))throw new AssertionError("invented apply result");
  }
  System.out.println("PASS original Throwable identity, stale snapshot retention, fresh probe, failing probe/JSON, no fabricated apply result");
 }
}''')
    subprocess.run(['javac','-d',str(base),str(json/'JSONObject.java'),str(package/source.name),str(package/'CaptureTest.java')],check=True)
    subprocess.run(['java','-cp',str(base),'com.github.ytlog.artdcevm.testing.CaptureTest'],check=True)
