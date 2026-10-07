package com.github.ytlog.artdcevm.fixture;
import java.util.concurrent.CountDownLatch;
public class Evolving {
    public int value;
    public static int counter = 7;
    public long added;
    public static int generation = 50;
    public Evolving(int value) { this.value = value; this.added = 77; }
    public int compute() { return value + 4; }
    public int waitThenCompute(CountDownLatch entered, CountDownLatch release) throws InterruptedException {
        entered.countDown();
        release.await();
        return value + 4;
    }
}
