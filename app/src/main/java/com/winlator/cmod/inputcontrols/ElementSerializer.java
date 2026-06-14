package com.winlator.cmod.inputcontrols;

import org.json.JSONObject;
import org.json.JSONArray;
import org.json.JSONException;
import java.util.List;

public class ElementSerializer {
    private ElementSerializer() {}

    public static JSONObject toJSONObject(ControlElement element) {
        try {
            JSONObject elementJSONObject = new JSONObject();
            elementJSONObject.put("type", element.type.name());
            elementJSONObject.put("shape", element.shape.name());
            elementJSONObject.put("elementWidth", element.elementWidth);
            elementJSONObject.put("elementHeight", element.elementHeight);
            if (element.cornerRadius >= 0) elementJSONObject.put("cornerRadius", element.cornerRadius);

            JSONArray bindingsJSONArray = new JSONArray();
            for (List<Bind> seq : element.bindings.bindings) {
                JSONArray seqArray = new JSONArray();
                for (Bind b : seq) {
                    if (b != null && b != Bind.NONE) seqArray.put(b.name());
                }
                bindingsJSONArray.put(seqArray);
            }

            elementJSONObject.put("bindings", bindingsJSONArray);
            elementJSONObject.put("scale", Float.valueOf(element.scale));
            elementJSONObject.put("x", (float)element.x / element.inputControlsView.getMaxWidth());
            elementJSONObject.put("y", (float)element.y / element.inputControlsView.getMaxHeight());
            elementJSONObject.put("toggleSwitch", element.bindings.hasAnySlotToggle());

            JSONArray stickyArray = new JSONArray();
            for (List<Boolean> seq : element.bindings.bindingSticky) {
                JSONArray seqArray = new JSONArray();
                for (Boolean b : seq) {
                    seqArray.put(b != null && b);
                }
                stickyArray.put(seqArray);
            }
            elementJSONObject.put("bindingSticky", stickyArray);
            if (element.passthroughTouch) elementJSONObject.put("passthroughTouch", true);
            if (element.opacity >= 0) elementJSONObject.put("opacity", element.opacity);
            elementJSONObject.put("text", element.text);
            elementJSONObject.put("iconId", element.iconId);
            if (element.hasCustomIcon()) elementJSONObject.put("customIconData", element.customIconData);

            if (element.bindings.hasLongPressBinding()) {
                JSONArray lpArray = new JSONArray();
                for (Bind b : element.bindings.longPressBindings) {
                    if (b != null && b != Bind.NONE) lpArray.put(b.name());
                }
                elementJSONObject.put("longPressBindings", lpArray);
            }

            if (element.bindings.hasGestureBinding()) {
                JSONArray gArray = new JSONArray();
                for (Bind b : element.bindings.gestureBindings) {
                    if (b != null && b != Bind.NONE) gArray.put(b.name());
                }
                elementJSONObject.put("gestureBindings", gArray);
            }

            if (element.type == ControlElement.Type.BUTTON) {
                JSONArray slotPackagesArray = new JSONArray();
                if (element.bindings.slotPackages != null) {
                    slotPackagesArray.put(element.bindings.slotPackages[0] != null ? element.bindings.slotPackages[0].toJSON() : new BindPackage().toJSON());
                }
                elementJSONObject.put("slotPackages", slotPackagesArray);
                if (element.bindings.longPressPackageVal != null) elementJSONObject.put("longPressPackage", element.bindings.longPressPackageVal.toJSON());
                if (element.bindings.gesturePackageVal != null) elementJSONObject.put("gesturePackage", element.bindings.gesturePackageVal.toJSON());
            } else {
                JSONArray simpleArr = new JSONArray();
                for (int i = 0; i < 4; i++) {
                    Bind b = element.bindings.binds[i];
                    simpleArr.put(b != null && b != Bind.NONE ? b.name() : "NONE");
                }
                elementJSONObject.put("binds", simpleArr);
            }

            if (element.type == ControlElement.Type.RANGE_BUTTON && element.range != null) {
                elementJSONObject.put("range", element.range.name());
                if (element.orientation != 0) elementJSONObject.put("orientation", element.orientation);
            }

            return elementJSONObject;
        }
        catch (JSONException e) {
            return null;
        }
    }

    public static JSONObject toOriginalJSONObject(ControlElement element) {
        try {
            JSONObject obj = new JSONObject();
            obj.put("type", element.type.name());
            String origShape = element.shape.name();
            if (element.shape == ControlElement.Shape.RECT && element.elementWidth == 5f && element.elementHeight == 5f && element.cornerRadius >= 0.74f && element.cornerRadius <= 0.76f)
                origShape = "SQUARE";
            else if (element.shape == ControlElement.Shape.RECT && element.elementWidth == 8f && element.elementHeight == 4f && element.cornerRadius >= 1.99f && element.cornerRadius <= 2.01f)
                origShape = "ROUND_RECT";
            obj.put("shape", origShape);

            JSONArray flatBindings = new JSONArray();
            int count = Math.max(4, element.bindings.bindings.size());
            if (element.type == ControlElement.Type.RANGE_BUTTON) count = 6;
            for (int i = 0; i < count; i++) {
                Bind b = Bind.NONE;
                if (element.type == ControlElement.Type.BUTTON) {
                    if (element.bindings.slotPackages != null && i < element.bindings.slotPackages.length && element.bindings.slotPackages[i] != null && !element.bindings.slotPackages[i].isEmpty())
                        b = element.bindings.slotPackages[i].get(0);
                } else {
                    if (i < element.bindings.binds.length && element.bindings.binds[i] != null && element.bindings.binds[i] != Bind.NONE)
                        b = element.bindings.binds[i];
                }
                if (b == Bind.NONE && i < element.bindings.bindings.size()) {
                    List<Bind> seq = element.bindings.bindings.get(i);
                    if (!seq.isEmpty()) b = seq.get(0);
                }
                flatBindings.put(b != null ? b.name() : "NONE");
            }
            obj.put("bindings", flatBindings);
            obj.put("toggleSwitch", element.bindings.hasAnySlotToggle());
            obj.put("scale", Float.valueOf(element.scale));
            obj.put("x", (float)element.x / element.inputControlsView.getMaxWidth());
            obj.put("y", (float)element.y / element.inputControlsView.getMaxHeight());
            obj.put("text", element.text);
            obj.put("iconId", element.iconId);
            if (element.type == ControlElement.Type.RANGE_BUTTON && element.range != null) {
                obj.put("range", element.range.name());
                if (element.orientation != 0) obj.put("orientation", element.orientation);
            }
            return obj;
        }
        catch (JSONException e) {
            return null;
        }
    }
}
