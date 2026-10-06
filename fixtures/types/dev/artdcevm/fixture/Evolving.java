package dev.artdcevm.fixture;
import java.util.concurrent.CountDownLatch;
public class Evolving {
    public long value;
    public static int counter = 7;
    public Object added;
    public static String generation = "next";
    public Evolving(int value) { this.value = value; this.added = new Object(); }
    public long compute(long factor) { return value * factor; }
    public int addedMethod() { return (int) value + 100; }
    public int waitThenCompute(CountDownLatch entered, CountDownLatch release) throws InterruptedException {
        entered.countDown(); release.await(); return (int) value + 5;
    }
}
