package com.github.ytlog.artdcevm.fixture;
import java.util.ArrayList;
import java.util.concurrent.CountDownLatch;
public class Evolving extends ArrayList<Object> implements Runnable {
    public int value;
    public static int counter = 7;
    public Evolving(int value) { this.value = value; }
    public int compute() { return value + 6; }
    public void run() { value++; }
    public int waitThenCompute(CountDownLatch entered, CountDownLatch release) throws InterruptedException {
        entered.countDown(); release.await(); return value + 6;
    }
}
