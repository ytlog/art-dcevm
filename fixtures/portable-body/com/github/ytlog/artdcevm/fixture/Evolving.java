package com.github.ytlog.artdcevm.fixture;
import java.util.concurrent.CountDownLatch;
public class Evolving {
    public int value;
    public static int counter = 7;
    public Evolving(int value) { this.value = value; }
    public int compute() { return value + 7; }
    public int waitThenCompute(CountDownLatch entered, CountDownLatch release) throws InterruptedException {
        entered.countDown();
        release.await();
        return value + 7;
    }
}
