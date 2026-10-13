package com.github.ytlog.artdcevm.testing;

import android.content.Context;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * The part of a hot update that belongs to the tool, not the library: the
 * library redefines loaded classes, and new classes reach the app through its
 * class loader. For the classes of one update, each compiled to its own DEX:
 * <ol>
 *   <li>Classify through the app's loader without initializing. A class it
 *       finds exists; this also loads an existing class nothing has used yet,
 *       so it can be redefined. A class it cannot find is new.</li>
 *   <li>Append the new classes, as one read-only jar, after the loader's
 *       existing path with ART's add_to_dex_class_loader extension. Android 14
 *       rejects writable DEX files for apps that target it.</li>
 *   <li>Redefine every existing class in one standard RedefineClasses batch
 *       (the caller); the library loads the appended classes the new
 *       definitions name before ART's transaction.</li>
 * </ol>
 */
final class HotReloadTool {
    private final Context context;
    private final ClassLoader loader;
    private final List<String> added = new ArrayList<>();
    private final List<Class<?>> existing = new ArrayList<>();
    /** The extension's JVMTI code, or -1 when the update adds no class. */
    int appendCode = -1;

    HotReloadTool(Context context) {
        this.context = context;
        loader = context.getClassLoader();
    }

    /** Steps 1 and 2 for the classes of fixture `variant` in `packagePath`. */
    HotReloadTool prepare(String variant, String packagePath) throws Exception {
        String directory = variant + "/" + packagePath;
        String[] files = context.getAssets().list(directory);
        Arrays.sort(files);
        List<byte[]> dexes = new ArrayList<>();
        for (String file : files) {
            if (!file.endsWith(".dex")) continue;
            String name = (packagePath + "/" + file.substring(0, file.length() - 4)).replace('/', '.');
            try {
                existing.add(Class.forName(name, false, loader));
            } catch (ClassNotFoundException missing) {
                added.add(name);
                dexes.add(read(directory + "/" + file));
            }
        }
        if (!dexes.isEmpty()) appendCode = append(variant, dexes);
        return this;
    }

    Class<?>[] existingClasses() {
        return existing.toArray(new Class<?>[0]);
    }

    /** "added=A,B;changed=C" with simple names, for the scenario report. */
    String classification() {
        List<String> changed = new ArrayList<>();
        for (Class<?> type : existing) changed.add(type.getName());
        return "added=" + simpleNames(added) + ";changed=" + simpleNames(changed);
    }

    private static String simpleNames(List<String> names) {
        StringBuilder result = new StringBuilder();
        for (String name : names) {
            if (result.length() > 0) result.append(',');
            result.append(name.substring(name.lastIndexOf('.') + 1));
        }
        return result.toString();
    }

    private int append(String variant, List<byte[]> dexes) throws IOException {
        File jar = new File(context.getCodeCacheDir(), "hot-update-" + variant + ".jar");
        if (jar.exists() && !jar.delete()) throw new IOException("Cannot replace an earlier update");
        try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(jar))) {
            for (int index = 0; index < dexes.size(); index++) {
                zip.putNextEntry(new ZipEntry(index == 0 ? "classes.dex" : "classes" + (index + 1) + ".dex"));
                zip.write(dexes.get(index));
                zip.closeEntry();
            }
        }
        if (!jar.setReadOnly()) throw new IOException("Cannot make the update read-only");
        return StandardJvmtiClient.addToClassLoader(loader, jar.getPath());
    }

    private byte[] read(String path) throws IOException {
        try (InputStream input = context.getAssets().open(path);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            return output.toByteArray();
        }
    }
}
