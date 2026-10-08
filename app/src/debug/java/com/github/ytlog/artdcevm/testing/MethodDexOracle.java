package com.github.ytlog.artdcevm.testing;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Independent identity oracle from the actual installed APK, never from ART fields. */
public final class MethodDexOracle {
    private static final int MAX_DEX_BYTES = 128 * 1024 * 1024;
    public static final class Result {
        public final int[] indices;
        public final int[] modifiers;
        public final String dexEntry;
        public final int headerOffset;
        Result(int[] indices, int[] modifiers, String entry, int offset) {
            this.indices = indices; this.modifiers = modifiers;
            this.dexEntry = entry; this.headerOffset = offset;
        }
    }
    private MethodDexOracle() { }
    public static Result read(String apkPath, Method[] methods) throws IOException {
        if (methods == null || methods.length < 3 || methods.length > 32)
            throw new IOException("At least three fixture methods are required");
        Class<?> owner = methods[0].getDeclaringClass();
        for (Method method : methods) {
            if (method.getDeclaringClass() != owner) throw new IOException("Mixed fixture declaring classes");
        }
        Result found = null;
        try (ZipFile zip = new ZipFile(apkPath)) {
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                if (!entry.getName().matches("classes(?:[2-9]|[1-9][0-9]+)?\\.dex")) continue;
                if (entry.getSize() < 112 || entry.getSize() > MAX_DEX_BYTES)
                    throw new IOException("Invalid APK DEX size: " + entry.getName());
                byte[] bytes;
                try (InputStream input = zip.getInputStream(entry)) {
                    ByteArrayOutputStream output = new ByteArrayOutputStream();
                    byte[] buffer = new byte[8192]; int read;
                    while ((read = input.read(buffer)) != -1) {
                        if (output.size() > MAX_DEX_BYTES - read) throw new IOException("DEX too large");
                        output.write(buffer, 0, read);
                    }
                    bytes = output.toByteArray();
                    if (bytes.length != entry.getSize()) throw new IOException("APK DEX entry size mismatch");
                }
                int offset = 0;
                do {
                    Dex dex = new Dex(bytes, offset);
                    int[] indices = dex.find(methods);
                    if (indices != null) {
                        if (found != null) throw new IOException("Duplicate fixture class definition across APK DEX files");
                        int[] modifiers = new int[methods.length];
                        for (int i = 0; i < methods.length; ++i) modifiers[i] = methods[i].getModifiers();
                        found = new Result(indices, modifiers, entry.getName(), offset);
                    }
                    offset += dex.fileSize;
                    if (!dex.container && offset != bytes.length) throw new IOException("Trailing bytes in legacy DEX");
                } while (offset < bytes.length);
            }
        }
        if (found == null) throw new IOException("Fixture class definition missing from installed APK DEX");
        return found;
    }
    private static String descriptor(Class<?> type) {
        if (type.isArray()) return type.getName().replace('.', '/');
        if (!type.isPrimitive()) return "L" + type.getName().replace('.', '/') + ";";
        if (type == void.class) return "V";
        if (type == boolean.class) return "Z";
        if (type == byte.class) return "B";
        if (type == char.class) return "C";
        if (type == short.class) return "S";
        if (type == int.class) return "I";
        if (type == long.class) return "J";
        if (type == float.class) return "F";
        if (type == double.class) return "D";
        throw new IllegalArgumentException("Unknown primitive");
    }
    private static String descriptor(Method method) {
        StringBuilder result = new StringBuilder("(");
        for (Class<?> type : method.getParameterTypes()) result.append(descriptor(type));
        return result.append(')').append(descriptor(method.getReturnType())).toString();
    }
    private static final class Dex {
        final byte[] bytes;
        final int base, fileSize;
        final boolean container;
        final int strings, stringOff, types, typeOff, protos, protoOff;
        final int methods, methodOff, classes, classOff;
        Dex(byte[] bytes, int base) throws IOException {
            this.bytes = bytes; this.base = base;
            range(base, 112);
            if (bytes[base] != 'd' || bytes[base + 1] != 'e' || bytes[base + 2] != 'x' ||
                bytes[base + 3] != '\n' || bytes[base + 7] != 0) throw bad("Invalid DEX magic");
            String version = new String(bytes, base + 4, 3, java.nio.charset.StandardCharsets.US_ASCII);
            if (!Arrays.asList("035", "037", "038", "039", "040", "041").contains(version))
                throw bad("Unsupported DEX version " + version);
            container = version.equals("041");
            fileSize = integer(base + 32);
            int headerSize = integer(base + 36);
            if (headerSize != (container ? 120 : 112) || fileSize < headerSize)
                throw bad("Invalid DEX header/file size");
            range(base, fileSize);
            if (integer(base + 40) != 0x12345678) throw bad("Unsupported DEX byte order");
            if (container) {
                range(base, 120);
                if (integer(base + 112) != bytes.length || integer(base + 116) != base)
                    throw bad("Invalid DEX 041 container bounds");
            } else if (base != 0 || fileSize != bytes.length) throw bad("Invalid legacy DEX bounds");
            strings = integer(base + 56); stringOff = integer(base + 60); table(strings, stringOff, 4);
            types = integer(base + 64); typeOff = integer(base + 68); table(types, typeOff, 4);
            protos = integer(base + 72); protoOff = integer(base + 76); table(protos, protoOff, 12);
            methods = integer(base + 88); methodOff = integer(base + 92); table(methods, methodOff, 8);
            classes = integer(base + 96); classOff = integer(base + 100); table(classes, classOff, 32);
        }
        IOException bad(String message) { return new IOException(message + " at DEX header " + base); }
        void range(int offset, long length) throws IOException {
            if (offset < base || length < 0 || length > bytes.length - (long) offset)
                throw bad("DEX range outside container");
        }
        int integer(int offset) throws IOException {
            range(offset, 4);
            long value = (bytes[offset] & 255L) | ((bytes[offset + 1] & 255L) << 8) |
                    ((bytes[offset + 2] & 255L) << 16) | ((bytes[offset + 3] & 255L) << 24);
            if (value > Integer.MAX_VALUE) throw bad("DEX integer exceeds parser bounds");
            return (int) value;
        }
        int ushort(int offset) throws IOException {
            range(offset, 2); return (bytes[offset] & 255) | ((bytes[offset + 1] & 255) << 8);
        }
        void table(int count, int offset, int width) throws IOException {
            if (count == 0) { if (offset != 0) throw bad("Empty DEX table has offset"); return; }
            if ((offset & 3) != 0) throw bad("Unaligned DEX table");
            range(offset, (long) count * width);
        }
        int uleb(int[] cursor) throws IOException {
            long value = 0;
            for (int i = 0; i < 5; ++i) {
                range(cursor[0], 1); int next = bytes[cursor[0]++] & 255;
                value |= (long) (next & 127) << (i * 7);
                if ((next & 128) == 0) {
                    if (value > Integer.MAX_VALUE) throw bad("ULEB exceeds parser bounds");
                    return (int) value;
                }
            }
            throw bad("Invalid ULEB encoding");
        }
        String string(int index) throws IOException {
            if (index < 0 || index >= strings) throw bad("Invalid string index");
            int[] cursor = {integer(stringOff + index * 4)};
            int length = uleb(cursor);
            if (length > bytes.length - cursor[0]) throw bad("Invalid string length");
            StringBuilder value = new StringBuilder(length);
            for (int i = 0; i < length; ++i) {
                range(cursor[0], 1); int first = bytes[cursor[0]++] & 255;
                int character;
                if (first >= 1 && first <= 127) character = first;
                else if ((first & 0xe0) == 0xc0) {
                    range(cursor[0], 1); int second = bytes[cursor[0]++] & 255;
                    if ((second & 0xc0) != 0x80) throw bad("Invalid MUTF-8 continuation");
                    character = ((first & 31) << 6) | (second & 63);
                    if (character < 128 && character != 0) throw bad("Overlong MUTF-8 sequence");
                } else if ((first & 0xf0) == 0xe0) {
                    range(cursor[0], 2); int second = bytes[cursor[0]++] & 255, third = bytes[cursor[0]++] & 255;
                    if ((second & 0xc0) != 0x80 || (third & 0xc0) != 0x80)
                        throw bad("Invalid MUTF-8 continuation");
                    character = ((first & 15) << 12) | ((second & 63) << 6) | (third & 63);
                    if (character < 2048) throw bad("Overlong MUTF-8 sequence");
                } else throw bad("Invalid MUTF-8 lead byte");
                value.append((char) character);
            }
            range(cursor[0], 1);
            if (bytes[cursor[0]] != 0) throw bad("Missing MUTF-8 terminator");
            return value.toString();
        }
        String type(int index) throws IOException {
            if (index < 0 || index >= types) throw bad("Invalid type index");
            String value = string(integer(typeOff + index * 4));
            int element = 0;
            while (element < value.length() && value.charAt(element) == '[') ++element;
            if (element > 255 || element == value.length()) throw bad("Invalid type descriptor");
            String tail = value.substring(element);
            boolean primitive = tail.length() == 1 && "VZBCSIJFD".contains(tail);
            boolean object = tail.length() >= 3 && tail.charAt(0) == 'L' && tail.endsWith(";") &&
                    tail.indexOf('.') == -1 && tail.indexOf(';') == tail.length() - 1;
            if ((!primitive && !object) || (element != 0 && tail.equals("V")))
                throw bad("Invalid type descriptor");
            return value;
        }
        String prototype(int index) throws IOException {
            if (index < 0 || index >= protos) throw bad("Invalid prototype index");
            int offset = protoOff + index * 12;
            String resultType = type(integer(offset + 4));
            StringBuilder result = new StringBuilder("("), shorty = new StringBuilder();
            shorty.append(resultType.charAt(0) == '[' || resultType.charAt(0) == 'L' ? 'L' : resultType.charAt(0));
            int parameters = integer(offset + 8);
            if (parameters != 0) {
                int count = integer(parameters); range(parameters + 4, (long) count * 2);
                for (int i = 0; i < count; ++i) {
                    String parameter = type(ushort(parameters + 4 + i * 2));
                    if (parameter.equals("V")) throw bad("Void parameter type");
                    result.append(parameter);
                    shorty.append(parameter.charAt(0) == '[' || parameter.charAt(0) == 'L' ? 'L' : parameter.charAt(0));
                }
            }
            if (!shorty.toString().equals(string(integer(offset)))) throw bad("Prototype shorty mismatch");
            return result.append(')').append(resultType).toString();
        }
        int[] find(Method[] reflected) throws IOException {
            String owner = descriptor(reflected[0].getDeclaringClass());
            int ownerType = -1, classData = -1;
            for (int i = 0; i < classes; ++i) {
                int definition = classOff + i * 32, index = integer(definition);
                if (owner.equals(type(index))) {
                    if (ownerType != -1) throw bad("Duplicate fixture class definition");
                    ownerType = index; classData = integer(definition + 24);
                }
            }
            if (ownerType == -1) return null;
            int[] found = new int[reflected.length]; Arrays.fill(found, -1);
            for (int i = 0; i < methods; ++i) {
                int method = methodOff + i * 8;
                if (ushort(method) != ownerType) continue;
                String name = string(integer(method + 4)), proto = prototype(ushort(method + 2));
                for (int j = 0; j < reflected.length; ++j) {
                    if (reflected[j].getName().equals(name) && descriptor(reflected[j]).equals(proto)) {
                        if (found[j] != -1) throw bad("Duplicate fixture method_id");
                        found[j] = i;
                    }
                }
            }
            for (int index : found) if (index == -1) throw bad("Fixture method_id missing");
            if (classData == 0) throw bad("Fixture class_data missing");
            int[] cursor = {classData};
            int staticFields = uleb(cursor), instanceFields = uleb(cursor);
            int directMethods = uleb(cursor), virtualMethods = uleb(cursor);
            if ((long) staticFields + instanceFields + directMethods + virtualMethods > bytes.length)
                throw bad("Invalid class_data counts");
            for (long i = 0; i < (long) staticFields + instanceFields; ++i) { uleb(cursor); uleb(cursor); }
            boolean[] defined = new boolean[found.length];
            for (int group : new int[]{directMethods, virtualMethods}) {
                int index = 0;
                for (int i = 0; i < group; ++i) {
                    int delta = uleb(cursor);
                    if (delta > Integer.MAX_VALUE - index || (i != 0 && delta == 0)) throw bad("Invalid encoded method delta");
                    index += delta;
                    if (index >= methods || ushort(methodOff + index * 8) != ownerType)
                        throw bad("Encoded method belongs to another class");
                    int flags = uleb(cursor), code = uleb(cursor);
                    for (int j = 0; j < found.length; ++j) if (found[j] == index) {
                        if (defined[j] || (flags & 0x11f) != (reflected[j].getModifiers() & 0x11f))
                            throw bad("Fixture class_data/modifier mismatch");
                        boolean noCode = (flags & (0x100 | 0x400)) != 0;
                        if (noCode != (code == 0)) throw bad("Fixture code presence mismatch");
                        if (code != 0) {
                            if ((code & 3) != 0) throw bad("Unaligned code_item");
                            range(code, 16); range(code + 16, (long) integer(code + 12) * 2);
                        }
                        defined[j] = true;
                    }
                }
            }
            for (boolean value : defined) if (!value) throw bad("Referenced fixture method is not defined by its class");
            return found;
        }
    }
}
