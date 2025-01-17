/**
 * Copyright (c) 2016-2022 Deephaven Data Labs and Patent Pending
 */
package io.deephaven.io.sched;

import io.deephaven.base.log.LogOutput;

import java.nio.channels.SelectableChannel;

/**
 * This is the base class for jobs which are only interested in timing events. It provides default invoke() and
 * cancelled() method which do nothing.
 */
public abstract class TimedJob extends Job {
    public int invoke(SelectableChannel channel, int readyOps, Runnable handoff) {
        if (handoff != null) {
            handoff.run();
        }
        return 0;
    }

    public void cancelled() {
        // do nothing
    }

    @Override
    public LogOutput append(LogOutput logOutput) {
        return logOutput.append(LogOutput.BASIC_FORMATTER, this);
    }
}
