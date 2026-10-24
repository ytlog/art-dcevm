package com.github.ytlog.artdcevm.internal;

import java.lang.reflect.Method;

/** Prove live Class.getMethods collection and actual reflected calls. */
public final class MethodEnumerationProbe {
    private MethodEnumerationProbe() { }
    public interface Root { Object value(); default int rank() { return 1; } }
    public interface Child extends Root {
        @Override default String value() { return "interface"; }
        @Override default int rank() { return 7; }
    }
    public interface Leaf extends Child { }
    public static class Receiver implements Leaf { }
    public static class Own implements Leaf {
        @Override public String value() { return "class"; }
        @Override public int rank() { return 9; }
    }
    public interface StaticRoot { static int hidden() { return 13; } }
    public interface StaticMixed extends StaticRoot {
        static int mixed() { return 17; }
        default int live() { return 19; }
    }
    public interface StaticLeaf extends StaticMixed { }
    public static class StaticReceiver implements StaticLeaf { }
    public static class StaticOwn extends StaticReceiver { public static int hidden() { return 23; } }
    public static class StaticSubclass extends StaticOwn { }
    public interface BootstrapLeaf extends java.util.Comparator<String>, java.util.function.Function<String,String> { }
    public static class BootstrapReceiver implements BootstrapLeaf {
        public int compare(String a, String b) { return a.compareTo(b); }
        public String apply(String a) { return a; }
    }
    public interface BootstrapOverride extends java.util.function.Supplier<String>, java.util.function.IntSupplier, java.util.Comparator<String> {
        @Override default String get() { return "override"; }
        @Override default int getAsInt() { return 29; }
        @Override default int compare(String a, String b) { return a.compareTo(b); }
        @Override default java.util.Comparator<String> reversed() { return (a,b) -> 31; }
    }
    public interface BootstrapOverrideLeaf extends BootstrapOverride { }
    public static class BootstrapOverrideReceiver implements BootstrapOverrideLeaf { }
    public static class BootstrapOverrideOwn implements BootstrapOverrideLeaf {
        @Override public String get() { return "own"; }
        @Override public int getAsInt() { return 37; }
        @Override public java.util.Comparator<String> reversed() { return (a,b) -> 41; }
    }
    public static void runBootstrapOverride() throws Exception {
        for (Class<?> boot : new Class<?>[]{java.util.function.Supplier.class,java.util.function.IntSupplier.class,java.util.Comparator.class})
            if(boot.getClassLoader()!=Object.class.getClassLoader()) throw new AssertionError("bootstrap identity");
        for (Class<?> owner : new Class<?>[]{BootstrapOverrideLeaf.class,BootstrapOverrideReceiver.class,BootstrapOverrideOwn.class}) {
            boolean own=owner==BootstrapOverrideOwn.class;
            Object receiver=own ? new BootstrapOverrideOwn() : new BootstrapOverrideReceiver();
            int methods=0;
            for(Method m:owner.getMethods()) {
                String name=m.getName();
                if(!name.equals("get") && !name.equals("getAsInt") && !name.equals("reversed"))continue;
                boolean bridge=name.equals("get") && m.getReturnType()==Object.class;
                if(m.getDeclaringClass()!=(own ? BootstrapOverrideOwn.class : BootstrapOverride.class)
                        || m.isBridge()!=bridge || m.isSynthetic()!=bridge || m.isDefault()==own)
                    throw new AssertionError("bootstrap ancestor retained");
                Object result=m.invoke(receiver);
                boolean correct=name.equals("get") ? (own ? "own" : "override").equals(result)
                        : name.equals("getAsInt") ? Integer.valueOf(own ? 37 : 29).equals(result)
                        : ((java.util.Comparator<String>)result).compare("a","b")== (own ? 41 : 31);
                if(!correct)throw new AssertionError("bootstrap override call");
                ++methods;
            }
            if(methods!=4)throw new AssertionError("bootstrap override incomplete");
        }
        Method original=java.util.function.Supplier.class.getMethod("get");
        if(original.getDeclaringClass()!=java.util.function.Supplier.class || original.isBridge()
                || !"override".equals(original.invoke(new BootstrapOverrideReceiver()))
                || BootstrapOverrideLeaf.class.getDeclaredMethods().length!=0)
            throw new AssertionError("bootstrap own or declared view changed");
    }
    public interface BootstrapList extends java.util.List<String> { }
    public interface BootstrapListLeaf extends BootstrapList { }
    public static class BootstrapListReceiver extends java.util.AbstractList<String> implements BootstrapListLeaf {
        public int size(){return 1;}
        public String get(int i){if(i!=0)throw new IndexOutOfBoundsException();return "proof";}
    }
    public static class BootstrapListOwn extends BootstrapListReceiver {
        @Override public java.util.Spliterator<String> spliterator(){return java.util.Spliterators.emptySpliterator();}
    }
    public interface BootstrapPath extends java.nio.file.Path { }
    public interface BootstrapPathLeaf extends BootstrapPath { }
    public abstract static class BootstrapPathReceiver implements BootstrapPathLeaf { }
    public static void runBootstrapInstance() throws Exception {
        for(Class<?> boot:new Class<?>[]{java.util.List.class,java.nio.file.Path.class})
            if(boot.getClassLoader()!=Object.class.getClassLoader())throw new AssertionError("bootstrap identity");
        Class<?>[] owners={BootstrapListLeaf.class,BootstrapListReceiver.class,BootstrapPathLeaf.class,BootstrapPathReceiver.class};
        for(int i=0;i<owners.length;++i){
            boolean path=i>=2;int count=0;
            for(Method m:owners[i].getMethods()){
                if(path ? !m.getName().equals("compareTo") || m.getParameterTypes().length!=1 || m.getParameterTypes()[0]!=Object.class : !m.getName().equals("spliterator"))continue;
                if(m.getDeclaringClass()!=(path?java.nio.file.Path.class:java.util.List.class)
                        || m.isBridge()!=path || m.isSynthetic()!=path || !m.isDefault())throw new AssertionError("bootstrap ancestor retained");
                if(path){
                    java.nio.file.Path value=java.nio.file.Paths.get("proof");
                    if(!Integer.valueOf(0).equals(m.invoke(value,value)))throw new AssertionError("bootstrap bridge call");
                }else{
                    java.util.ArrayList<String> values=new java.util.ArrayList<>();
                    ((java.util.Spliterator<String>)m.invoke(new BootstrapListReceiver())).forEachRemaining(values::add);
                    if(!values.equals(java.util.Collections.singletonList("proof")))throw new AssertionError("bootstrap default call");
                }
                ++count;
            }
            if(count!=1)throw new AssertionError("incomplete bootstrap specificity");
        }
        for(Method m:BootstrapListOwn.class.getMethods())if(m.getName().equals("spliterator"))
            if(m.getDeclaringClass()!=BootstrapListOwn.class || ((java.util.Spliterator<?>)m.invoke(new BootstrapListOwn())).estimateSize()!=0)
                throw new AssertionError("class priority changed");
        if(BootstrapPathLeaf.class.getDeclaredMethods().length!=0
                || java.nio.file.Path.class.getDeclaredMethod("compareTo",Object.class).getDeclaringClass()!=java.nio.file.Path.class)
            throw new AssertionError("declared view changed");
    }
    public static void runBootstrapRootInstance() throws Exception {
        Class<?>[] roots={java.util.NavigableSet.class,java.util.AbstractList.class,java.util.AbstractSet.class,java.util.AbstractCollection.class};
        Class<?>[] owners={java.util.SortedSet.class,java.util.List.class,java.util.Set.class,java.util.Collection.class};
        Object[] receivers={new java.util.TreeSet<String>(java.util.Collections.singleton("proof")),new BootstrapListReceiver(),
                new java.util.HashSet<String>(java.util.Collections.singleton("proof")),new BootstrapListReceiver()};
        for(int i=0;i<roots.length;++i){
            if(roots[i].getClassLoader()!=Object.class.getClassLoader())throw new AssertionError("bootstrap root identity");
            int count=0;
            for(Method m:roots[i].getMethods())if(m.getName().equals("spliterator")){
                if(m.getDeclaringClass()!=owners[i] || m.isBridge() || m.isSynthetic() || !m.isDefault())
                    throw new AssertionError("bootstrap root ancestor retained");
                java.util.ArrayList<String> values=new java.util.ArrayList<>();
                ((java.util.Spliterator<String>)m.invoke(receivers[i])).forEachRemaining(values::add);
                if(!values.equals(java.util.Collections.singletonList("proof")))throw new AssertionError("bootstrap root call");
                ++count;
            }
            if(count!=1)throw new AssertionError("incomplete bootstrap root specificity");
        }
        int own=0;
        for(Method m:java.util.ArrayList.class.getMethods())if(m.getName().equals("spliterator")){
            if(m.getDeclaringClass()!=java.util.ArrayList.class || m.isDefault()
                    || ((java.util.Spliterator<?>)m.invoke(new java.util.ArrayList<>())).estimateSize()!=0)
                throw new AssertionError("bootstrap class priority changed");
            ++own;
        }
        if(own!=1 || java.util.List.class.getDeclaredMethod("spliterator").getDeclaringClass()!=java.util.List.class)
            throw new AssertionError("bootstrap declared view changed");
        try{java.util.AbstractList.class.getDeclaredMethod("spliterator");throw new AssertionError("inherited method became declared");}
        catch(NoSuchMethodException expected){ }
    }
    public static void runBootstrapRootStatic() throws Exception {
        Class<?>[] roots={java.util.function.UnaryOperator.class,java.text.Collator.class,java.text.RuleBasedCollator.class};
        Object token=new Object();
        for(int i=0;i<roots.length;++i){
            Class<?> root=roots[i];int own=0;
            if(root.getClassLoader()!=Object.class.getClassLoader())throw new AssertionError("bootstrap root identity");
            for(Method m:root.getMethods()){
                boolean isStatic=java.lang.reflect.Modifier.isStatic(m.getModifiers());
                if(isStatic && m.getDeclaringClass().isInterface() && m.getDeclaringClass()!=root)
                    throw new AssertionError("inherited bootstrap root static");
                if(i==0 && m.getName().equals("identity")){
                    if(m.getDeclaringClass()!=root || !isStatic || ((java.util.function.UnaryOperator<Object>)m.invoke(null)).apply(token)!=token)
                        throw new AssertionError("own bootstrap static changed");
                    ++own;
                }else if(i!=0 && m.getName().equals("getInstance") && java.util.Arrays.equals(m.getParameterTypes(),new Class<?>[]{java.util.Locale.class})){
                    if(m.getDeclaringClass()!=java.text.Collator.class || !isStatic || ((java.text.Collator)m.invoke(null,java.util.Locale.ROOT)).compare("a","b")>=0)
                        throw new AssertionError("inherited class static changed");
                    ++own;
                }
            }
            if(own!=1)throw new AssertionError("bootstrap root declaration lost");
        }
        int defaults=0;
        for(Method m:java.text.Collator.class.getMethods())if(m.getName().equals("reversed")){
            if(m.getDeclaringClass()!=java.util.Comparator.class || !m.isDefault()
                    || ((java.util.Comparator<String>)m.invoke(java.text.Collator.getInstance(java.util.Locale.ROOT))).compare("a","b")<=0)
                throw new AssertionError("bootstrap default changed");
            ++defaults;
        }
        if(defaults!=1 || ((java.util.function.Function<Object,Object>)java.util.function.Function.class.getDeclaredMethod("identity").invoke(null)).apply(token)!=token)
            throw new AssertionError("bootstrap declared view changed");
    }
    public static void runBootstrapStatic() throws Exception {
        if (java.util.Comparator.class.getClassLoader()!=Object.class.getClassLoader()
                || java.util.function.Function.class.getClassLoader()!=Object.class.getClassLoader())
            throw new AssertionError("standard interfaces are not bootstrap declarations");
        for (Class<?> owner : new Class<?>[]{BootstrapLeaf.class,BootstrapReceiver.class}) {
            int reverse = 0, then = 0;
            for (Method method : owner.getMethods()) {
                if (method.getDeclaringClass().getClassLoader()==Object.class.getClassLoader()
                        && method.getDeclaringClass().isInterface()
                        && java.lang.reflect.Modifier.isStatic(method.getModifiers()))
                    throw new AssertionError("inherited bootstrap static");
                if (method.getName().equals("reversed")) {
                    if (method.getDeclaringClass()!=java.util.Comparator.class
                            || ((java.util.Comparator<String>)method.invoke(new BootstrapReceiver())).compare("a","b")!=1)
                        throw new AssertionError("bootstrap default changed");
                    ++reverse;
                }
                if (method.getName().equals("andThen")) {
                    Object value = ((java.util.function.Function<String,String>)method.invoke(new BootstrapReceiver(),
                            java.util.function.Function.identity())).apply("value");
                    if (!"value".equals(value)) throw new AssertionError("bootstrap function changed");
                    ++then;
                }
            }
            if (reverse!=1 || then!=1) throw new AssertionError("bootstrap instance declaration lost");
        }
        int own = 0;
        for (Method method : java.util.Comparator.class.getMethods()) {
            if (method.getName().equals("naturalOrder")
                    && ((java.util.Comparator<String>)method.invoke(null)).compare("a","b")==-1) ++own;
        }
        Object token = new Object();
        if (own!=1 || ((java.util.function.Function<Object,Object>)java.util.function.Function.class
                .getDeclaredMethod("identity").invoke(null)).apply(token)!=token)
            throw new AssertionError("bootstrap own declarations changed");
    }
    public static void runStatic() throws Exception {
        for (Class<?> owner : new Class<?>[]{StaticLeaf.class, StaticReceiver.class, StaticOwn.class, StaticSubclass.class}) {
            int own = 0, live = 0;
            boolean hasOwn = owner == StaticOwn.class || owner == StaticSubclass.class;
            for (Method method : owner.getMethods()) {
                if (method.getName().equals("mixed")) throw new AssertionError("inherited interface static");
                if (method.getName().equals("hidden")) {
                    if (!hasOwn || method.getDeclaringClass() != StaticOwn.class
                            || !Integer.valueOf(23).equals(method.invoke(null))) throw new AssertionError("class static changed");
                    ++own;
                }
                if (method.getName().equals("live")) {
                    if (!Integer.valueOf(19).equals(method.invoke(new StaticReceiver()))) throw new AssertionError("instance method lost");
                    ++live;
                }
            }
            if (own != (hasOwn ? 1 : 0) || live != 1) throw new AssertionError("incomplete static filtering");
        }
        int declarations = 0;
        for (Method method : StaticRoot.class.getMethods()) {
            if (method.getName().equals("hidden") && method.getDeclaringClass() == StaticRoot.class
                    && Integer.valueOf(13).equals(method.invoke(null))) ++declarations;
        }
        if (declarations != 1 || !Integer.valueOf(13).equals(StaticRoot.class.getDeclaredMethod("hidden").invoke(null))
                || !Integer.valueOf(17).equals(StaticMixed.class.getDeclaredMethod("mixed").invoke(null)))
            throw new AssertionError("own static declaration lost");
    }
    public static void run() throws Exception {
        for (Class<?> owner : new Class<?>[]{Leaf.class, Receiver.class, Own.class}) {
            boolean own = owner == Own.class;
            Object receiver = own ? new Own() : new Receiver();
            Class<?> expectedOwner = own ? Own.class : Child.class;
            int bridges = 0, narrow = 0, ranks = 0;
            for (Method method : owner.getMethods()) {
                if (method.getName().equals("value")) {
                    if (method.getDeclaringClass() != expectedOwner
                            || !(own ? "class" : "interface").equals(method.invoke(receiver)))
                        throw new AssertionError("enumeration chose an ancestor");
                    if (method.getReturnType() == Object.class && method.isBridge()) ++bridges;
                    if (method.getReturnType() == String.class && !method.isBridge()) ++narrow;
                } else if (method.getName().equals("rank")) {
                    if (method.getDeclaringClass() != expectedOwner || !Integer.valueOf(own ? 9 : 7).equals(method.invoke(receiver)))
                        throw new AssertionError("enumeration changed class or interface priority");
                    ++ranks;
                }
            }
            if (bridges != 1 || narrow != 1 || ranks != 1) throw new AssertionError("incomplete enumeration");
        }
        if (Leaf.class.getDeclaredMethods().length != 0) throw new AssertionError("declared enumeration changed");
    }
}
