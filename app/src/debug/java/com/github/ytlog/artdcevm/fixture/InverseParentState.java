package com.github.ytlog.artdcevm.fixture;

import java.lang.invoke.*;
import java.lang.reflect.*;

/** Behavioral contract shared by the pinned JBR oracle and device instrumentation. */
public final class InverseParentState {
    public interface Check { void eq(String name, Object expected, Object actual) throws Exception; }
    public interface Call { Object run() throws Throwable; }
    public static Object result(Call call) {
        try { return call.run(); }
        catch (Throwable error) {
            while (error instanceof InvocationTargetException) error = error.getCause();
            return error.getClass().getName();
        }
    }
    public final Class<?>[] children = {InverseParentFixture.PrivateDefault.class, InverseParentFixture.PrivateAbstract.class,
            InverseParentFixture.StaticDefault.class, InverseParentFixture.StaticAbstract.class};
    public final Class<?>[] parents = {InverseParentFixture.DefaultParent.class, InverseParentFixture.AbstractParent.class};
    public final Object[] receivers = {new InverseParentFixture.PrivateDefaultImpl(), new InverseParentFixture.PrivateAbstractImpl(),
            new InverseParentFixture.StaticDefaultImpl(), new InverseParentFixture.StaticAbstractImpl()};
    public final Method[] own = new Method[4], locals = new Method[4];
    private final MethodHandle[] handles = new MethodHandle[4], parentHandles = new MethodHandle[2];
    private final Object[] proxies = new Object[4];
    private final int[] identities = new int[4];
    private Object late;
    private final InverseParentFixture.Shared shared = new InverseParentFixture.Shared();
    private final InverseParentFixture.Peer peer = new InverseParentFixture.Peer();
    public InverseParentState() throws Exception {
        for (int i=0;i<4;i++) {
            own[i]=children[i].getDeclaredMethod("valueX"); own[i].setAccessible(true);
            locals[i]=children[i].getMethod("local");
            handles[i]=MethodHandles.lookup().unreflect(own[i]);
            identities[i]=System.identityHashCode(receivers[i]);
            proxies[i]=Proxy.newProxyInstance(children[i].getClassLoader(),new Class<?>[]{children[i]},(p,m,a)->23);
        }
        for(int i=0;i<2;i++) parentHandles[i]=MethodHandles.publicLookup().findVirtual(parents[i],"valueX",MethodType.methodType(int.class));
    }
    public final class Active implements AutoCloseable {
        final java.util.concurrent.CountDownLatch ready=new java.util.concurrent.CountDownLatch(4),resume=new java.util.concurrent.CountDownLatch(1);
        final Object[] results=new Object[4]; final Thread[] threads=new Thread[4];
        Active() throws Exception {
            for(int i=0;i<4;i++) {
                final int k=i; final Method method=children[i].getMethod("await",java.util.concurrent.CountDownLatch.class,java.util.concurrent.CountDownLatch.class);
                threads[i]=new Thread(()->results[k]=result(()->method.invoke(receivers[k],ready,resume)));
                threads[i].setDaemon(true); threads[i].start();
            }
            if(!ready.await(5,java.util.concurrent.TimeUnit.SECONDS)){close();throw new AssertionError("active frames not ready");}
        }
        public void close() throws InterruptedException {
            resume.countDown();for(Thread thread:threads)if(thread!=null)thread.join(5000);
        }
        public Object observation(int i) { return results[i]; }
        public void verify(Check check) throws Exception {
            for(int i=0;i<4;i++) {check.eq("active"+i,11,results[i]);check.eq("joined"+i,true,!threads[i].isAlive());}
        }
    }
    public Active startActive() throws Exception { return new Active(); }
    public Object cachedHandleResult(int k) {
        return result(()->k<2?handles[k].invokeWithArguments(receivers[k]):handles[k].invokeWithArguments());
    }
    public Class<?>[] targets(boolean reverse) {
        Class<?>[] targets={children[0],children[1],children[2],children[3],parents[0],InverseParentFixture.Peer.class};
        if(reverse) for(int i=0;i<targets.length/2;i++){Class<?> t=targets[i];targets[i]=targets[targets.length-1-i];targets[targets.length-1-i]=t;}
        return targets;
    }
    public void verify(Check check,String prefix,boolean changed) throws Exception {
        verify(check,prefix,changed,true);
    }
    // The pinned JBR aliases some cached reflection slots after adding mirandas.
    // Measure that reference-VM defect separately; never require unsafe aliasing on ART.
    public void verify(Check check,String prefix,boolean changed,boolean checkCachedReflection) throws Exception {
        MethodType type=MethodType.methodType(int.class);
        for(int i=0;i<4;i++) {
            final int k=i; final Object receiver=receivers[i]; final Class<?> child=children[i],parent=parents[i%2];
            String key=prefix+i+".";
            Object expected=changed?(i%2==0?9:"java.lang.AbstractMethodError"):"java.lang.ClassCastException";
            check.eq(key+"local",11,InverseParentFixture.local(receiver,i));
            if(checkCachedReflection) check.eq(key+"cachedOwn",11,result(()->own[k].invoke(receiver)));
            if(checkCachedReflection) check.eq(key+"cachedHandle",11,result(()->k<2?handles[k].invokeWithArguments(receiver):handles[k].invokeWithArguments()));
            Method fresh=child.getDeclaredMethod("valueX");fresh.setAccessible(true);
            check.eq(key+"freshOwn",11,result(()->fresh.invoke(receiver)));
            check.eq(key+"parent",expected,result(()->InverseParentFixture.parent(receiver,k)));
            check.eq(key+"parentHandle",expected,result(()->parentHandles[k%2].invokeWithArguments(receiver)));
            check.eq(key+"parentInstance",changed,parent.isInstance(receiver));
            check.eq(key+"identity",identities[i],System.identityHashCode(receiver));
            check.eq(key+"marker",31,receiver.getClass().getField("marker").get(receiver));
            check.eq(key+"freshVirtual","java.lang.IllegalAccessException",result(()->MethodHandles.publicLookup().findVirtual(child,"valueX",type)));
            check.eq(key+"freshStatic",i<2?"java.lang.IllegalAccessException":11,result(()->MethodHandles.publicLookup().findStatic(child,"valueX",type).invokeWithArguments()));
            check.eq(key+"proxyParent",expected,result(()->InverseParentFixture.parent(proxies[k],k)));
            check.eq(key+"proxyLocal",23,locals[i].invoke(proxies[i]));
            check.eq(key+"nullLocal","java.lang.NullPointerException",result(()->InverseParentFixture.local(null,k)));
        }
        if(late==null&&changed)late=Class.forName("com.github.ytlog.artdcevm.fixture.InverseParentFixture$Late").getConstructor().newInstance();
        if(late!=null) {
            check.eq(prefix+"lateLocal",11,InverseParentFixture.local(late,0));
            check.eq(prefix+"lateParent",changed?9:"java.lang.ClassCastException",result(()->InverseParentFixture.parent(late,0)));
        }
        check.eq(prefix+"sharedParent",changed?9:7,InverseParentFixture.parent(shared,0));
        check.eq(prefix+"sharedLocal",11,shared.local());
        check.eq(prefix+"peer",changed?5:3,peer.value());
    }
}
