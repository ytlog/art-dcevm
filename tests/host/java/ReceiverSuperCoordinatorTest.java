import com.github.ytlog.artdcevm.internal.ReceiverGuardFixture;
import java.util.concurrent.atomic.AtomicReference;

public final class ReceiverSuperCoordinatorTest {
    static void check(boolean value) { if (!value) throw new AssertionError(); }
    static final class Worker {
        final AtomicReference<Throwable> failure = new AtomicReference<>();
        volatile int result;
        volatile boolean interruptObserved;
        final Thread thread;
        Worker(ReceiverGuardFixture fixture) {
            thread = new Thread(() -> {
                try { result = fixture.receiver.callSuper(); }
                catch (Throwable error) { failure.set(error); interruptObserved = Thread.currentThread().isInterrupted(); }
            }, "old-super-body-worker");
        }
        void join() throws InterruptedException { thread.join(5000); check(!thread.isAlive()); }
    }
    static void scenario(int mode) throws Exception {
        ReceiverGuardFixture fixture = new ReceiverGuardFixture();
        var coordinator = fixture.receiver.coordinateSuper(mode == 4 ? 20 : 5000);
        check(coordinator.receiver() == fixture.receiver && !coordinator.release());
        try { fixture.receiver.coordinateSuper(5000); throw new AssertionError(); }
        catch (IllegalStateException expected) {}
        long[] before = ReceiverGuardFixture.counters();
        Worker worker = new Worker(fixture);
        if (mode == 3) check(coordinator.cancel());
        worker.thread.start();
        try {
            if (mode != 3) check(coordinator.awaitEntered(5000));
            if (mode != 3 && mode != 4) {
                check(coordinator.enteredThread() == worker.thread && !coordinator.completed());
                check(ReceiverGuardFixture.counters()[0] == before[0]);
            }
            if (mode == 0) { check(coordinator.release()); check(!coordinator.release()); }
            if (mode == 1) { check(coordinator.cancel()); check(!coordinator.release()); }
            if (mode == 2) worker.thread.interrupt();
            worker.join();
            check(coordinator.entered() && coordinator.completed() && coordinator.enteredThread() == worker.thread);
            long[] after = ReceiverGuardFixture.counters();
            check(after[1] == before[1] && fixture.receiver.value == 71);
            if (mode == 0) check(worker.failure.get() == null && worker.result == 113 && after[0] == before[0] + 1 && !coordinator.cancelled());
            else check(worker.failure.get() instanceof InterruptedException && after[0] == before[0] && coordinator.cancelled());
            check(worker.interruptObserved == (mode == 2));
            check(!coordinator.release() && !coordinator.cancel());
        } finally { coordinator.cancel(); worker.join(); }
    }
    public static void main(String[] args) throws Exception {
        ReceiverGuardFixture baseline = new ReceiverGuardFixture();
        long before = ReceiverGuardFixture.counters()[0];
        check(baseline.receiver.callSuper() == 113 && ReceiverGuardFixture.counters()[0] == before + 1);
        for (int mode = 0; mode < 5; ++mode) scenario(mode);
        ReceiverGuardFixture untouched = new ReceiverGuardFixture();
        var notEntered = untouched.receiver.coordinateSuper(5000);
        check(!notEntered.awaitEntered(10) && notEntered.enteredThread() == null);
        AtomicReference<Throwable> observerFailure = new AtomicReference<>();
        Thread observer = new Thread(() -> {
            Thread.currentThread().interrupt();
            try { notEntered.awaitEntered(5000); observerFailure.set(new AssertionError()); }
            catch (InterruptedException expected) {
                if (!Thread.currentThread().isInterrupted()) observerFailure.set(new AssertionError());
            }
        });
        observer.start(); observer.join(5000);
        check(!observer.isAlive() && observerFailure.get() == null && !notEntered.entered() && !notEntered.cancelled());
        check(notEntered.cancel() && !notEntered.awaitEntered(10));
        for(long bad : new long[]{0, -1, Long.MAX_VALUE}) {
            try { new ReceiverGuardFixture().receiver.coordinateSuper(bad); throw new AssertionError(); }
            catch (IllegalArgumentException expected) {}
        }
        System.out.println("Actual old callSuper worker: default baseline, exact receiver/thread, monitor-releasing wait, release, cancellation, interrupt restoration, timeout, unchanged pre-super counters PASS; no migration/frame permission proved");
    }
}
