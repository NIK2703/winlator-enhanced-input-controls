package com.winlator.cmod.inputcontrols;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.media.MediaScannerConnection;
import android.net.Uri;
import android.os.Environment;
import android.util.JsonReader;
import android.util.JsonToken;

import java.util.ArrayList;
import java.util.List;

import androidx.preference.PreferenceManager;

import com.winlator.cmod.SettingsFragment;
import com.winlator.cmod.core.AppUtils;
import com.winlator.cmod.core.FileUtils;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;

public class InputControlsManager {
    private final Context context;
    private ArrayList<ControlsProfile> profiles;
    private int maxProfileId;
    private boolean profilesLoaded = false;

    public InputControlsManager(Context context) {
        this.context = context;
    }

    public static File getProfilesDir(Context context) {
        File profilesDir = new File(context.getFilesDir(), "profiles");
        if (!profilesDir.isDirectory()) profilesDir.mkdir();
        return profilesDir;
    }

    public ArrayList<ControlsProfile> getProfiles() {
        return getProfiles(false);
    }

    public ArrayList<ControlsProfile> getProfiles(boolean ignoreTemplates) {
        if (!profilesLoaded) loadProfiles(ignoreTemplates);
        return profiles;
    }

    private void copyAssetProfilesIfNeeded() {
        File profilesDir = InputControlsManager.getProfilesDir(context);
        if (FileUtils.isEmpty(profilesDir)) {
            FileUtils.copy(context, "inputcontrols/profiles", profilesDir);
            return;
        }

        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(context);

        int newVersion = AppUtils.getVersionCode(context);
        int oldVersion = preferences.getInt("inputcontrols_app_version", 0);
        if (oldVersion == newVersion) return;
        preferences.edit().putInt("inputcontrols_app_version", newVersion).apply();

        File[] files = profilesDir.listFiles((d, n) -> n.endsWith(".icp"));
        if (files == null) return;

        try {
            AssetManager assetManager = context.getAssets();
            String[] assetFiles = assetManager.list("inputcontrols/profiles");
            for (String assetFile : assetFiles) {
                String assetPath = "inputcontrols/profiles/"+assetFile;
                ControlsProfile originProfile = loadProfile(context, assetManager.open(assetPath));
                if (originProfile == null) continue;

                File targetFile = null;
                for (File file : files) {
                    ControlsProfile targetProfile = loadProfile(context, file);
                    if (targetProfile != null && originProfile.id == targetProfile.id && originProfile.getName().equals(targetProfile.getName())) {
                        targetFile = file;
                        break;
                    }
                }

                if (targetFile != null) {
                    FileUtils.copy(context, assetPath, targetFile);
                }
            }
        }
        catch (IOException e) {}
    }

    public void loadProfiles(boolean ignoreTemplates) {
        File profilesDir = InputControlsManager.getProfilesDir(context);
        copyAssetProfilesIfNeeded();

        ArrayList<ControlsProfile> profiles = new ArrayList<>();
        File[] files = profilesDir.listFiles((d, n) -> n.endsWith(".icp"));
        if (files != null) {
            for (File file : files) {
                ControlsProfile profile = loadProfile(context, file);
                if (profile != null) {
                    if (!(ignoreTemplates && profile.isTemplate())) profiles.add(profile);
                    maxProfileId = Math.max(maxProfileId, profile.id);
                }
            }
        }

        Collections.sort(profiles);
        this.profiles = profiles;
        profilesLoaded = true;
    }

    public ControlsProfile createProfile(String name) {
        ControlsProfile profile = new ControlsProfile(context, ++maxProfileId);
        profile.setName(name);
        profile.save();
        profiles.add(profile);
        return profile;
    }

    public ControlsProfile duplicateProfile(ControlsProfile source) {
        String newName;
        for (int i = 1;;i++) {
            newName = source.getName() + " ("+i+")";
            boolean found = false;
            for (ControlsProfile profile : profiles) {
                if (profile.getName().equals(newName)) {
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }

        int newId = ++maxProfileId;
        File newFile = ControlsProfile.getProfileFile(context, newId);

        try {
            JSONObject data = new JSONObject(FileUtils.readString(ControlsProfile.getProfileFile(context, source.id)));
            data.put("id", newId);
            data.put("name", newName);
            if (data.has("template")) data.remove("template");
            FileUtils.writeString(newFile, data.toString());
        }
        catch (JSONException e) {}

        ControlsProfile profile = loadProfile(context, newFile);
        profiles.add(profile);
        return profile;
    }

    public void removeProfile(ControlsProfile profile) {
        File file = ControlsProfile.getProfileFile(context, profile.id);
        if (file.isFile() && file.delete()) profiles.remove(profile);
    }

    public ControlsProfile importProfile(JSONObject data) {
        try {
            if (!data.has("id") || !data.has("name")) return null;
            int newId = ++maxProfileId;
            File newFile = ControlsProfile.getProfileFile(context, newId);
            data.put("id", newId);
            FileUtils.writeString(newFile, data.toString());
            ControlsProfile newProfile = loadProfile(context, newFile);

            int foundIndex = -1;
            for (int i = 0; i < profiles.size(); i++) {
                ControlsProfile profile = profiles.get(i);
                if (profile.getName().equals(newProfile.getName())) {
                    foundIndex = i;
                    break;
                }
            }

            if (foundIndex != -1) {
                profiles.set(foundIndex, newProfile);
            }
            else profiles.add(newProfile);
            return newProfile;
        }
        catch (JSONException e) {
            return null;
        }
    }

    public File exportProfile(ControlsProfile profile) {
        File destination;
        SharedPreferences sp = PreferenceManager.getDefaultSharedPreferences(context);
        String winlatorPath = sp.getString("winlator_path_uri", null);
        if (winlatorPath != null) {
            Uri winlatorUri = Uri.parse(winlatorPath);
            destination = new File(FileUtils.getFilePathFromUri(context, winlatorUri), "profiles/" + profile.getName() + ".icp");
        }
        else {
            destination = new File(SettingsFragment.DEFAULT_WINLATOR_PATH, "profiles/" + profile.getName() + ".icp");
        }
        FileUtils.copy(ControlsProfile.getProfileFile(context, profile.id), destination);
        MediaScannerConnection.scanFile(context, new String[]{destination.getAbsolutePath()}, null, null);
        return destination.isFile() ? destination : null;
    }

    public static ControlsProfile loadProfile(Context context, File file) {
        try {
            return loadProfile(context, new FileInputStream(file));
        }
        catch (FileNotFoundException e) {
            return null;
        }
    }

    private static List<Binding> readBindingListOld(JsonReader reader) {
        List<Binding> list = new ArrayList<>();
        try {
            if (reader.peek() == JsonToken.BEGIN_ARRAY) {
                reader.beginArray();
                while (reader.hasNext()) {
                    String str = reader.nextString();
                    Binding b = ControlsProfile.parseBinding(str, Binding.NONE);
                    if (b != Binding.NONE) list.add(b);
                }
                reader.endArray();
            } else {
                String str = reader.nextString();
                Binding b = ControlsProfile.parseBinding(str, Binding.NONE);
                if (b != Binding.NONE) list.add(b);
            }
        } catch (IOException e) {
            // ignore
        }
        return list;
    }

    public static ControlsProfile loadProfile(Context context, InputStream inStream) {
        try (JsonReader reader = new JsonReader(new InputStreamReader(inStream, StandardCharsets.UTF_8))) {
            int profileId = 0;
            String profileName = null;
            float cursorSpeed = Float.NaN;

            // Buffered gesture values
            String mouseModeStr = null;
            String inputModeStr = null;
            String dragModeStr = null;
            List<Binding> singleTapAction = null;
            List<Binding> longPressAction = null;
            List<Binding> doubleTapAction = null;
            List<Binding> singleTap2ndFingerAction = null;
            List<Binding> longPress2ndFingerAction = null;
            List<Binding> doubleTap2ndFingerAction = null;
            List<Binding> singleTapDragAction = null;
            List<Binding> longPressDragAction = null;
            List<Binding> doubleTapDragAction = null;
            List<Binding> singleTap2ndFingerDragAction = null;
            List<Binding> longPress2ndFingerDragAction = null;
            List<Binding> doubleTap2ndFingerDragAction = null;
            int bindingDelay = -1;
            int longPressDelay = -1;
            int longPressHapticIntensity = -1;
            int dragThreshold = -1;
            int doubleTapTimeout = -1;
            int longPressTimeout = -1;
            String secondFingerModeStr = null;

            reader.beginObject();
            while (reader.hasNext()) {
                String name = reader.nextName();

                if (name.equals("id")) {
                    profileId = reader.nextInt();
                }
                else if (name.equals("name")) {
                    profileName = reader.nextString();
                }
                else if (name.equals("cursorSpeed")) {
                    cursorSpeed = (float) reader.nextDouble();
                }
                else if (name.equals("bindingDelay")) {
                    bindingDelay = reader.nextInt();
                }
                else if (name.equals("longPressDelay")) {
                    longPressDelay = reader.nextInt();
                }
                else if (name.equals("longPressHapticIntensity")) {
                    longPressHapticIntensity = reader.nextInt();
                }
                else if (name.equals("dragThreshold")) {
                    dragThreshold = reader.nextInt();
                }
                else if (name.equals("touchscreenGestures")) {
                    reader.beginObject();
                    while (reader.hasNext()) {
                        String key = reader.nextName();
                        switch (key) {
                            case "mouseMode": mouseModeStr = reader.nextString(); break;
                            case "inputMode": inputModeStr = reader.nextString(); break;
                            case "dragMode": dragModeStr = reader.nextString(); break;
                            case "singleTapAction": singleTapAction = readBindingListOld(reader); break;
                            case "longPressAction": longPressAction = readBindingListOld(reader); break;
                            case "doubleTapAction": doubleTapAction = readBindingListOld(reader); break;
                            case "singleTap2ndFingerAction": singleTap2ndFingerAction = readBindingListOld(reader); break;
                            case "longPress2ndFingerAction": longPress2ndFingerAction = readBindingListOld(reader); break;
                            case "doubleTap2ndFingerAction": doubleTap2ndFingerAction = readBindingListOld(reader); break;
                            case "singleTapDragAction": singleTapDragAction = readBindingListOld(reader); break;
                            case "longPressDragAction": longPressDragAction = readBindingListOld(reader); break;
                            case "doubleTapDragAction": doubleTapDragAction = readBindingListOld(reader); break;
                            case "singleTap2ndFingerDragAction": singleTap2ndFingerDragAction = readBindingListOld(reader); break;
                            case "longPress2ndFingerDragAction": longPress2ndFingerDragAction = readBindingListOld(reader); break;
                            case "doubleTap2ndFingerDragAction": doubleTap2ndFingerDragAction = readBindingListOld(reader); break;
                            case "doubleTapTimeout": doubleTapTimeout = reader.nextInt(); break;
                            case "longPressTimeout": longPressTimeout = reader.nextInt(); break;
                            case "secondFingerMode": secondFingerModeStr = reader.nextString(); break;
                            default: reader.skipValue(); break;
                        }
                    }
                    reader.endObject();
                }
                else {
                    reader.skipValue();
                }
            }

            ControlsProfile profile = new ControlsProfile(context, profileId);
            profile.setName(profileName);
            if (!Float.isNaN(cursorSpeed)) profile.setCursorSpeed(cursorSpeed);

            // Apply buffered gesture values
            if (mouseModeStr != null) profile.setMouseMode(ControlsProfile.parseMouseMode(mouseModeStr));
            if (inputModeStr != null) profile.setInputMode(ControlsProfile.parseInputMode(inputModeStr));
            if (dragModeStr != null) profile.setDragMode(ControlsProfile.parseDragMode(dragModeStr));
            if (singleTapAction != null) profile.setSingleTapAction(singleTapAction);
            if (longPressAction != null) profile.setLongPressAction(longPressAction);
            if (doubleTapAction != null) profile.setDoubleTapAction(doubleTapAction);
            if (singleTap2ndFingerAction != null) profile.setSingleTap2ndFingerAction(singleTap2ndFingerAction);
            if (longPress2ndFingerAction != null) profile.setLongPress2ndFingerAction(longPress2ndFingerAction);
            if (doubleTap2ndFingerAction != null) profile.setDoubleTap2ndFingerAction(doubleTap2ndFingerAction);
            if (singleTapDragAction != null) profile.setSingleTapDragAction(singleTapDragAction);
            if (longPressDragAction != null) profile.setLongPressDragAction(longPressDragAction);
            if (doubleTapDragAction != null) profile.setDoubleTapDragAction(doubleTapDragAction);
            if (singleTap2ndFingerDragAction != null) profile.setSingleTap2ndFingerDragAction(singleTap2ndFingerDragAction);
            if (longPress2ndFingerDragAction != null) profile.setLongPress2ndFingerDragAction(longPress2ndFingerDragAction);
            if (doubleTap2ndFingerDragAction != null) profile.setDoubleTap2ndFingerDragAction(doubleTap2ndFingerDragAction);
            if (doubleTapTimeout >= 0) profile.setDoubleTapTimeout(doubleTapTimeout);
            if (longPressTimeout >= 0) profile.setLongPressTimeout(longPressTimeout);
            if (secondFingerModeStr != null) profile.setSecondFingerMode(ControlsProfile.parseEnum(SecondFingerMode.class, secondFingerModeStr, SecondFingerMode.SECOND_TAP_ACTIONS));
            if (bindingDelay >= 0) profile.setBindingDelay(bindingDelay);
            if (longPressDelay >= 0) profile.setLongPressDelay(longPressDelay);
            if (longPressHapticIntensity >= 0) profile.setLongPressHapticIntensity(longPressHapticIntensity);
            if (dragThreshold >= 0) profile.setDragThreshold(dragThreshold);

            profile.markGestureSettingsLoaded();
            return profile;
        }
        catch (IOException e) {
            return null;
        }
    }

    public ControlsProfile getProfile(int id) {
        for (ControlsProfile profile : getProfiles()) if (profile.id == id) return profile;
        return null;
    }
}
