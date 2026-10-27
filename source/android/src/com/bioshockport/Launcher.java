package com.bioshockport;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.preference.PreferenceManager;
import android.provider.Settings;

import android.view.Gravity;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Iterator;

/**
 * Launcher of the BioShock port. Replaces WinlatorXR's own launcher so its UI is never shown:
 *  1. finds the game folder ("cache") in the root of the headset storage by its name,
 *  2. on the very first start lets WinlatorXR unpack its Windows environment (the only time
 *     any of its screens appears), waiting for the container in the background,
 *  3. writes the container settings and the BioShock shortcut baked in by the PC script,
 *  4. starts the game straight in VR.
 * Everything it needs comes from assets/bioshockport/port.json, written by the PC script.
 */
public class Launcher extends Activity {
    private static final String TAG = "BioshockPort";
    private static final String CONFIG_ASSET = "bioshockport/port.json";
    private static final String APPLIED_FILE = ".bioshockport_applied";
    private static final long POLL_MILLIS = 2000;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private JSONObject config;
    private TextView status;
    private boolean waitingForStorage;
    private boolean setupStarted;
    private boolean launched;
    private JSONObject override = new JSONObject();
    private boolean installing;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setBackgroundColor(Color.BLACK);
        status.setTextSize(22);
        status.setGravity(Gravity.CENTER);
        status.setPadding(48, 48, 48, 48);
        setContentView(status);

        try {
            config = new JSONObject(readAsset(CONFIG_ASSET));
        } catch (Exception e) {
            PortLog.e(TAG, "Missing " + CONFIG_ASSET, e);
            showStatus("Port configuration is missing from the APK. Rebuild it with the PC script.");
            return;
        }
        start();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (waitingForStorage && hasStorageAccess()) {
            waitingForStorage = false;
            start();
        }
    }

    @Override protected void onDestroy(){
        handler.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    private void start() {
        if (!hasStorageAccess()) {
            waitingForStorage = true;
            showStatus("Allow \"All files access\" so the game can read its folder.");
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
            return;
        }

        File gameDir = findGameDir();
        if (gameDir == null) {
            showStatus("Game folder not found.\n\nCopy the folder \"" + config.optString("cacheName")
                    + "\" made by the PC script to the root of the headset storage\n(next to Download, Android, ...).");
            return;
        }
        loadOverride(gameDir);
        if (installing) return;
        installing = true;
        final File source = gameDir;
        showStatus("Preparing the game files...");
        new Thread(new Runnable() {
            @Override
            public void run() {
                final File installed = installInternal(source);
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        if(isFinishing()||isDestroyed())return;
                        installing = false;
                        if (installed != null) startInstalled(installed);
                    }
                });
            }
        }, "BioshockPortInstall").start();
    }

    /**
     * The game runs from the app's own storage: on /sdcard (FUSE) every file Wine opens costs a
     * case-insensitive directory scan, and the engine probes dozens of paths per package, which
     * stretched the start to many minutes. Only new or changed files are copied after the first
     * time, so files updated in the storage root still reach the game.
     */
    private File installInternal(File source) {
        File target = new File(getFilesDir(), "game");
        final long[] totals = new long[2];  // bytes total, bytes copied
        java.util.ArrayList<File> files = new java.util.ArrayList<>();
        collect(source, files);
        for (File f : files) totals[0] += f.length();
        long started = System.currentTimeMillis();
        long lastShown = 0;
        try {
            String root = source.getAbsolutePath();
            for (File f : files) {
                File dst = new File(target, f.getAbsolutePath().substring(root.length()));
                if (dst.isFile() && dst.length() == f.length() && dst.lastModified() >= f.lastModified()) {
                    totals[1] += f.length();
                    continue;
                }
                File parent = dst.getParentFile();
                if (parent != null && !parent.isDirectory()) parent.mkdirs();
                copyFile(f, dst);
                dst.setLastModified(f.lastModified());
                totals[1] += f.length();
                long now = System.currentTimeMillis();
                if (now - lastShown > 500) {
                    lastShown = now;
                    final int percent = (int) (totals[1] * 100 / Math.max(1, totals[0]));
                    handler.post(new Runnable() {
                        @Override
                        public void run() {
                            showStatus("Installing the game into the app: " + percent + "%\n(only the first time)");
                        }
                    });
                }
            }
        } catch (final Exception e) {
            PortLog.e(TAG, "Install failed", e);
            handler.post(new Runnable() {
                @Override
                public void run() {
                    showStatus("Could not install the game files: " + e.getMessage()
                            + "\n\nIs there enough free space?");
                }
            });
            return null;
        }
        PortLog.i(TAG, "Game files in " + target + " (" + (System.currentTimeMillis() - started) + " ms)");
        return target;
    }

    private static void collect(File dir, java.util.List<File> out) {
        File[] list = dir.listFiles();
        if (list == null) return;
        for (File f : list) {
            if (f.isDirectory()) collect(f, out);
            else {
                String name = f.getName().toLowerCase(java.util.Locale.ROOT);
                // These belong to a running Wine session, not the game cache.
                if (name.endsWith(".log") || name.equals("vrshot.bmp") || name.equals("hudshot.bmp")
                        || name.equals("vrcmd.txt") || name.endsWith(".dmp")) continue;
                out.add(f);
            }
        }
    }

    private static void copyFile(File src, File dst) throws Exception {
        File tmp = new File(dst.getPath() + ".part");
        try (InputStream in = new FileInputStream(src); FileOutputStream out = new FileOutputStream(tmp)) {
            byte[] buffer = new byte[1 << 20];
            int n;
            while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
        }
        if (dst.exists()) dst.delete();
        if (!tmp.renameTo(dst)) throw new Exception("cannot write " + dst);
    }

    private void startInstalled(File gameDir) {
        if (tryLaunch(gameDir)) return;

        // First start: WinlatorXR's own activity unpacks the system image and creates the
        // default container on its own; this keeps polling until both are there.
        showStatus("First start: preparing the Windows environment.\nThis takes a minute and happens only once.");
        disableXrHints();
        if (!setupStarted) {
            setupStarted = true;
            try {
                startActivity(new Intent(this, Class.forName("com.winlator.cmod.MainActivity")));
            } catch (ClassNotFoundException e) {
                showStatus("This APK is not built on WinlatorXR.");
                return;
            }
        }
        final File dir = gameDir;
        handler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if(isFinishing()||isDestroyed())return;
                if (!launched && !tryLaunch(dir)) handler.postDelayed(this, POLL_MILLIS);
            }
        }, POLL_MILLIS);
    }

    private boolean hasStorageAccess() {
        return Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager();
    }

    /** The folder by its exact name first, then any root folder carrying the port's marker file. */
    private File findGameDir() {
        File root = Environment.getExternalStorageDirectory();
        String name = config.optString("cacheName");
        String marker = config.optString("marker");
        String exe = config.optString("exeRelative").replace('\\', '/');

        File exact = new File(root, name);
        if (new File(exact, exe).isFile()) return exact;

        File[] dirs = root.listFiles();
        if (dirs == null) return null;
        for (File dir : dirs) {
            if (!dir.isDirectory()) continue;
            if (dir.getName().equalsIgnoreCase(name) && new File(dir, exe).isFile()) return dir;
        }
        for (File dir : dirs) {
            if (dir.isDirectory() && new File(dir, marker).isFile() && new File(dir, exe).isFile()) return dir;
        }
        return null;
    }

    private boolean tryLaunch(File gameDir) {
        if (launched) return true;
        File imageFs = new File(getFilesDir(), "imagefs");
        if (!new File(imageFs, ".winlator/.img_version").isFile()) return false;

        File containerDir = findContainer(new File(imageFs, "home"));
        if (containerDir == null) return false;
        int containerId = Integer.parseInt(containerDir.getName().substring("xuser-".length()));

        try {
            File shortcut = applyPort(containerDir, containerId, gameDir);
            launchGame(containerId, shortcut, gameDir);
            return true;
        } catch (Exception e) {
            PortLog.e(TAG, "Could not set up the container", e);
            showStatus("Could not set up the game: " + e.getMessage());
            launched = true; // stop polling, the message stays on screen
            return true;
        }
    }

    /** The container named after the port, else the first finished one WinlatorXR made. */
    private File findContainer(File homeDir) {
        File[] dirs = homeDir.listFiles();
        if (dirs == null) return null;
        String wanted = config.optJSONObject("container") != null
                ? config.optJSONObject("container").optString("name") : "";
        File best = null;
        int bestId = Integer.MAX_VALUE;
        for (File dir : dirs) {
            String n = dir.getName();
            if (!n.startsWith("xuser-")) continue;
            int id;
            try {
                id = Integer.parseInt(n.substring(6));
            } catch (NumberFormatException e) {
                continue;
            }
            File conf = new File(dir, ".container");
            // WinlatorXR saves .container only after the prefix has been unpacked
            if (!conf.isFile() || !new File(dir, ".wine/drive_c").isDirectory()) continue;
            try {
                if (new JSONObject(readFile(conf)).optString("name").equals(wanted)) return dir;
            } catch (Exception ignored) {
            }
            if (id < bestId) {
                bestId = id;
                best = dir;
            }
        }
        return best;
    }

    /** Writes container settings and the shortcut once per port version, so in-game tweaks survive. */
    private File applyPort(File containerDir, int containerId, File gameDir) throws Exception {
        // A changed override rewrites the shortcut too
        String version = config.optString("version") + "/" + override.optJSONObject("shortcutExtras");
        String drives = "D:" + gameDir.getAbsolutePath() + "E:/data/data/" + getPackageName() + "/storage";
        File desktopDir = new File(containerDir, ".wine/drive_c/users/xuser/Desktop");
        File shortcut = new File(desktopDir, config.optString("shortcutName", "BioShock") + ".desktop");
        File applied = new File(containerDir, APPLIED_FILE);

        boolean upToDate = applied.isFile() && shortcut.isFile()
                && readFile(applied).trim().equals(version + "|" + drives);
        if (upToDate) return shortcut;

        File confFile = new File(containerDir, ".container");
        JSONObject container = new JSONObject(readFile(confFile));
        JSONObject wanted = config.optJSONObject("container");
        if (wanted != null) {
            for (Iterator<String> it = wanted.keys(); it.hasNext(); ) {
                String key = it.next();
                container.put(key, wanted.get(key));
            }
        }
        container.put("drives", drives);
        writeFile(confFile, container.toString());

        // Профиль эмулятора FEX для игры (WinlatorXR читает .fex-emu/AppConfig/<exe>.json)
        JSONObject fex = config.optJSONObject("fexAppConfig");
        if (fex != null) {
            File dir = new File(containerDir, ".fex-emu/AppConfig");
            if (dir.isDirectory() || dir.mkdirs())
                writeFile(new File(dir, fex.optString("exe") + ".json"), fex.optJSONObject("config").toString());
        }

        StringBuilder desktop = new StringBuilder();
        String exe = config.optString("exeRelative");
        // WinlatorXR's reader unescapes every backslash from four characters
        String winePath = ("D:\\" + exe).replace("\\", "\\\\\\\\");
        desktop.append("[Desktop Entry]\n")
                .append("Name=").append(config.optString("shortcutName", "BioShock")).append('\n')
                .append("Exec=wine ").append(winePath).append('\n')
                .append("Type=Application\n")
                .append("StartupNotify=true\n")
                .append("Icon=\n")
                .append("StartupWMClass=").append(new File(exe.replace('\\', '/')).getName()).append("\n\n")
                .append("[Extra Data]\n")
                .append("container_id:").append(containerId).append('\n');
        JSONObject extras = new JSONObject();
        merge(extras, config.optJSONObject("shortcutExtras"));
        merge(extras, override.optJSONObject("shortcutExtras"));
        for (Iterator<String> it = extras.keys(); it.hasNext(); ) {
            String key = it.next();
            desktop.append(key).append('=').append(extras.getString(key)).append('\n');
        }
        if (!desktopDir.isDirectory() && !desktopDir.mkdirs()) throw new Exception("cannot create " + desktopDir);
        writeFile(shortcut, desktop.toString());
        // BioShock copies Default.ini into its own Bioshock.ini/User.ini on the first start and
        // ignores Default.ini afterwards; removing them lets a new port version's settings apply
        resetGameConfig(new File(containerDir, ".wine/drive_c/users/xuser"), 0);
        writeFile(applied, version + "|" + drives);
        return shortcut;
    }

    /**
     * Optional port_override.json in the game folder, so the port can be tuned on the headset
     * without rebuilding the APK: {"immersive": false, "sbs": false,
     * "shortcutExtras": {"execArgs": "-dx9"}, "prefs": {"enable_wine_debug": true}}.
     * "prefs" go to WinlatorXR's own settings (e.g. Wine logs in Download/Winlator/logs).
     */
    private void loadOverride(File gameDir) {
        override = new JSONObject();
        File file = new File(gameDir, "port_override.json");
        if (!file.isFile()) return;
        try {
            override = new JSONObject(readFile(file));
        } catch (Exception e) {
            PortLog.e(TAG, "Ignoring broken " + file, e);
            return;
        }
        JSONObject prefs = override.optJSONObject("prefs");
        if (prefs == null) return;
        SharedPreferences.Editor editor = PreferenceManager.getDefaultSharedPreferences(this).edit();
        for (Iterator<String> it = prefs.keys(); it.hasNext(); ) {
            String key = it.next();
            Object value = prefs.opt(key);
            if (value instanceof Boolean) editor.putBoolean(key, (Boolean) value);
            else if (value instanceof Integer) editor.putInt(key, (Integer) value);
            else if (value instanceof Number) editor.putFloat(key, ((Number) value).floatValue());
            else editor.putString(key, String.valueOf(value));
        }
        editor.apply();
    }

    private static void resetGameConfig(File dir, int depth) {
        File[] files = dir.listFiles();
        if (files == null || depth > 5) return;
        for (File file : files) {
            String name = file.getName();
            if (file.isDirectory()) {
                resetGameConfig(file, depth + 1);
            } else if (dir.getName().equalsIgnoreCase("Bioshock")
                    && (name.equalsIgnoreCase("Bioshock.ini") || name.equalsIgnoreCase("User.ini"))) {
                PortLog.i(TAG, "Resetting " + file + ": " + file.delete());
            }
        }
    }

    private static void merge(JSONObject into, JSONObject from) throws Exception {
        if (from == null) return;
        for (Iterator<String> it = from.keys(); it.hasNext(); ) {
            String key = it.next();
            into.put(key, from.get(key));
        }
    }

    private void launchGame(int containerId, File shortcut, File gameDir) throws Exception {
        launched = true;
        JSONObject vr = config.optJSONObject("vr");
        Class<?> target;
        String maker = Build.MANUFACTURER;
        if ("OCULUS".equalsIgnoreCase(maker) || "META".equalsIgnoreCase(maker)) {
            target = VrGame.class;
        } else if ("PICO".equalsIgnoreCase(maker)) {
            target = Class.forName("com.winlator.xr.runtime.Pico");
        } else {
            target = Class.forName("com.winlator.cmod.XServerDisplayActivity");
        }
        Intent intent = new Intent(this, target);
        intent.putExtra("container_id", containerId);
        intent.putExtra("shortcut_path", shortcut.getAbsolutePath());
        boolean immersive = override.optBoolean("immersive", vr == null || vr.optBoolean("immersive", true));
        boolean sbs = override.optBoolean("sbs", vr != null && vr.optBoolean("sbs", false));
        // Without the ReShade stereo build each eye would get half of a flat frame
        File exe = new File(gameDir, config.optString("exeRelative").replace('\\', '/'));
        if (!new File(exe.getParentFile(), "d3d9.dll").isFile()) sbs = false;
        intent.putExtra(VrGame.EXTRA_IMMERSIVE, immersive);
        intent.putExtra(VrGame.EXTRA_SBS, sbs);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK
                | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PortLog.i(TAG, "Starting " + target.getName() + " container " + containerId + " " + shortcut);
        startActivity(intent);
        finish();
    }

    private void disableXrHints() {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        prefs.edit().putBoolean("xr_disable_startup_hints", true).apply();
    }

    private void showStatus(String text) {
        status.setText(config != null && config.has("title")
                ? config.optString("title") + "\n\n" + text : text);
    }

    private String readAsset(String name) throws Exception {
        try (InputStream in = getAssets().open(name)) {
            return readStream(in);
        }
    }

    private static String readFile(File file) throws Exception {
        try (InputStream in = new FileInputStream(file)) {
            return readStream(in);
        }
    }

    private static String readStream(InputStream in) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int n;
        while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
        return new String(out.toByteArray(), StandardCharsets.UTF_8);
    }

    private static void writeFile(File file, String text) throws Exception {
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
}
