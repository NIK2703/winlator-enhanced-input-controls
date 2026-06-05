package com.winlator.cmod;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ShortcutInfo;
import android.content.pm.ShortcutManager;
import android.graphics.Bitmap;
import android.graphics.drawable.Icon;
import android.os.Build;

import android.widget.Toast;

public class ShortcutBroadcastReceiver extends BroadcastReceiver {

    private static final String LOG_TAG = "ShortcutBroadcastReceiver";

    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        if (action != null && action.equals("com.winlator.SHORTCUT_ADDED")) {
            boolean isShortcutAdded = intent.getBooleanExtra("shortcut_added", false);
            if (isShortcutAdded) {
                Toast.makeText(context, "Sorry, your device may not be supported", Toast.LENGTH_SHORT).show(); // yeah. I'm at a loss here.
            } else {
                Toast.makeText(context, "Failed to add shortcut.", Toast.LENGTH_SHORT).show();

                addShortcutToHomeScreen(context, intent);
            }
        }
    }

    private void addShortcutToHomeScreen(Context context, Intent originalIntent) {
        String shortcutName = originalIntent.getStringExtra("shortcut_name");
        Bitmap shortcutIcon = originalIntent.getParcelableExtra("shortcut_icon");
        Intent shortcutIntent = originalIntent.getParcelableExtra(Intent.EXTRA_SHORTCUT_INTENT);

        if (shortcutName != null && shortcutIcon != null && shortcutIntent != null) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                ShortcutManager shortcutManager = context.getSystemService(ShortcutManager.class);

                if (shortcutManager != null && shortcutManager.isRequestPinShortcutSupported()) {
                    ShortcutInfo pinShortcutInfo = new ShortcutInfo.Builder(context, shortcutName)
                            .setShortLabel(shortcutName)
                            .setIcon(Icon.createWithBitmap(shortcutIcon))
                            .setIntent(shortcutIntent)
                            .build();

                    boolean result = shortcutManager.requestPinShortcut(pinShortcutInfo, null);

                    if (result) {
                        Toast.makeText(context, "Shortcut added successfully from BroadcastReceiver!", Toast.LENGTH_SHORT).show();
                    }
                }
            } else {
                Intent addIntent = new Intent();
                addIntent.putExtra(Intent.EXTRA_SHORTCUT_INTENT, shortcutIntent);
                addIntent.putExtra(Intent.EXTRA_SHORTCUT_NAME, shortcutName);
                addIntent.putExtra(Intent.EXTRA_SHORTCUT_ICON, shortcutIcon);
                addIntent.setAction("com.android.launcher.action.INSTALL_SHORTCUT");

                try {
                    context.sendBroadcast(addIntent);
                    Toast.makeText(context, "Shortcut added successfully (Broadcast).", Toast.LENGTH_SHORT).show();
                } catch (Exception e) {
                    Toast.makeText(context, "Failed to add shortcut via broadcast.", Toast.LENGTH_SHORT).show();
                }
            }
        }
    }
}



