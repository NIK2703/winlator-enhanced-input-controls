package com.winlator.cmod.inputcontrols;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import com.winlator.cmod.inputcontrols.ControlElement.BindingSection;
import com.winlator.cmod.inputcontrols.ControlElement.Type;

class ElementBindings {
    final ControlElement element;
    List<List<Bind>> bindings = new ArrayList<>();
    List<List<Boolean>> bindingSticky = new ArrayList<>();
    List<Bind> longPressBindings = new ArrayList<>();
    List<Bind> gestureBindings = new ArrayList<>();
    BindPackage[] slotPackages;
    BindPackage longPressPackageVal;
    BindPackage gesturePackageVal;
    Bind[] binds = new Bind[]{Bind.NONE, Bind.NONE, Bind.NONE, Bind.NONE};
    boolean[] states = new boolean[4];

    ElementBindings(ControlElement element) {
        this.element = element;
        for (int i = 0; i < 4; i++) {
            bindings.add(new ArrayList<Bind>());
            bindingSticky.add(new ArrayList<Boolean>());
        }
        slotPackages = new BindPackage[4];
        for (int i = 0; i < 4; i++) slotPackages[i] = new BindPackage();
        longPressPackageVal = new BindPackage();
        gesturePackageVal = new BindPackage();
    }

    void reset(Type type) {
        for (List<Bind> seq : bindings) {
            seq.clear();
        }
        for (List<Boolean> seq : bindingSticky) {
            seq.clear();
        }

        if (type == Type.STICK) {
            bindings.get(0).add(Bind.KEY_W);
            bindings.get(1).add(Bind.KEY_D);
            bindings.get(2).add(Bind.KEY_S);
            bindings.get(3).add(Bind.KEY_A);
        }
        else if (type == Type.D_PAD) {
            bindings.get(0).add(Bind.GAMEPAD_DPAD_UP);
            bindings.get(1).add(Bind.GAMEPAD_DPAD_RIGHT);
            bindings.get(2).add(Bind.GAMEPAD_DPAD_DOWN);
            bindings.get(3).add(Bind.GAMEPAD_DPAD_LEFT);
        }
        else if (type == Type.TRACKPAD) {
            bindings.get(0).add(Bind.GAMEPAD_RIGHT_THUMB_UP);
            bindings.get(1).add(Bind.GAMEPAD_RIGHT_THUMB_RIGHT);
            bindings.get(2).add(Bind.GAMEPAD_RIGHT_THUMB_DOWN);
            bindings.get(3).add(Bind.GAMEPAD_RIGHT_THUMB_LEFT);
        }
    }

    int getBindingCount() {
        return bindings.size();
    }

    void setBindingCount(int bindingCount) {
        while (bindings.size() < bindingCount) {
            List<Bind> seq = new ArrayList<>();
            seq.add(Bind.NONE);
            bindings.add(seq);
        }
        while (bindings.size() > bindingCount) {
            bindings.remove(bindings.size() - 1);
        }
        while (bindingSticky.size() < bindingCount) {
            bindingSticky.add(new ArrayList<Boolean>());
        }
        while (bindingSticky.size() > bindingCount) {
            bindingSticky.remove(bindingSticky.size() - 1);
        }
        states = new boolean[bindingCount];
        element.boundingBoxNeedsUpdate = true;
        element.invalidateElementCache();
    }

    Bind getBindingAt(int index) {
        if (index >= bindings.size()) return Bind.NONE;
        List<Bind> seq = bindings.get(index);
        return seq.isEmpty() ? Bind.NONE : seq.get(0);
    }

    void setBindingAt(int index, Bind binding) {
        while (index >= bindings.size()) {
            bindings.add(new ArrayList<Bind>());
        }
        List<Bind> seq = bindings.get(index);
        seq.clear();
        seq.add(binding);
        while (index >= bindingSticky.size()) {
            bindingSticky.add(new ArrayList<Boolean>());
        }
        List<Boolean> stickySeq = bindingSticky.get(index);
        stickySeq.clear();
        stickySeq.add(false);
        element.markDisplayTextDirty();
        element.invalidateElementCache();
    }

    void setBinding(Bind binding) {
        for (int i = 0; i < bindings.size(); i++) {
            List<Bind> seq = bindings.get(i);
            seq.clear();
            seq.add(binding);
        }
        for (List<Boolean> seq : bindingSticky) {
            seq.clear();
            seq.add(false);
        }
        element.markDisplayTextDirty();
        element.invalidateElementCache();
    }

    List<Bind> getBindingSequence(int index) {
        if (index >= bindings.size()) return Collections.emptyList();
        return Collections.unmodifiableList(bindings.get(index));
    }

    void setBindingSequence(int index, List<Bind> sequence) {
        while (index >= bindings.size()) {
            bindings.add(new ArrayList<Bind>());
        }
        List<Bind> seq = bindings.get(index);
        seq.clear();
        seq.addAll(sequence);
        while (index >= bindingSticky.size()) {
            bindingSticky.add(new ArrayList<Boolean>());
        }
        List<Boolean> stickySeq = bindingSticky.get(index);
        while (stickySeq.size() < sequence.size()) {
            stickySeq.add(false);
        }
        while (stickySeq.size() > sequence.size()) {
            stickySeq.remove(stickySeq.size() - 1);
        }
        element.invalidateElementCache();
    }

    void addBindingToSequence(int index, Bind binding) {
        while (index >= bindings.size()) {
            bindings.add(new ArrayList<Bind>());
        }
        bindings.get(index).add(binding);
        while (index >= bindingSticky.size()) {
            bindingSticky.add(new ArrayList<Boolean>());
        }
        bindingSticky.get(index).add(false);
        element.invalidateElementCache();
    }

    void removeBindingFromSequence(int index, int seqIndex) {
        if (index < bindings.size()) {
            List<Bind> seq = bindings.get(index);
            if (seqIndex >= 0 && seqIndex < seq.size()) {
                seq.remove(seqIndex);
                if (index < bindingSticky.size()) {
                    List<Boolean> stickySeq = bindingSticky.get(index);
                    if (seqIndex < stickySeq.size()) {
                        stickySeq.remove(seqIndex);
                    }
                }
                element.invalidateElementCache();
            }
        }
    }

    boolean isBindingSticky(int slot, int index) {
        if (slot >= bindingSticky.size()) return false;
        List<Boolean> seq = bindingSticky.get(slot);
        if (index >= seq.size()) return false;
        Boolean val = seq.get(index);
        return val != null && val;
    }

    void setBindingSticky(int slot, int index, boolean sticky) {
        while (slot >= bindingSticky.size()) {
            bindingSticky.add(new ArrayList<Boolean>());
        }
        List<Boolean> seq = bindingSticky.get(slot);
        while (index >= seq.size()) {
            seq.add(false);
        }
        seq.set(index, sticky);
    }

    List<Boolean> getBindingSticky(int slot) {
        if (slot >= bindingSticky.size()) return Collections.emptyList();
        return Collections.unmodifiableList(bindingSticky.get(slot));
    }

    void setBindingAtSequenceIndex(int slotIndex, int seqIndex, Bind binding) {
        if (slotIndex < bindings.size()) {
            List<Bind> seq = bindings.get(slotIndex);
            if (seqIndex >= 0 && seqIndex < seq.size()) {
                seq.set(seqIndex, binding);
                element.invalidateElementCache();
            }
        }
    }

    List<Bind> getLongPressBindings() {
        return longPressBindings;
    }

    void setLongPressBindings(List<Bind> bindings) {
        longPressBindings.clear();
        longPressBindings.addAll(bindings);
    }

    void addLongPressBinding(Bind binding) {
        longPressBindings.add(binding);
    }

    void removeLongPressBinding(int index) {
        if (index >= 0 && index < longPressBindings.size()) {
            longPressBindings.remove(index);
        }
    }

    boolean hasLongPressBinding() {
        if (longPressBindings == null || longPressBindings.isEmpty()) return false;
        if (longPressBindings.size() == 1 && longPressBindings.get(0) == Bind.NONE) return false;
        return true;
    }

    List<Bind> getGestureBindings() {
        return gestureBindings;
    }

    void setGestureBindings(List<Bind> bindings) {
        gestureBindings.clear();
        gestureBindings.addAll(bindings);
    }

    void addGestureBinding(Bind binding) {
        gestureBindings.add(binding);
    }

    void removeGestureBinding(int index) {
        if (index >= 0 && index < gestureBindings.size()) {
            gestureBindings.remove(index);
        }
    }

    boolean hasGestureBinding() {
        if (gestureBindings == null || gestureBindings.isEmpty()) return false;
        if (gestureBindings.size() == 1 && gestureBindings.get(0) == Bind.NONE) return false;
        return true;
    }

    BindPackage getSlotPackage(int index) {
        if (slotPackages != null && index >= 0 && index < slotPackages.length) {
            return slotPackages[index];
        }
        return new BindPackage();
    }

    void setSlotPackage(int index, BindPackage pkg) {
        if (slotPackages != null && index >= 0 && index < slotPackages.length) {
            slotPackages[index] = pkg;
            while (bindings.size() <= index) {
                bindings.add(new ArrayList<Bind>());
                bindingSticky.add(new ArrayList<Boolean>());
            }
            List<Bind> seq = bindings.get(index);
            seq.clear();
            List<Boolean> stickySeq = bindingSticky.get(index);
            stickySeq.clear();
            if (pkg != null) {
                for (int k = 0; k < pkg.size(); k++) {
                    seq.add(pkg.get(k));
                    stickySeq.add(pkg.isSticky(k));
                }
            }
            element.invalidateElementCache();
        }
    }

    BindPackage getLongPressPackage() {
        return longPressPackageVal != null ? longPressPackageVal : new BindPackage();
    }

    void setLongPressPackage(BindPackage pkg) {
        longPressPackageVal = pkg;
        longPressBindings.clear();
        if (pkg != null) {
            for (int k = 0; k < pkg.size(); k++) {
                longPressBindings.add(pkg.get(k));
            }
        }
    }

    BindPackage getGesturePackage() {
        return gesturePackageVal != null ? gesturePackageVal : new BindPackage();
    }

    void setGesturePackage(BindPackage pkg) {
        gesturePackageVal = pkg;
        gestureBindings.clear();
        if (pkg != null) {
            for (int k = 0; k < pkg.size(); k++) {
                gestureBindings.add(pkg.get(k));
            }
        }
    }

    Bind getBind(int index) {
        if (index >= 0 && index < 4) return binds[index];
        return Bind.NONE;
    }

    void setBind(int index, Bind binding) {
        if (index >= 0 && index < 4) {
            binds[index] = binding != null ? binding : Bind.NONE;
            while (bindings.size() <= index) {
                bindings.add(new ArrayList<Bind>());
                bindingSticky.add(new ArrayList<Boolean>());
            }
            List<Bind> seq = bindings.get(index);
            seq.clear();
            if (binds[index] != Bind.NONE) {
                seq.add(binds[index]);
            }
            element.invalidateElementCache();
        }
    }

    boolean hasAnySlotToggle() {
        if (slotPackages != null) {
            for (BindPackage pkg : slotPackages) {
                if (pkg != null && pkg.isToggleSwitch()) return true;
            }
        }
        return false;
    }

    boolean isLongPressToggle() {
        return longPressPackageVal != null && longPressPackageVal.isToggleSwitch();
    }

    boolean isGestureToggle() {
        return gesturePackageVal != null && gesturePackageVal.isToggleSwitch();
    }

    BindPackage getBindingPackage(BindingSection section) {
        switch (section) {
            case SLOT_0: return getSlotPackage(0);
            case SLOT_1: return getSlotPackage(1);
            case SLOT_2: return getSlotPackage(2);
            case SLOT_3: return getSlotPackage(3);
            case LONG_PRESS: return getLongPressPackage();
            case GESTURE: return getGesturePackage();
            default: return new BindPackage();
        }
    }

    void setBindingPackage(BindingSection section, BindPackage pkg) {
        switch (section) {
            case SLOT_0: setSlotPackage(0, pkg); break;
            case SLOT_1: setSlotPackage(1, pkg); break;
            case SLOT_2: setSlotPackage(2, pkg); break;
            case SLOT_3: setSlotPackage(3, pkg); break;
            case LONG_PRESS: setLongPressPackage(pkg); break;
            case GESTURE: setGesturePackage(pkg); break;
        }
    }

    List<Bind> getBindingsList(BindingSection section) {
        switch (section) {
            case SLOT_0: return getBindingSequence(0);
            case SLOT_1: return getBindingSequence(1);
            case SLOT_2: return getBindingSequence(2);
            case SLOT_3: return getBindingSequence(3);
            case LONG_PRESS: return getLongPressBindings();
            case GESTURE: return getGestureBindings();
            default: return new ArrayList<>();
        }
    }

    boolean isBindingToggle(BindingSection section) {
        switch (section) {
            case LONG_PRESS: return isLongPressToggle();
            case GESTURE: return isGestureToggle();
            case SLOT_0: case SLOT_1: case SLOT_2: case SLOT_3: {
                BindPackage pkg = getBindingPackage(section);
                return pkg != null && pkg.isToggleSwitch();
            }
            default: return false;
        }
    }

    boolean getBindingAutoRepeat(BindingSection section) {
        switch (section) {
            case SLOT_0: return getSlotAutoRepeat(0);
            case SLOT_1: return getSlotAutoRepeat(1);
            case SLOT_2: return getSlotAutoRepeat(2);
            case SLOT_3: return getSlotAutoRepeat(3);
            default: return false;
        }
    }

    boolean hasBinding(BindingSection section) {
        switch (section) {
            case SLOT_0: case SLOT_1: case SLOT_2: case SLOT_3: {
                int idx = section.ordinal();
                List<Bind> seq = idx < bindings.size() ? bindings.get(idx) : null;
                if (seq != null) {
                    for (Bind b : seq) {
                        if (b != null && b != Bind.NONE) return true;
                    }
                }
                BindPackage pkg = getBindingPackage(section);
                return pkg != null && !pkg.isEmpty();
            }
            case LONG_PRESS: return hasLongPressBinding();
            case GESTURE: return hasGestureBinding();
            default: return false;
        }
    }

    int computeToggleBitmask(BindingSection section) {
        int mask = 0, idx = 0;
        for (Bind b : getBindingsList(section)) {
            if (b == null || b == Bind.NONE) continue;
            if (isBindingToggle(section)) mask |= (1 << idx);
            idx++;
        }
        return mask;
    }

    boolean getSlotAutoRepeat(int slot) {
        if (slotPackages != null && slot >= 0 && slot < slotPackages.length && slotPackages[slot] != null) {
            return slotPackages[slot].isAutoRepeat();
        }
        return false;
    }

    int getSlotAutoRepeatIntervalMs(int slot) {
        if (slotPackages != null && slot >= 0 && slot < slotPackages.length && slotPackages[slot] != null) {
            return slotPackages[slot].getAutoRepeatIntervalMs();
        }
        return 100;
    }

    boolean hasAnyAction() {
        Type type = element.getType();
        for (int i = 0; i < bindings.size(); i++) {
            List<Bind> seq = bindings.get(i);
            if (seq != null) {
                for (Bind b : seq) {
                    if (b != null && b != Bind.NONE) return true;
                }
            }
        }
        if (type == Type.BUTTON) {
            if (slotPackages != null) {
                for (BindPackage pkg : slotPackages) {
                    if (pkg != null && !pkg.isEmpty()) return true;
                }
            }
        } else {
            for (int i = 0; i < 4; i++) {
                if (binds[i] != null && binds[i] != Bind.NONE) return true;
            }
        }
        if (hasLongPressBinding()) return true;
        if (hasGestureBinding()) return true;
        return false;
    }
}
