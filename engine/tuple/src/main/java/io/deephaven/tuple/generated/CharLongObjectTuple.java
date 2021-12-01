package io.deephaven.tuple.generated;

import gnu.trove.map.TIntObjectMap;
import io.deephaven.tuple.CanonicalizableTuple;
import io.deephaven.tuple.serialization.SerializationUtils;
import io.deephaven.tuple.serialization.StreamingExternalizable;
import io.deephaven.util.compare.CharComparisons;
import io.deephaven.util.compare.LongComparisons;
import io.deephaven.util.compare.ObjectComparisons;
import org.jetbrains.annotations.NotNull;

import java.io.Externalizable;
import java.io.IOException;
import java.io.ObjectInput;
import java.io.ObjectOutput;
import java.util.Objects;
import java.util.function.UnaryOperator;

/**
 * <p>3-Tuple (triple) key class composed of char, long, and Object elements.
 * <p>Generated by io.deephaven.replicators.TupleCodeGenerator.
 */
public class CharLongObjectTuple implements Comparable<CharLongObjectTuple>, Externalizable, StreamingExternalizable, CanonicalizableTuple<CharLongObjectTuple> {

    private static final long serialVersionUID = 1L;

    private char element1;
    private long element2;
    private Object element3;

    private transient int cachedHashCode;

    public CharLongObjectTuple(
            final char element1,
            final long element2,
            final Object element3
    ) {
        initialize(
                element1,
                element2,
                element3
        );
    }

    /** Public no-arg constructor for {@link Externalizable} support only. <em>Application code should not use this!</em> **/
    public CharLongObjectTuple() {
    }

    private void initialize(
            final char element1,
            final long element2,
            final Object element3
    ) {
        this.element1 = element1;
        this.element2 = element2;
        this.element3 = element3;
        cachedHashCode = ((31 +
                Character.hashCode(element1)) * 31 +
                Long.hashCode(element2)) * 31 +
                Objects.hashCode(element3);
    }

    public final char getFirstElement() {
        return element1;
    }

    public final long getSecondElement() {
        return element2;
    }

    public final Object getThirdElement() {
        return element3;
    }

    @Override
    public final int hashCode() {
        return cachedHashCode;
    }

    @Override
    public final boolean equals(final Object other) {
        if (this == other) {
            return true;
        }
        if (other == null || getClass() != other.getClass()) {
            return false;
        }
        final CharLongObjectTuple typedOther = (CharLongObjectTuple) other;
        // @formatter:off
        return element1 == typedOther.element1 &&
               element2 == typedOther.element2 &&
               ObjectComparisons.eq(element3, typedOther.element3);
        // @formatter:on
    }

    @Override
    public final int compareTo(@NotNull final CharLongObjectTuple other) {
        if (this == other) {
            return 0;
        }
        int comparison;
        // @formatter:off
        return 0 != (comparison = CharComparisons.compare(element1, other.element1)) ? comparison :
               0 != (comparison = LongComparisons.compare(element2, other.element2)) ? comparison :
               ObjectComparisons.compare(element3, other.element3);
        // @formatter:on
    }

    @Override
    public void writeExternal(@NotNull final ObjectOutput out) throws IOException {
        out.writeChar(element1);
        out.writeLong(element2);
        out.writeObject(element3);
    }

    @Override
    public void readExternal(@NotNull final ObjectInput in) throws IOException, ClassNotFoundException {
        initialize(
                in.readChar(),
                in.readLong(),
                in.readObject()
        );
    }

    @Override
    public void writeExternalStreaming(@NotNull final ObjectOutput out, @NotNull final TIntObjectMap<SerializationUtils.Writer> cachedWriters) throws IOException {
        out.writeChar(element1);
        out.writeLong(element2);
        StreamingExternalizable.writeObjectElement(out, cachedWriters, 2, element3);
    }

    @Override
    public void readExternalStreaming(@NotNull final ObjectInput in, @NotNull final TIntObjectMap<SerializationUtils.Reader> cachedReaders) throws Exception {
        initialize(
                in.readChar(),
                in.readLong(),
                StreamingExternalizable.readObjectElement(in, cachedReaders, 2)
        );
    }

    @Override
    public String toString() {
        return "CharLongObjectTuple{" +
                element1 + ", " +
                element2 + ", " +
                element3 + '}';
    }

    @Override
    public CharLongObjectTuple canonicalize(@NotNull final UnaryOperator<Object> canonicalizer) {
        final Object canonicalizedElement3 = canonicalizer.apply(element3);
        return canonicalizedElement3 == element3
                ? this : new CharLongObjectTuple(element1, element2, canonicalizedElement3);
    }
}