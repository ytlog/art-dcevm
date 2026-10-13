package com.github.ytlog.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;

/** Locations typed as AddedContract; use only after the interface is added. */
public final class ContractHolder {
    private static AddedContract field;
    private static AddedContract[] array;
    private static volatile int stackResult;

    private ContractHolder() {}

    public static void storeField() { field = (AddedContract) InterfaceHolder.subject(); }
    public static void storeArray() { array = new AddedContract[]{(AddedContract) InterfaceHolder.subject()}; }
    public static void clear() { field = null; array = null; }

    /** A thread whose frame keeps the instance in a local typed as AddedContract. */
    public static Thread holdOnStack(CountDownLatch held, CountDownLatch release) {
        Thread thread = new Thread(() -> {
            AddedContract contract = (AddedContract) InterfaceHolder.subject();
            held.countDown();
            try {
                release.await();
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
            }
            stackResult = contract.contractTwice();
        }, "artdcevm-contract-holder");
        thread.start();
        return thread;
    }

    public static int stackResult() { return stackResult; }
}
