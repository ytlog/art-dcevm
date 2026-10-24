package com.github.ytlog.artdcevm.fixture;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/** Shared symbolic-resolution assertions for the pinned JBR and ART. */
public final class InterfaceSymbolState {
    public interface Check { void eq(String name, Object expected, Object actual) throws Exception; }
    public interface Call { Object run() throws Throwable; }
    public static Object result(Call call) {
        try { return call.run(); }
        catch (Throwable error) {
            while (error instanceof InvocationTargetException) error = error.getCause();
            return error.getClass().getName();
        }
    }
    public final InterfaceSymbolFixture.Receiver receiver = new InterfaceSymbolFixture.Receiver();
    public final InterfaceSymbolFixture.Lost lost = new InterfaceSymbolFixture.Lost();
    public final InterfaceSymbolFixture.Override override = new InterfaceSymbolFixture.Override();
    public final InterfaceSymbolFixture.Peer peer = new InterfaceSymbolFixture.Peer();
    private final int identity = System.identityHashCode(receiver);
    private final Class<?> child = InterfaceSymbolFixture.Child.class, parent = InterfaceSymbolFixture.Parent.class;
    public final Method cachedChild = child.getMethod("value"), cachedParent = parent.getMethod("value");
    private final MethodHandle childHandle = MethodHandles.publicLookup().findVirtual(child, "value", MethodType.methodType(int.class));
    private final MethodHandle parentHandle = MethodHandles.publicLookup().unreflect(cachedParent);
    private final MethodHandle classHandle = MethodHandles.publicLookup().findVirtual(InterfaceSymbolFixture.Lost.class, "value", MethodType.methodType(int.class));
    private final MethodHandle parentEcho = MethodHandles.publicLookup().findVirtual(parent,"echo",MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class));
    private final MethodHandle bodyErrorHandle = MethodHandles.publicLookup().findVirtual(parent, "badArgument", MethodType.methodType(int.class));
    public static class OrdinaryLookup {
        public InterfaceSymbolFixture.Result objectValue() { return null; }
        public int echo(InterfaceSymbolFixture.Result value) { return 7; }
        public static InterfaceSymbolFixture.Result staticObject(InterfaceSymbolFixture.Result value) { return value; }
        public static int staticEcho(InterfaceSymbolFixture.Result value) { return value == null ? 41 : 29; }
        private static int privateStatic() { return 67; }
        protected static int protectedStatic() { return 71; }
        public static int throwingStatic() { throw new IllegalStateException(); }
    }
    static class HiddenStaticBase { public static int publicStatic() { return 73; } }
    public static class PublicStaticChild extends HiddenStaticBase { }
    public interface StaticInterface { static int interfaceOnly() { return 79; } }
    public static class StaticImplementor implements StaticInterface { }
    public interface StaticChildInterface extends StaticInterface {
        static int childOnly() { return InterfaceSymbolFixture.Peer.staticValue(); }
    }
    public interface StaticLeafInterface extends StaticChildInterface { }
    public interface StaticOtherInterface extends StaticInterface { }
    public interface StaticDiamond extends StaticLeafInterface, StaticOtherInterface { }
    public interface StaticReverse extends StaticOtherInterface, StaticLeafInterface { }
    public static class StaticLeafImplementor extends PeerStaticChild implements StaticDiamond { }
    public static class StaticOwnImplementor extends StaticLeafImplementor {
        public static int interfaceOnly() { return 101; }
    }
    public interface BootComparator extends java.util.Comparator<String> { }
    public interface BootFunction extends java.util.function.Function<String,String> { }
    public interface BootCombined extends BootComparator, BootFunction { }
    public interface BootReverse extends BootFunction, BootComparator { }
    public static class BootReceiver extends PeerStaticChild implements BootCombined, BootReverse {
        public int compare(String a, String b) { return a.compareTo(b); }
        public String apply(String value) { return new InterfaceSymbolFixture.Peer().covariant(false); }
    }
    public static class BootSubclass extends BootReceiver { }
    public static class BootHiding extends BootReceiver { public static int naturalOrder() { return 107; } }
    public interface BootOwn extends BootFunction { static String identity() { return "own"; } }
    public interface BootSpecific extends java.util.function.Supplier<String>, java.util.function.IntSupplier, java.util.Comparator<String> {
        @Override default String get() { return new InterfaceSymbolFixture.Peer().covariant(false); }
        @Override default int getAsInt() { return InterfaceSymbolFixture.Peer.staticValue(); }
        @Override default int compare(String a, String b) { return a.compareTo(b); }
        @Override default java.util.Comparator<String> reversed() { return (a,b) -> 109; }
    }
    public interface BootSpecificLeaf extends BootSpecific { }
    public interface BootSpecificOther extends java.util.function.Supplier<String> { }
    public interface BootSpecificDiamond extends BootSpecificLeaf, BootSpecificOther { }
    public interface BootSpecificReverse extends BootSpecificOther, BootSpecificLeaf { }
    public static class BootSpecificReceiver implements BootSpecificDiamond, BootSpecificReverse { }
    public static class BootSpecificSubclass extends BootSpecificReceiver { }
    public static class BootSpecificOwn extends BootSpecificReceiver {
        @Override public String get() { return "own"; }
        @Override public int getAsInt() { return 113; }
        @Override public java.util.Comparator<String> reversed() { return (a,b) -> 127; }
    }
    private final Method[] bootOverrideCache = new Method[8];
    private void verifyBootOverride(Check check, String prefix, boolean changed) throws Exception {
        Class<?>[] owners = {BootSpecific.class,BootSpecificLeaf.class,BootSpecificDiamond.class,
                BootSpecificReverse.class,BootSpecificReceiver.class,BootSpecificSubclass.class,BootSpecificOwn.class};
        for (int i=0;i<owners.length;++i) {
            boolean own = i==6; Object receiver = own ? new BootSpecificOwn() : new BootSpecificReceiver();
            Class<?> expectedOwner = own ? BootSpecificOwn.class : BootSpecific.class;
            Method[] methods = {enumerated(owners[i],"get",Object.class),enumerated(owners[i],"get",String.class),
                    enumerated(owners[i],"getAsInt",int.class),enumerated(owners[i],"reversed",java.util.Comparator.class)};
            for (int n=0;n<methods.length;++n) {
                Method m=methods[n];String key=prefix+"bootOverride"+i+"_"+n;
                check.eq(key+"Owner",true,m.getDeclaringClass()==expectedOwner);
                check.eq(key+"Bridge",n==0,m.isBridge());
                check.eq(key+"Synthetic",n==0,m.isSynthetic());
                check.eq(key+"Default",!own,m.isDefault());
                Object expected = n<2 ? (own ? "own" : changed ? "after" : "before") : n==2 ? (own ? 113 : changed ? 61 : 59) : (own ? 127 : 109);
                Object value=m.invoke(receiver);
                check.eq(key+"Call",expected,n==3 ? ((java.util.Comparator<String>)value).compare("a","b") : value);
                if (i==4) {
                    if(bootOverrideCache[n]==null)bootOverrideCache[n]=m;
                    Object cached=bootOverrideCache[n].invoke(receiver);
                    check.eq(key+"Cached",expected,n==3 ? ((java.util.Comparator<String>)cached).compare("a","b") : cached);
                }
            }
        }
        check.eq(prefix+"bootOverrideNull","java.lang.NullPointerException",result(() -> bootOverrideCache[0].invoke(null)));
        Method original=enumerated(java.util.function.Supplier.class,"get",Object.class);
        check.eq(prefix+"bootOverrideBootOwner",true,original.getDeclaringClass()==java.util.function.Supplier.class);
        check.eq(prefix+"bootOverrideBootBridge",false,original.isBridge());
        check.eq(prefix+"bootOverrideBootCall",changed ? "after" : "before",original.invoke(new BootSpecificReceiver()));
        check.eq(prefix+"bootOverrideDeclared",0,BootSpecificLeaf.class.getDeclaredMethods().length);
    }
    public interface BootList extends java.util.List<String> { }
    public interface BootListLeaf extends BootList { }
    public static class BootListReceiver extends java.util.AbstractList<String> implements BootListLeaf {
        public int size() { return 1; }
        public String get(int i) { if(i!=0)throw new IndexOutOfBoundsException();return new InterfaceSymbolFixture.Peer().covariant(false); }
    }
    public interface BootSorted extends java.util.SortedSet<String> { }
    public interface BootSortedLeaf extends BootSorted { }
    public static class BootSortedReceiver extends java.util.AbstractSet<String> implements BootSortedLeaf {
        private java.util.SortedSet<String> values() { return new java.util.TreeSet<String>(java.util.Collections.singleton(new InterfaceSymbolFixture.Peer().covariant(false))); }
        public int size() { return 1; }
        public java.util.Iterator<String> iterator() { return values().iterator(); }
        public java.util.Comparator<? super String> comparator() { return null; }
        public java.util.SortedSet<String> subSet(String a,String b) { return values().subSet(a,b); }
        public java.util.SortedSet<String> headSet(String a) { return values().headSet(a); }
        public java.util.SortedSet<String> tailSet(String a) { return values().tailSet(a); }
        public String first() { return values().first(); }
        public String last() { return values().last(); }
    }
    public interface BootPath extends java.nio.file.Path { }
    public interface BootPathLeaf extends BootPath { }
    public abstract static class BootPathBase implements BootPathLeaf { }
    public static class BootPathReceiver extends BootPathBase {
        private final java.nio.file.Path path=java.nio.file.Paths.get("sample");
        public java.nio.file.FileSystem getFileSystem(){return path.getFileSystem();}
        public boolean isAbsolute(){return path.isAbsolute();}
        public java.nio.file.Path getRoot(){return path.getRoot();}
        public java.nio.file.Path getFileName(){return path.getFileName();}
        public java.nio.file.Path getParent(){return path.getParent();}
        public int getNameCount(){return path.getNameCount();}
        public java.nio.file.Path getName(int n){return path.getName(n);}
        public java.nio.file.Path subpath(int a,int b){return path.subpath(a,b);}
        public boolean startsWith(java.nio.file.Path p){return path.startsWith(p);}
        public boolean startsWith(String p){return path.startsWith(p);}
        public boolean endsWith(java.nio.file.Path p){return path.endsWith(p);}
        public boolean endsWith(String p){return path.endsWith(p);}
        public java.nio.file.Path normalize(){return path.normalize();}
        public java.nio.file.Path resolve(java.nio.file.Path p){return path.resolve(p);}
        public java.nio.file.Path resolve(String p){return path.resolve(p);}
        public java.nio.file.Path resolveSibling(java.nio.file.Path p){return path.resolveSibling(p);}
        public java.nio.file.Path resolveSibling(String p){return path.resolveSibling(p);}
        public java.nio.file.Path relativize(java.nio.file.Path p){return path.relativize(p);}
        public java.net.URI toUri(){return path.toUri();}
        public java.nio.file.Path toAbsolutePath(){return path.toAbsolutePath();}
        public java.nio.file.Path toRealPath(java.nio.file.LinkOption... o) throws java.io.IOException{return path.toRealPath(o);}
        public java.io.File toFile(){return path.toFile();}
        public java.nio.file.WatchKey register(java.nio.file.WatchService w,java.nio.file.WatchEvent.Kind<?>[] k,java.nio.file.WatchEvent.Modifier... m) throws java.io.IOException{return path.register(w,k,m);}
        public java.nio.file.WatchKey register(java.nio.file.WatchService w,java.nio.file.WatchEvent.Kind<?>... k) throws java.io.IOException{return path.register(w,k);}
        public java.util.Iterator<java.nio.file.Path> iterator(){return path.iterator();}
        public int compareTo(java.nio.file.Path p){if(p==null)throw new NullPointerException();return InterfaceSymbolFixture.Peer.staticValue();}
    }
    private final Method[] bootDefaultCache=new Method[9];
    private void verifyBootDefaults(Check check,String prefix,boolean changed) throws Exception {
        Class<?>[] owners={BootList.class,BootListLeaf.class,BootListReceiver.class,BootSorted.class,BootSortedLeaf.class,BootSortedReceiver.class,BootPath.class,BootPathLeaf.class,BootPathBase.class};
        for(int i=0;i<owners.length;++i){
            boolean path=i>=6;Class<?> expectedOwner=path?java.nio.file.Path.class:i>=3?java.util.SortedSet.class:java.util.List.class;
            Method m=path?enumerated(owners[i],"compareTo",int.class,Object.class):enumerated(owners[i],"spliterator",java.util.Spliterator.class);
            String key=prefix+"bootDefault"+i;
            check.eq(key+"Owner",true,m.getDeclaringClass()==expectedOwner);
            check.eq(key+"Bridge",path,m.isBridge());check.eq(key+"Synthetic",path,m.isSynthetic());check.eq(key+"Default",true,m.isDefault());
            Object receiver=path?new BootPathReceiver():i>=3?new BootSortedReceiver():new BootListReceiver();
            if(bootDefaultCache[i]==null)bootDefaultCache[i]=m;
            for(int cached=0;cached<2;++cached){
                Method call=cached==0?m:bootDefaultCache[i];Object actual;
                if(path)actual=call.invoke(receiver,java.nio.file.Paths.get("target"));
                else {java.util.ArrayList<String> values=new java.util.ArrayList<>();((java.util.Spliterator<String>)call.invoke(receiver)).forEachRemaining(values::add);actual=values.toString();}
                check.eq(key+(cached==0?"Call":"Cached"),path?(Object)(changed?61:59):changed?"[after]":"[before]",actual);
            }
        }
        Method bridge=enumerated(BootPathBase.class,"compareTo",int.class,Object.class);
        check.eq(prefix+"bootDefaultNull","java.lang.NullPointerException",result(() -> bridge.invoke(new BootPathReceiver(),new Object[]{null})));
        check.eq(prefix+"bootDefaultWrongType","java.lang.ClassCastException",result(() -> bridge.invoke(new BootPathReceiver(),"wrong")));
        check.eq(prefix+"bootDefaultClassPriority",true,enumerated(BootPathReceiver.class,"compareTo",int.class,Object.class).getDeclaringClass()==BootPathReceiver.class);
        check.eq(prefix+"bootDefaultDeclared",0,BootPathLeaf.class.getDeclaredMethods().length);
        check.eq(prefix+"bootDefaultBootOwn",true,enumerated(java.nio.file.Path.class,"compareTo",int.class,Object.class).getDeclaringClass()==java.nio.file.Path.class);
    }
    private Method bootEnumerationCache;
    private void verifyBootEnumeration(Check check, String prefix, boolean changed) throws Exception {
        Class<?>[] owners = {BootComparator.class, BootFunction.class, BootCombined.class,
                BootReverse.class, BootReceiver.class, BootSubclass.class, BootHiding.class, BootOwn.class};
        for (int i = 0; i < owners.length; ++i) {
            Class<?> owner = owners[i]; String key = prefix+"bootEnumeration"+i;
            check.eq(key+"Natural",i == 6 ? 1 : 0,enumeratedCount(owner,"naturalOrder"));
            check.eq(key+"Identity",i == 7 ? 1 : 0,enumeratedCount(owner,"identity"));
            check.eq(key+"DeclaredNatural",i == 6,
                    java.util.Arrays.stream(owner.getDeclaredMethods()).anyMatch(m -> m.getName().equals("naturalOrder")));
        }
        BootReceiver receiver = new BootReceiver();
        Method reversed = enumerated(BootCombined.class,"reversed",java.util.Comparator.class);
        check.eq(prefix+"bootEnumerationDefaultOwner",true,reversed.getDeclaringClass()==java.util.Comparator.class);
        check.eq(prefix+"bootEnumerationDefault",true,reversed.isDefault());
        check.eq(prefix+"bootEnumerationDefaultCall",1,((java.util.Comparator<String>)reversed.invoke(receiver)).compare("a","b"));
        Method then = enumerated(BootCombined.class,"andThen",java.util.function.Function.class,java.util.function.Function.class);
        check.eq(prefix+"bootEnumerationFunctionOwner",true,then.getDeclaringClass()==java.util.function.Function.class);
        if (bootEnumerationCache==null) bootEnumerationCache=then;
        java.util.function.Function<String,String> identity = java.util.function.Function.identity();
        check.eq(prefix+"bootEnumerationFresh",changed ? "after" : "before",((java.util.function.Function<String,String>)then.invoke(receiver,identity)).apply("input"));
        check.eq(prefix+"bootEnumerationCached",changed ? "after" : "before",((java.util.function.Function<String,String>)bootEnumerationCache.invoke(receiver,identity)).apply("input"));
        check.eq(prefix+"bootEnumerationNull", "java.lang.NullPointerException",result(() -> then.invoke(receiver,new Object[]{null})));
        Method own = enumerated(BootOwn.class,"identity",String.class);
        check.eq(prefix+"bootEnumerationOwnOwner",true,own.getDeclaringClass()==BootOwn.class);
        check.eq(prefix+"bootEnumerationOwnCall","own",own.invoke(null));
        Method hiding = enumerated(BootHiding.class,"naturalOrder",int.class);
        check.eq(prefix+"bootEnumerationHidingOwner",true,hiding.getDeclaringClass()==BootHiding.class);
        check.eq(prefix+"bootEnumerationHidingCall",107,hiding.invoke(null));
        Method natural = enumerated(java.util.Comparator.class,"naturalOrder",java.util.Comparator.class);
        check.eq(prefix+"bootEnumerationBootOwner",true,natural.getDeclaringClass()==java.util.Comparator.class);
        check.eq(prefix+"bootEnumerationBootCall",-1,((java.util.Comparator<String>)natural.invoke(null)).compare("a","b"));
        Object token = new Object();
        Method function = java.util.function.Function.class.getDeclaredMethod("identity");
        check.eq(prefix+"bootEnumerationBootDeclared",true,((java.util.function.Function<Object,Object>)function.invoke(null)).apply(token)==token);
        check.eq(prefix+"bootEnumerationBootPublic",1,enumeratedCount(java.util.function.Function.class,"identity"));
    }
    private Method staticEnumerationCache;
    private static int enumeratedCount(Class<?> owner, String name) {
        int count = 0;
        for (Method method : owner.getMethods()) if (method.getName().equals(name)) ++count;
        return count;
    }
    private void verifyStaticEnumeration(Check check, String prefix, String kind, boolean changed) throws Exception {
        Class<?>[] owners = {StaticInterface.class, StaticChildInterface.class, StaticLeafInterface.class,
                StaticDiamond.class, StaticReverse.class, StaticImplementor.class,
                StaticLeafImplementor.class, StaticOwnImplementor.class};
        for (int i = 0; i < owners.length; ++i) {
            Class<?> owner = owners[i]; String key = prefix + "staticEnumeration" + i;
            check.eq(key+"Root", i == 0 || i == 7 ? 1 : 0, enumeratedCount(owner,"interfaceOnly"));
            check.eq(key+"Child", i == 1 ? 1 : 0, enumeratedCount(owner,"childOnly"));
            check.eq(key+"Declared", i == 0 || i == 7,
                    java.util.Arrays.stream(owner.getDeclaredMethods()).anyMatch(m -> m.getName().equals("interfaceOnly")));
        }
        Method own = enumerated(StaticInterface.class,"interfaceOnly",int.class);
        check.eq(prefix+"staticEnumerationOwnOwner",true,own.getDeclaringClass()==StaticInterface.class);
        check.eq(prefix+"staticEnumerationOwnCall",79,own.invoke(null));
        check.eq(prefix+"staticEnumerationOwnDeclared",79,StaticInterface.class.getDeclaredMethod("interfaceOnly").invoke(null));
        Method childStatic = enumerated(StaticChildInterface.class,"childOnly",int.class);
        check.eq(prefix+"staticEnumerationChildOwner",true,childStatic.getDeclaringClass()==StaticChildInterface.class);
        check.eq(prefix+"staticEnumerationChildCall",changed ? 61 : 59,childStatic.invoke(null));
        if (staticEnumerationCache == null) staticEnumerationCache = childStatic;
        check.eq(prefix+"staticEnumerationCached",changed ? 61 : 59,staticEnumerationCache.invoke(null));
        Method classStatic = enumerated(StaticLeafImplementor.class,"staticValue",int.class);
        check.eq(prefix+"staticEnumerationClassOwner",true,classStatic.getDeclaringClass()==InterfaceSymbolFixture.Peer.class);
        check.eq(prefix+"staticEnumerationClassCall",changed ? 61 : 59,classStatic.invoke(null));
        Method hidden = enumerated(StaticOwnImplementor.class,"interfaceOnly",int.class);
        check.eq(prefix+"staticEnumerationClassHiding",true,hidden.getDeclaringClass()==StaticOwnImplementor.class);
        check.eq(prefix+"staticEnumerationClassHidingCall",101,hidden.invoke(null));
        Class<?>[] changing = {InterfaceSymbolFixture.Child.class,InterfaceSymbolFixture.Receiver.class,
                InterfaceSymbolFixture.Override.class,InterfaceSymbolFixture.Lost.class};
        for (int i = 0; i < changing.length; ++i) {
            int staticValues = 0;
            for (Method method : changing[i].getMethods())
                if (method.getName().equals("value") && java.lang.reflect.Modifier.isStatic(method.getModifiers())) ++staticValues;
            check.eq(prefix+"staticEnumerationTransition"+i, i == 0 && changed && kind.equals("static") ? 1 : 0,staticValues);
        }
    }
    static class PackageStaticOwner {
        public static InterfaceSymbolFixture.Result object(InterfaceSymbolFixture.Result value) { return value; }
        public static int echo(InterfaceSymbolFixture.Result value) { return value == null ? 83 : 89; }
    }
    static class PackageStaticMiddle extends PackageStaticOwner { }
    static class PackageStaticLeaf extends PackageStaticMiddle { }
    static class PackagePeerChild extends InterfaceSymbolFixture.Peer { }
    private MethodHandle packagePeerHandle;
    public static class PeerStaticChild extends InterfaceSymbolFixture.Peer { }
    private MethodHandle inheritedPeerHandle;
    public static class StaticMiddle extends OrdinaryLookup { }
    public static class StaticLeaf extends StaticMiddle { }
    public static class StaticHiding extends StaticMiddle {
        public static int staticEcho(InterfaceSymbolFixture.Result value) { return value == null ? 47 : 43; }
        public static int staticEcho(String value) { return 53; }
    }
    // A distinct multi-level hierarchy keeps recursive lookup independent of
    // the Child declarations being changed by the transaction matrix.
    public interface LookupMiddle extends InterfaceSymbolFixture.Parent {}
    public interface LookupLeaf extends LookupMiddle {}
    public static class LookupReceiver implements LookupLeaf {}
    public static class CovariantLeaf extends InterfaceSymbolFixture.Peer { }
    public static class CovariantOverride extends InterfaceSymbolFixture.Peer {
        @Override public String covariant(boolean fail) { return "override"; }
    }
    public interface CovariantRoot { Object covariant(boolean fail); default int rank() { return 1; } }
    public interface CovariantInterface extends CovariantRoot {
        @Override default int rank() { return 7; }
        @Override default String covariant(boolean fail) { return new InterfaceSymbolFixture.Peer().covariant(fail); }
    }
    public interface CovariantInterfaceLeaf extends CovariantInterface { }
    public static class CovariantReceiver implements CovariantInterfaceLeaf { }
    public interface EnumerationLeft extends CovariantInterface { }
    public interface EnumerationRight extends CovariantInterface { }
    public interface EnumerationExtra { default int extra() { return 97; } }
    public interface EnumerationDiamond extends EnumerationLeft, EnumerationRight, EnumerationExtra { }
    public interface EnumerationReverse extends EnumerationRight, EnumerationLeft, EnumerationExtra { }
    public static class EnumerationReceiver implements EnumerationDiamond { }
    private final Method[] enumerationCache = new Method[9];
    private static Method enumerated(Class<?> owner, String name, Class<?> result, Class<?>... parameters) throws Exception {
        Method selected = null;
        for (Method method : owner.getMethods()) {
            if (!method.getName().equals(name) || method.getReturnType() != result
                    || !java.util.Arrays.equals(method.getParameterTypes(),parameters)) continue;
            if (selected != null) throw new AssertionError("duplicate enumeration for " + name);
            selected = method;
        }
        if (selected == null) throw new NoSuchMethodException(name);
        return selected;
    }
    private void verifyEnumeration(Check check, String prefix, boolean changed) throws Exception {
        Class<?>[] owners = {InterfaceSymbolFixture.Peer.class, CovariantLeaf.class, CovariantInterface.class,
                CovariantInterfaceLeaf.class, CovariantReceiver.class, EnumerationDiamond.class,
                EnumerationReverse.class, EnumerationReceiver.class, CovariantOverride.class};
        Object[] targets = {peer, new CovariantLeaf(), new CovariantReceiver(), new CovariantReceiver(),
                new CovariantReceiver(), new EnumerationReceiver(), new EnumerationReceiver(),
                new EnumerationReceiver(), new CovariantOverride()};
        for (int i = 0; i < owners.length; ++i) {
            final int slot = i; Class<?> owner = owners[i]; Object target = targets[i];
            Class<?> declaring = i < 2 ? InterfaceSymbolFixture.Peer.class : i == 8 ? CovariantOverride.class : CovariantInterface.class;
            String key = prefix + "enumeration" + i;
            String value = i == 8 ? "override" : changed ? "after" : "before";
            Method bridge = enumerated(owner,"covariant",Object.class,boolean.class);
            Method narrow = enumerated(owner,"covariant",String.class,boolean.class);
            check.eq(key+"BridgeOwner", true, bridge.getDeclaringClass() == declaring);
            check.eq(key+"Bridge", true, bridge.isBridge());
            check.eq(key+"Synthetic", true, bridge.isSynthetic());
            check.eq(key+"Default", i >= 2 && i < 8, bridge.isDefault());
            check.eq(key+"NarrowOwner", true, narrow.getDeclaringClass() == declaring);
            check.eq(key+"Call", value, result(() -> bridge.invoke(target,false)));
            check.eq(key+"NarrowCall", value, result(() -> narrow.invoke(target,false)));
            check.eq(key+"Cached", value, result(() -> {
                if (enumerationCache[slot] == null) enumerationCache[slot] = bridge;
                return enumerationCache[slot].invoke(target,false);
            }));
            check.eq(key+"Null", "java.lang.NullPointerException", result(() -> bridge.invoke(null,false)));
            check.eq(key+"Body", i == 8 ? "override" : "java.lang.IllegalStateException", result(() -> bridge.invoke(target,true)));
            check.eq(key+"Declared", i == 0 || i == 2 || i == 8,
                    java.util.Arrays.stream(owner.getDeclaredMethods()).anyMatch(m -> m.getName().equals("covariant")));
            if (i >= 2 && i < 8) {
                Method rank = enumerated(owner,"rank",int.class);
                check.eq(key+"RankOwner", true, rank.getDeclaringClass() == CovariantInterface.class);
                check.eq(key+"RankCall", 7, rank.invoke(target));
            }
        }
        Method extra = enumerated(EnumerationDiamond.class,"extra",int.class);
        check.eq(prefix+"enumerationExtraOwner", true, extra.getDeclaringClass() == EnumerationExtra.class);
        check.eq(prefix+"enumerationExtraCall", 97, extra.invoke(new EnumerationReceiver()));
    }
    private final MethodHandle[] covariantHandles = new MethodHandle[5];
    public Class<?> aliasReturn;
    private Method lateChild, lateParent;
    public InterfaceSymbolState() throws Exception { }
    public Object freshChildHandleResult() {
        return result(() -> MethodHandles.publicLookup().findVirtual(child,"value",MethodType.methodType(int.class)).invokeWithArguments(receiver));
    }
    public void verifyFreshLookup(Check check, String prefix, String kind, boolean changed) throws Exception {
        MethodType type = MethodType.methodType(int.class);
        check.eq(prefix+"freshChildHandle", changed ? kind.equals("drop") ? "java.lang.NoSuchMethodException" :
                "java.lang.IllegalAccessException" : 7,
                freshChildHandleResult());
        check.eq(prefix+"wrongReturnHandle", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findVirtual(child,"value",MethodType.methodType(String.class))));
        check.eq(prefix+"cachedParentEchoHandle", 7, result(() -> parentEcho.invokeWithArguments(receiver,new InterfaceSymbolFixture.Result())));
        check.eq(prefix+"freshParentEchoHandle", 7,
                result(() -> MethodHandles.publicLookup().findVirtual(parent,"echo",MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments(receiver,new InterfaceSymbolFixture.Result())));
        check.eq(prefix+"freshObjectParentHandle", true,
                result(() -> MethodHandles.publicLookup().findVirtual(parent,"objectValue",MethodType.methodType(InterfaceSymbolFixture.Result.class)).type().returnType() == InterfaceSymbolFixture.Result.class));
        check.eq(prefix+"freshObjectChildHandle", changed && kind.equals("static") ? "java.lang.IllegalAccessException" :
                changed && kind.equals("drop") ? "java.lang.NoSuchMethodException" : true,
                result(() -> MethodHandles.publicLookup().findVirtual(child,"objectValue",MethodType.methodType(InterfaceSymbolFixture.Result.class)).type().returnType() == InterfaceSymbolFixture.Result.class));
        if (changed && kind.equals("static")) {
            check.eq(prefix+"aliasReturnDistinct", true, aliasReturn != InterfaceSymbolFixture.Result.class);
            check.eq(prefix+"aliasReturnSameName", true, aliasReturn.getName().equals(InterfaceSymbolFixture.Result.class.getName()));
            check.eq(prefix+"freshAliasedParameterHandle", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findVirtual(child,"echo",MethodType.methodType(int.class,aliasReturn))));
            check.eq(prefix+"freshAliasedChildHandle", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findVirtual(child,"objectValue",MethodType.methodType(aliasReturn))));
        }
        check.eq(prefix+"freshParentHandle", 7,
                result(() -> MethodHandles.publicLookup().findVirtual(parent,"value",type).invokeWithArguments(receiver)));
        check.eq(prefix+"freshParentOverrideHandle", 17,
                result(() -> MethodHandles.publicLookup().findVirtual(parent,"value",type).invokeWithArguments(override)));
        check.eq(prefix+"freshParentLostHandle", changed && kind.equals("drop") ? "java.lang.ClassCastException" : 7,
                result(() -> MethodHandles.publicLookup().findVirtual(parent,"value",type).invokeWithArguments(lost)));
        if (changed && kind.equals("static"))
            check.eq(prefix+"freshChildStaticHandle", 11,
                    result(() -> MethodHandles.publicLookup().findStatic(child,"value",type).invokeWithArguments()));
        check.eq(prefix+"freshParentStaticHandle", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(parent,"value",type).invokeWithArguments()));
        check.eq(prefix+"declaredChildReflection", changed && kind.equals("static") ? 11 :
                changed && kind.equals("private") ? "java.lang.IllegalAccessException" : "java.lang.NoSuchMethodException",
                result(() -> child.getDeclaredMethod("value").invoke(receiver)));
        check.eq(prefix+"declaredChildStatic", changed && !kind.equals("drop") ? kind.equals("static") : "java.lang.NoSuchMethodException",
                result(() -> java.lang.reflect.Modifier.isStatic(child.getDeclaredMethod("value").getModifiers())));
        check.eq(prefix+"unreflectChildStatic", changed && kind.equals("static") ? 11 :
                changed && kind.equals("private") ? "java.lang.IllegalAccessException" : "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().unreflect(child.getDeclaredMethod("value")).invokeWithArguments()));
        for (Class<?> owner : new Class<?>[]{parent, OrdinaryLookup.class, LookupMiddle.class, LookupLeaf.class, LookupReceiver.class}) {
            String key=prefix+(owner==parent?"interface":owner==OrdinaryLookup.class?"ordinary":owner==LookupMiddle.class?"middle":owner==LookupLeaf.class?"leaf":"inheritedClass");
            check.eq(key+"AliasedReturn", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findVirtual(owner,"objectValue",MethodType.methodType(aliasReturn))));
            check.eq(key+"AliasedParameter", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findVirtual(owner,"echo",MethodType.methodType(int.class,aliasReturn))));
            check.eq(key+"WrongReturn", "java.lang.NoSuchMethodException",
                    result(() -> MethodHandles.publicLookup().findVirtual(owner,"objectValue",MethodType.methodType(String.class))));
            if (owner != parent && owner != OrdinaryLookup.class) {
                check.eq(key+"MatchingCall", 7, result(() -> MethodHandles.publicLookup().findVirtual(owner,"echo",
                        MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class))
                        .invokeWithArguments(new LookupReceiver(),new InterfaceSymbolFixture.Result())));
                check.eq(key+"ReflectionAlias", "java.lang.NoSuchMethodException",
                        result(() -> owner.getMethod("echo",aliasReturn)));
            }
        }
        check.eq(prefix+"childStaticResolution", changed && kind.equals("drop") ? "java.lang.NoSuchMethodException" : changed && kind.equals("static") ? 11 : "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(child,"value",type).invokeWithArguments()));
        check.eq(prefix+"childStaticWrongReturn", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findStatic(child,"value",MethodType.methodType(String.class))));
        check.eq(prefix+"parentStaticWrongReturn", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findStatic(parent,"value",MethodType.methodType(String.class))));
        check.eq(prefix+"parentStaticAliasedParameter", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(parent,"echo",MethodType.methodType(int.class,aliasReturn))));
        check.eq(prefix+"childStaticAliasedReturn", changed && kind.equals("drop") ? "java.lang.NoSuchMethodException" : "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(child,"objectValue",MethodType.methodType(aliasReturn))));
        check.eq(prefix+"childStaticWrongParameter", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findStatic(child,"echo",MethodType.methodType(int.class,String.class))));
        check.eq(prefix+"childStaticNullName", "java.lang.NullPointerException",
                result(() -> MethodHandles.publicLookup().findStatic(child,null,type)));
        check.eq(prefix+"ordinaryStaticHandle", 9,
                result(() -> MethodHandles.publicLookup().findStatic(Math.class,"abs",MethodType.methodType(int.class,int.class)).invokeWithArguments(-9)));
        check.eq(prefix+"ordinaryStaticAliasedReturn", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticObject",
                        MethodType.methodType(aliasReturn,InterfaceSymbolFixture.Result.class))));
        check.eq(prefix+"ordinaryStaticAliasedParameter", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticEcho",
                        MethodType.methodType(int.class,aliasReturn))));
        check.eq(prefix+"ordinaryStaticAliasedBoth", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticObject",
                        MethodType.methodType(aliasReturn,aliasReturn))));
        check.eq(prefix+"ordinaryStaticWrongReturn", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticObject",
                        MethodType.methodType(String.class,InterfaceSymbolFixture.Result.class))));
        check.eq(prefix+"ordinaryStaticWrongParameter", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticEcho",
                        MethodType.methodType(int.class,String.class))));
        InterfaceSymbolFixture.Result argument = new InterfaceSymbolFixture.Result();
        check.eq(prefix+"ordinaryStaticMatchingCall", 29,
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticEcho",
                        MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments(argument)));
        check.eq(prefix+"ordinaryStaticNullCall", 41,
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticEcho",
                        MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments((Object)null)));
        check.eq(prefix+"ordinaryStaticIdentityCall", true,
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticObject",
                        MethodType.methodType(InterfaceSymbolFixture.Result.class,InterfaceSymbolFixture.Result.class))
                        .invokeWithArguments(argument) == argument));
        for (Class<?> owner : new Class<?>[]{StaticMiddle.class, StaticLeaf.class, StaticHiding.class}) {
            String key = prefix + (owner == StaticMiddle.class ? "staticMiddle" : owner == StaticLeaf.class ? "staticLeaf" : "staticHiding");
            check.eq(key+"MatchingCall", owner == StaticHiding.class ? 43 : 29,
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticEcho",
                            MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments(argument)));
            check.eq(key+"NullCall", owner == StaticHiding.class ? 47 : 41,
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticEcho",
                            MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments((Object)null)));
            check.eq(key+"IdentityCall", true,
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticObject",
                            MethodType.methodType(InterfaceSymbolFixture.Result.class,InterfaceSymbolFixture.Result.class))
                            .invokeWithArguments(argument) == argument));
            check.eq(key+"AliasedReturn", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticObject",
                            MethodType.methodType(aliasReturn,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"AliasedParameter", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticEcho",MethodType.methodType(int.class,aliasReturn))));
            check.eq(key+"WrongReturn", "java.lang.NoSuchMethodException",
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticEcho",
                            MethodType.methodType(String.class,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"Overload", owner == StaticHiding.class ? 53 : "java.lang.NoSuchMethodException",
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticEcho",
                            MethodType.methodType(int.class,String.class)).invokeWithArguments("argument")));
            check.eq(key+"DeclaredReflection", "java.lang.NoSuchMethodException",
                    result(() -> owner.getDeclaredMethod("staticObject",InterfaceSymbolFixture.Result.class)));
        }
        check.eq(prefix+"inheritedPrivateAccess", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(StaticLeaf.class,"privateStatic",type)));
        check.eq(prefix+"inheritedProtectedAccess", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(StaticLeaf.class,"protectedStatic",type)));
        check.eq(prefix+"inheritedBodyException", "java.lang.IllegalStateException",
                result(() -> MethodHandles.publicLookup().findStatic(StaticLeaf.class,"throwingStatic",type).invokeWithArguments()));
        check.eq(prefix+"hiddenDeclaringClassCall", 73,
                result(() -> MethodHandles.publicLookup().findStatic(PublicStaticChild.class,"publicStatic",type).invokeWithArguments()));
        check.eq(prefix+"interfaceStaticNotInherited", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findStatic(StaticImplementor.class,"interfaceOnly",type)));
        check.eq(prefix+"inheritedPeerFresh", changed ? 61 : 59,
                result(() -> MethodHandles.publicLookup().findStatic(PeerStaticChild.class,"staticValue",type).invokeWithArguments()));
        check.eq(prefix+"inheritedPeerCached", changed ? 61 : 59, result(() -> {
            if (inheritedPeerHandle == null) inheritedPeerHandle = MethodHandles.publicLookup().findStatic(PeerStaticChild.class,"staticValue",type);
            return (int) inheritedPeerHandle.invokeExact();
        }));
        MethodHandles.Lookup foreignAccess = (MethodHandles.Lookup) aliasReturn.getMethod("lookup").invoke(null);
        check.eq(prefix+"packageForeignLookupClass", true, foreignAccess.lookupClass() == aliasReturn);
        for (Class<?> owner : new Class<?>[]{PackageStaticOwner.class, PackageStaticLeaf.class}) {
            String key = prefix + (owner == PackageStaticOwner.class ? "packageDeclared" : "packageInherited");
            MethodHandles.Lookup access = MethodHandles.lookup();
            check.eq(key+"Call", 89, result(() -> access.findStatic(owner,"echo",
                    MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments(argument)));
            check.eq(key+"NullCall", 83, result(() -> access.findStatic(owner,"echo",
                    MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments((Object)null)));
            check.eq(key+"IdentityCall", true, result(() -> access.findStatic(owner,"object",
                    MethodType.methodType(InterfaceSymbolFixture.Result.class,InterfaceSymbolFixture.Result.class))
                    .invokeWithArguments(argument) == argument));
            check.eq(key+"AliasedReturn", "java.lang.IllegalAccessException", result(() -> access.findStatic(owner,"object",
                    MethodType.methodType(aliasReturn,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"AliasedParameter", "java.lang.IllegalAccessException", result(() -> access.findStatic(owner,"echo",
                    MethodType.methodType(int.class,aliasReturn))));
            check.eq(key+"WrongReturn", "java.lang.NoSuchMethodException", result(() -> access.findStatic(owner,"echo",
                    MethodType.methodType(String.class,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"PublicDenied", "java.lang.IllegalAccessException", result(() -> MethodHandles.publicLookup().findStatic(owner,"echo",
                    MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"NoAccessDenied", "java.lang.IllegalAccessException", result(() -> MethodHandles.publicLookup().in(owner).findStatic(owner,"echo",
                    MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"ForeignPackageDenied", "java.lang.IllegalAccessException", result(() -> foreignAccess.findStatic(owner,"echo",
                    MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"WrongParameter", "java.lang.NoSuchMethodException", result(() -> access.findStatic(owner,"echo",
                    MethodType.methodType(int.class,String.class))));
            check.eq(key+"ReflectionAlias", "java.lang.NoSuchMethodException", result(() -> owner.getDeclaredMethod("echo",aliasReturn)));
        }
        check.eq(prefix+"packagePeerFresh", changed ? 61 : 59, result(() -> MethodHandles.lookup()
                .findStatic(PackagePeerChild.class,"staticValue",type).invokeWithArguments()));
        check.eq(prefix+"packagePeerCached", changed ? 61 : 59, result(() -> {
            if (packagePeerHandle == null) packagePeerHandle = MethodHandles.lookup().findStatic(PackagePeerChild.class,"staticValue",type);
            return (int) packagePeerHandle.invokeExact();
        }));
        Class<?>[] bridgeOwners = {InterfaceSymbolFixture.Peer.class, CovariantLeaf.class,
                CovariantInterface.class, CovariantInterfaceLeaf.class, CovariantReceiver.class};
        Object[] bridgeReceivers = {peer, new CovariantLeaf(), new CovariantReceiver(),
                new CovariantReceiver(), new CovariantReceiver()};
        for (int i = 0; i < bridgeOwners.length; ++i) {
            final int slot = i;
            Class<?> owner = bridgeOwners[i]; Object target = bridgeReceivers[i];
            String key = prefix + "covariant" + i;
            MethodType bridgeType = MethodType.methodType(Object.class, boolean.class);
            check.eq(key+"Call", changed ? "after" : "before", result(() -> MethodHandles.publicLookup()
                    .findVirtual(owner,"covariant",bridgeType).invokeWithArguments(target,false)));
            check.eq(key+"Cached", changed ? "after" : "before", result(() -> {
                if (covariantHandles[slot] == null) covariantHandles[slot] = MethodHandles.publicLookup()
                        .findVirtual(owner,"covariant",bridgeType);
                return covariantHandles[slot].invokeWithArguments(target,false);
            }));
            check.eq(key+"NarrowCall", changed ? "after" : "before", result(() -> MethodHandles.publicLookup()
                    .findVirtual(owner,"covariant",MethodType.methodType(String.class,boolean.class))
                    .invokeWithArguments(target,false)));
            check.eq(key+"Type", true, result(() -> MethodHandles.publicLookup().findVirtual(owner,"covariant",bridgeType)
                    .type().equals(bridgeType.insertParameterTypes(0,owner))));
            check.eq(key+"Null", "java.lang.NullPointerException", result(() -> MethodHandles.publicLookup()
                    .findVirtual(owner,"covariant",bridgeType).invokeWithArguments(null,false)));
            check.eq(key+"BodyException", "java.lang.IllegalStateException", result(() -> MethodHandles.publicLookup()
                    .findVirtual(owner,"covariant",bridgeType).invokeWithArguments(target,true)));
            check.eq(key+"WrongReturn", "java.lang.NoSuchMethodException", result(() -> MethodHandles.publicLookup()
                    .findVirtual(owner,"covariant",MethodType.methodType(Number.class,boolean.class))));
            boolean hasBridge = false;
            for (Method method : (i < 2 ? InterfaceSymbolFixture.Peer.class : CovariantInterface.class).getDeclaredMethods())
                hasBridge |= method.getName().equals("covariant") && method.isBridge()
                        && method.getReturnType() == Object.class;
            check.eq(key+"BridgePresent", true, hasBridge);
            check.eq(key+"AccessDenied", "java.lang.IllegalAccessException", result(() -> MethodHandles.publicLookup()
                    .in(PackageStaticOwner.class).findVirtual(owner,"covariant",bridgeType)));
            check.eq(key+"Reflection", true, owner.getMethod("covariant",boolean.class).getReturnType() == String.class);
        }
        check.eq(prefix+"covariantOverride", "override", result(() -> MethodHandles.publicLookup()
                .findVirtual(InterfaceSymbolFixture.Peer.class,"covariant",MethodType.methodType(Object.class,boolean.class))
                .invokeWithArguments(new CovariantOverride(),false)));
        check.eq(prefix+"covariantExact", changed ? "after" : "before", result(() -> {
            MethodHandle handle = MethodHandles.publicLookup().findVirtual(InterfaceSymbolFixture.Peer.class,
                    "covariant",MethodType.methodType(Object.class,boolean.class));
            return (Object) handle.invokeExact(peer,false);
        }));
        verifyEnumeration(check,prefix,changed);
        verifyStaticEnumeration(check,prefix,kind,changed);
        verifyBootEnumeration(check,prefix,changed);
        verifyBootOverride(check,prefix,changed);
        verifyBootDefaults(check,prefix,changed);
        check.eq(prefix+"ordinaryStaticReflectionAlias", "java.lang.NoSuchMethodException",
                result(() -> OrdinaryLookup.class.getDeclaredMethod("staticEcho",aliasReturn)));
    }
    public void verify(Check check, String prefix, String kind, boolean changed) throws Exception {
        Object failure = !changed ? null : kind.equals("drop") ? "java.lang.NoSuchMethodError"
                : kind.equals("private") ? "java.lang.IllegalAccessError" : "java.lang.IncompatibleClassChangeError";
        check.eq(prefix+"oldChild", changed ? failure : 7, result(() -> InterfaceSymbolFixture.Calls.child(receiver)));
        check.eq(prefix+"oldOverrideChild", changed ? failure : 17, result(() -> InterfaceSymbolFixture.Calls.child(override)));
        check.eq(prefix+"nullChild", changed ? failure : "java.lang.NullPointerException", result(() -> InterfaceSymbolFixture.Calls.child(null)));
        boolean removed = changed && kind.equals("drop");
        check.eq(prefix+"lostChild", changed ? failure : 7, result(() -> InterfaceSymbolFixture.Calls.child(lost)));
        check.eq(prefix+"lostParent", removed ? "java.lang.IncompatibleClassChangeError" : 7,
                result(() -> InterfaceSymbolFixture.Calls.lostParent(lost)));
        check.eq(prefix+"lostVirtual", removed ? "java.lang.NoSuchMethodError" : 7,
                result(() -> InterfaceSymbolFixture.Calls.lostVirtual(lost)));
        check.eq(prefix+"lostReflection", removed ? "java.lang.IllegalArgumentException" : 7,
                result(() -> cachedParent.invoke(lost)));
        check.eq(prefix+"lostChildHandle", removed ? "java.lang.IncompatibleClassChangeError" : 7,
                result(() -> childHandle.invokeWithArguments(lost)));
        check.eq(prefix+"lostParentHandle", removed ? "java.lang.ClassCastException" : 7,
                result(() -> parentHandle.invokeWithArguments(lost)));
        check.eq(prefix+"lostParentInstance", !removed, parent.isInstance(lost));
        check.eq(prefix+"lostChildExactHandle", removed ? "java.lang.IncompatibleClassChangeError" : 7,
                result(() -> (int) childHandle.invokeExact((InterfaceSymbolFixture.Child) lost)));
        check.eq(prefix+"lostClassHandle", removed ? "java.lang.AbstractMethodError" : 7,
                result(() -> classHandle.invokeWithArguments(lost)));
        check.eq(prefix+"nullChildHandle", "java.lang.NullPointerException", result(() -> childHandle.invokeWithArguments((Object) null)));
        check.eq(prefix+"bodyIllegalArgument", "java.lang.IllegalArgumentException", result(() -> bodyErrorHandle.invokeWithArguments(receiver)));
        check.eq(prefix+"parentCall", 7, result(() -> InterfaceSymbolFixture.Calls.parent(receiver)));
        check.eq(prefix+"parentOverride", 17, result(() -> InterfaceSymbolFixture.Calls.parent(override)));
        check.eq(prefix+"virtualCall", 7, result(() -> InterfaceSymbolFixture.Calls.virtual(receiver)));
        check.eq(prefix+"virtualOverride", 17, result(() -> InterfaceSymbolFixture.Calls.virtual(override)));
        check.eq(prefix+"cachedChildOwner", true, cachedChild.getDeclaringClass() == parent);
        check.eq(prefix+"cachedChildReflection", 7, result(() -> cachedChild.invoke(receiver)));
        check.eq(prefix+"cachedParentReflection", 7, result(() -> cachedParent.invoke(receiver)));
        check.eq(prefix+"cachedChildHandle", 7, result(() -> childHandle.invokeWithArguments(receiver)));
        check.eq(prefix+"cachedParentHandle", 7, result(() -> parentHandle.invokeWithArguments(receiver)));
        check.eq(prefix+"freshChildLookup", changed && kind.equals("drop") ? "java.lang.NoSuchMethodException" :
                changed && kind.equals("static") ? 11 : 7, result(() -> child.getMethod("value").invoke(receiver)));
        check.eq(prefix+"freshParentReflection", 7, result(() -> parent.getMethod("value").invoke(receiver)));
        check.eq(prefix+"childInstance", true, child.isInstance(receiver));
        check.eq(prefix+"parentInstance", true, parent.isInstance(receiver));
        check.eq(prefix+"identity", true, identity == System.identityHashCode(receiver));
        check.eq(prefix+"marker", 31, receiver.marker);
        check.eq(prefix+"peerCall", changed ? 5 : 3, peer.value());
        if (lateChild == null && changed) {
            Class<?> late = Class.forName("com.github.ytlog.artdcevm.fixture.InterfaceSymbolFixture$LateCalls");
            lateChild = late.getMethod("child", child); lateParent = late.getMethod("parent", parent);
        }
        if (lateChild != null) {
            check.eq(prefix+"lateChild", changed ? failure : 7, result(() -> lateChild.invoke(null,receiver)));
            check.eq(prefix+"lateParent", 7, result(() -> lateParent.invoke(null,receiver)));
        }
        check.eq(prefix+"newReceiverChild", changed ? failure : 7, result(() -> InterfaceSymbolFixture.Calls.child(new InterfaceSymbolFixture.Receiver())));
    }
}
