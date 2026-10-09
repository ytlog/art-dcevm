package com.github.ytlog.artdcevm.testing;

import android.os.Process;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Comparator;

/** Bounded current-run breadcrumbs; call only at Java suite boundaries. */
public final class TestPhaseLog {
    private static final int MAX_PHASES=64,MAX_BYTES=32768,MAX_FILES=8;
    private final String runId;
    private final int pid=Process.myPid();
    private final File file;
    private final ArrayDeque<String> phases=new ArrayDeque<>();
    private long sequence;
    public TestPhaseLog(String runId) { this(runId,null); }
    public TestPhaseLog(String runId,File filesDirectory) {
        if(runId==null||!runId.matches("[0-9a-f]{32}"))throw new IllegalArgumentException("Malformed test run ID");
        this.runId=runId;
        this.file=filesDirectory==null?null:new File(new File(filesDirectory,"runtime-phases"),runId+".json");
        if(file!=null) { persist(); pruneHistory(); }
    }
    public synchronized void mark(String phase) {
        if(phase==null||phase.length()>64||!phase.matches("[a-z0-9.-]+"))throw new IllegalArgumentException("Malformed phase name");
        Log.i("ArtDcevmPhase","{\"runId\":\""+runId+"\",\"pid\":"+pid+",\"phase\":\""+phase+"\"}");
        phases.addLast("{\"phase\":\""+phase+"\",\"sequence\":"+(sequence++)+",\"threadId\":"+Process.myTid()+"}");
        if(phases.size()>MAX_PHASES)phases.removeFirst();
        persist();
    }
    private void persist() {
        if(file==null)return;
        File temporary=null;
        try {
            File directory=file.getParentFile();
            if(!directory.isDirectory()&&!directory.mkdirs())throw new IOException("Phase directory unavailable");
            StringBuilder json=new StringBuilder("{\"runId\":\"").append(runId).append("\",\"pid\":").append(pid).append(",\"phases\":[");
            boolean comma=false;
            for(String phase:phases) {if(comma)json.append(',');json.append(phase);comma=true;}
            byte[] bytes=json.append("]}\n").toString().getBytes(StandardCharsets.UTF_8);
            if(bytes.length>MAX_BYTES)throw new IOException("Phase file bound exceeded");
            temporary=File.createTempFile(runId+"-",".tmp",directory);
            try(FileOutputStream out=new FileOutputStream(temporary)) {out.write(bytes);out.getFD().sync();}
            if(!temporary.renameTo(file))throw new IOException("Atomic phase publication failed");
        } catch(IOException error) {
            // Keep original suite behavior and logcat when storage is unavailable.
            Log.w("ArtDcevmPhaseFile","Current-run phase publication unavailable",error);
        } finally {if(temporary!=null&&temporary.exists())temporary.delete();}
    }
    private void pruneHistory() {
        File[] entries=file.getParentFile().listFiles();if(entries==null)return;
        ArrayList<File> previous=new ArrayList<>();
        for(File entry:entries)if(!entry.equals(file)&&entry.getName().matches("[0-9a-f]{32}\\.json"))previous.add(entry);
        previous.sort(Comparator.comparingLong(File::lastModified).reversed());
        for(int i=MAX_FILES-1;i<previous.size();++i)previous.get(i).delete();
    }
}
