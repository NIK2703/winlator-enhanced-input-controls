package com.winlator.cmod.inputcontrols;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class BindPackage {

    private final List<Bind> bindings = new ArrayList<>();
    private final List<Boolean> stickyFlags = new ArrayList<>();
    private boolean toggleSwitch = false;
    private boolean autoRepeat = false;
    private int autoRepeatIntervalMs = 300;

    public BindPackage() {}

    public BindPackage(List<Bind> bindings) {
        for (Bind b : bindings) {
            this.bindings.add(b);
            this.stickyFlags.add(false);
        }
        this.toggleSwitch = false;
        this.autoRepeat = false;
    }

    public BindPackage(List<Bind> bindings, List<Boolean> stickyFlags) {
        for (int i = 0; i < bindings.size(); i++) {
            this.bindings.add(bindings.get(i));
            this.stickyFlags.add(i < stickyFlags.size() && stickyFlags.get(i) != null && stickyFlags.get(i));
        }
        this.toggleSwitch = false;
        this.autoRepeat = false;
    }

    public BindPackage(BindPackage other) {
        this.bindings.addAll(other.bindings);
        this.stickyFlags.addAll(other.stickyFlags);
        this.toggleSwitch = other.toggleSwitch;
        this.autoRepeat = other.autoRepeat;
        this.autoRepeatIntervalMs = other.autoRepeatIntervalMs;
    }

    public List<Bind> getBindings() {
        return Collections.unmodifiableList(bindings);
    }

    public void clear() {
        bindings.clear();
        stickyFlags.clear();
        toggleSwitch = false;
        autoRepeat = false;
        autoRepeatIntervalMs = 300;
    }

    public List<Boolean> getStickyFlags() {
        return Collections.unmodifiableList(stickyFlags);
    }

    public boolean isToggleSwitch() { return toggleSwitch; }

    public void setToggleSwitch(boolean toggleSwitch) { this.toggleSwitch = toggleSwitch; }

    public boolean isAutoRepeat() { return autoRepeat; }
    public void setAutoRepeat(boolean autoRepeat) { this.autoRepeat = autoRepeat; }
    public int getAutoRepeatIntervalMs() { return autoRepeatIntervalMs; }
    public void setAutoRepeatIntervalMs(int autoRepeatIntervalMs) { this.autoRepeatIntervalMs = autoRepeatIntervalMs; }

    public Bind get(int index) {
        return index >= 0 && index < bindings.size() ? bindings.get(index) : Bind.NONE;
    }

    public int size() {
        return bindings.size();
    }

    public boolean isEmpty() {
        for (Bind b : bindings) {
            if (b != null && b != Bind.NONE) return false;
        }
        return true;
    }


    public void add(Bind binding) {
        bindings.add(binding);
        stickyFlags.add(false);
    }

    public void set(int index, Bind binding) {
        if (index >= 0 && index < bindings.size()) {
            bindings.set(index, binding);
        }
    }

    public void remove(int index) {
        if (index >= 0 && index < bindings.size()) {
            bindings.remove(index);
            stickyFlags.remove(index);
        }
    }

    public BindPackage withBindingToggled(Bind binding, boolean add) {
        BindPackage copy = new BindPackage(this);
        if (add) {
            if (!copy.bindings.contains(binding)) {
                copy.bindings.add(binding);
                copy.stickyFlags.add(false);
            }
        } else {
            int idx = copy.bindings.indexOf(binding);
            if (idx >= 0) {
                copy.bindings.remove(idx);
                if (idx < copy.stickyFlags.size()) copy.stickyFlags.remove(idx);
            }
        }
        return copy;
    }

    public boolean isSticky(int index) {
        return index >= 0 && index < stickyFlags.size() && stickyFlags.get(index) != null && stickyFlags.get(index);
    }

    public void setSticky(int index, boolean sticky) {
        while (stickyFlags.size() <= index) stickyFlags.add(false);
        stickyFlags.set(index, sticky);
    }

    public void sync() {
        while (stickyFlags.size() < bindings.size()) stickyFlags.add(false);
        while (stickyFlags.size() > bindings.size()) stickyFlags.remove(stickyFlags.size() - 1);
    }

    public int[] encode() {
        int count = 0;
        for (Bind b : bindings) {
            if (b != null && b != Bind.NONE) count++;
        }
        int[] result = new int[count * 2];
        int idx = 0;
        for (Bind b : bindings) {
            if (b == null || b == Bind.NONE) continue;
            result[idx++] = b.encodeTypeValue();
            result[idx++] = b.encodeKeyValue();
        }
        return result;
    }

    public int encodeStickyBitmask() {
        int mask = 0;
        int nonNoneIdx = 0;
        for (int i = 0; i < bindings.size(); i++) {
            Bind b = bindings.get(i);
            if (b == null || b == Bind.NONE) continue;
            if (i < stickyFlags.size() && stickyFlags.get(i) != null && stickyFlags.get(i)) {
                mask |= (1 << nonNoneIdx);
            }
            nonNoneIdx++;
        }
        return mask;
    }

    public int encodeToggleBitmask() {
        int mask = 0;
        int nonNoneIdx = 0;
        for (int i = 0; i < bindings.size(); i++) {
            Bind b = bindings.get(i);
            if (b == null || b == Bind.NONE) continue;
            if (toggleSwitch) {
                mask |= (1 << nonNoneIdx);
            }
            nonNoneIdx++;
        }
        return mask;
    }

    public int encodeAutoRepeatBitmask() {
        int mask = 0;
        int nonNoneIdx = 0;
        for (int i = 0; i < bindings.size(); i++) {
            Bind b = bindings.get(i);
            if (b == null || b == Bind.NONE) continue;
            if (autoRepeat) {
                mask |= (1 << nonNoneIdx);
            }
            nonNoneIdx++;
        }
        return mask;
    }

    public JSONArray toJSONArray() {
        JSONArray arr = new JSONArray();
        for (Bind b : bindings) {
            if (b != null && b != Bind.NONE) arr.put(b.name());
        }
        return arr;
    }

    public JSONObject toJSON() {
        JSONObject obj = new JSONObject();
        try {
            JSONArray bindingsArr = new JSONArray();
            JSONArray stickyArr = new JSONArray();
            for (int i = 0; i < bindings.size(); i++) {
                Bind b = bindings.get(i);
                if (b != null && b != Bind.NONE) {
                    bindingsArr.put(b.name());
                    stickyArr.put(isSticky(i));
                }
            }
            obj.put("bindings", bindingsArr);
            obj.put("sticky", stickyArr);
            obj.put("toggleSwitch", toggleSwitch);
            obj.put("autoRepeat", autoRepeat);
            obj.put("autoRepeatIntervalMs", autoRepeatIntervalMs);
        } catch (JSONException e) {}
        return obj;
    }

    public static BindPackage fromJSON(JSONObject obj, Bind defaultBinding) {
        JSONArray bindingsArray = obj.optJSONArray("bindings");
        BindPackage bp = fromJSONArray(bindingsArray, defaultBinding);
        bp.setToggleSwitch(obj.optBoolean("toggleSwitch", false));
        bp.setAutoRepeat(obj.optBoolean("autoRepeat", false));
        bp.setAutoRepeatIntervalMs(obj.optInt("autoRepeatIntervalMs", 300));
        JSONArray stickyArray = obj.optJSONArray("sticky");
        if (stickyArray != null) {
            for (int i = 0; i < stickyArray.length() && i < bp.size(); i++) {
                bp.setSticky(i, stickyArray.optBoolean(i, false));
            }
        }
        return bp;
    }

    public JSONArray stickyToJSONArray() {
        JSONArray arr = new JSONArray();
        for (int i = 0; i < bindings.size(); i++) {
            boolean sticky = i < stickyFlags.size() && stickyFlags.get(i) != null && stickyFlags.get(i);
            arr.put(sticky ? 1 : 0);
        }
        return arr;
    }

    public static BindPackage fromJSONArray(JSONArray arr, Bind defaultBinding) {
        BindPackage bp = new BindPackage();
        if (arr != null) {
            for (int i = 0; i < arr.length(); i++) {
                try {
                    bp.add(ControlsProfile.parseBinding(arr.getString(i), defaultBinding));
                } catch (JSONException e) {
                    bp.add(defaultBinding);
                }
            }
        }
        return bp;
    }

    public static BindPackage fromJSONArray(JSONArray arr, JSONArray stickyArr, Bind defaultBinding) {
        BindPackage bp = fromJSONArray(arr, defaultBinding);
        if (stickyArr != null) {
            for (int i = 0; i < stickyArr.length() && i < bp.bindings.size(); i++) {
                try {
                    bp.stickyFlags.set(i, stickyArr.getInt(i) != 0);
                } catch (JSONException e) {
                }
            }
        }
        return bp;
    }

    public static BindPackage fromSingle(Bind binding) {
        BindPackage bp = new BindPackage();
        bp.bindings.add(binding);
        bp.stickyFlags.add(false);
        return bp;
    }
}
