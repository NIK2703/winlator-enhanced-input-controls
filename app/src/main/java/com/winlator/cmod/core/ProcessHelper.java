package com.winlator.cmod.core;

import android.os.Process;


import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FilenameFilter;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.Executors;

public abstract class ProcessHelper {
    public static final boolean PRINT_DEBUG = false;
    private static final ArrayList<Callback<String>> debugCallbacks = new ArrayList<>();
    private static final byte SIGCONT = 18;
    private static final byte SIGSTOP = 19;
    private static final byte SIGTERM = 15;
    private static final byte SIGKILL = 9;
    private static final String[] WINE_FILTERS = {"wine", "wine64", "wine-preloader", "wineserver", "box64", "FEXCore"};
    private static final java.util.HashSet<Integer> stoppedPids = new java.util.HashSet<>();

    public static void suspendProcess(int pid) {
        Process.sendSignal(pid, SIGSTOP);
    }

    public static void resumeProcess(int pid) {
        Process.sendSignal(pid, SIGCONT);
    }

    public static void terminateProcess(int pid) {
        Process.sendSignal(pid, SIGTERM);
    }

    public static void killProcess(int pid) {
        Process.sendSignal(pid, SIGKILL);
    }

    public static void terminateAllWineProcesses() {
        for (String process : listRunningWineProcesses()) {
            terminateProcess(Integer.parseInt(process));
        }
    }

    public static void pauseAllWineProcesses() {
        for (String process : listRunningWineProcesses()) {
            int pid = Integer.parseInt(process);
            synchronized (stoppedPids) {
                if (!stoppedPids.contains(pid)) {
                    suspendProcess(pid);
                    stoppedPids.add(pid);
                }
            }
        }
    }

    public static void resumeAllWineProcesses() {
        synchronized (stoppedPids) {
            for (int pid : stoppedPids) {
                if (isProcessAlive(pid)) {
                    resumeProcess(pid);
                }
            }
            stoppedPids.clear();

            // Handle orphaned stopped wine processes (PPID=1, state=T)
            // These can exist when box64 parent was killed while children were stopped
            for (String process : listRunningWineProcesses()) {
                int pid = Integer.parseInt(process);
                if (isProcessStopped(pid)) {
                    int ppid = getParentPid(pid);
                    if (ppid <= 1) {
                        Log.w("ProcessHelper", "Killing orphaned stopped wine process: pid=" + pid);
                        killProcess(pid);
                    } else {
                        Log.w("ProcessHelper", "Resuming orphaned stopped wine process: pid=" + pid);
                        resumeProcess(pid);
                    }
                }
            }
        }
    }

    public static boolean isProcessAlive(int pid) {
        return new File("/proc/" + pid).exists();
    }

    public static boolean isProcessAliveByName(int pid, String namePrefix) {
        try {
            BufferedReader br = new BufferedReader(new InputStreamReader(
                new FileInputStream("/proc/" + pid + "/stat")));
            String data = br.readLine();
            br.close();
            int commStart = data.indexOf('(');
            int commEnd = data.lastIndexOf(')');
            if (commStart < 0 || commEnd <= commStart) return false;
            String comm = data.substring(commStart + 1, commEnd);
            return comm.contains(namePrefix);
        } catch (IOException e) {
            return false;
        }
    }

    private static boolean isProcessStopped(int pid) {
        try {
            BufferedReader br = new BufferedReader(new InputStreamReader(
                new FileInputStream("/proc/" + pid + "/stat")));
            String data = br.readLine();
            br.close();
            int commStart = data.indexOf('(');
            int commEnd = data.lastIndexOf(')');
            if (commStart < 0 || commEnd <= commStart) return false;
            // After ") " comes the state character (field 3)
            String afterComm = data.substring(commEnd + 2);
            char state = afterComm.charAt(0);
            return state == 'T' || state == 't';
        } catch (IOException e) {
            return false;
        }
    }

    private static int getParentPid(int pid) {
        try {
            BufferedReader br = new BufferedReader(new InputStreamReader(
                new FileInputStream("/proc/" + pid + "/stat")));
            String data = br.readLine();
            br.close();
            int commStart = data.indexOf('(');
            int commEnd = data.lastIndexOf(')');
            if (commStart < 0 || commEnd <= commStart) return -1;
            // After ") " comes state (char), then space, then ppid (int)
            String afterComm = data.substring(commEnd + 2);
            String[] fields = afterComm.split("\\s+");
            return Integer.parseInt(fields[1]); // field index 1 = ppid
        } catch (Exception e) {
            return -1;
        }
    }

    public static int exec(String command) {
        return exec(command, null);
    }

    public static int exec(String command, String[] envp) {
        return exec(command, envp, null);
    }

    public static int exec(String command, String[] envp, File workingDir) {
        return exec(command, envp, workingDir, null);
    }

    public static int exec(String command, String[] envp, File workingDir, Callback<Integer> terminationCallback) {


        EnvironmentManager.setEnvVars(envp);

        int pid = -1;
        try {
            String[] splitCommand = splitCommand(command);
            ProcessBuilder pb = new ProcessBuilder(splitCommand);
            pb.directory(workingDir);
            pb.environment().putAll(EnvironmentManager.getEnvVars());
            if (debugCallbacks.isEmpty()) {
                File null_file = new File("/dev/null");
                pb.redirectError(null_file);
                pb.redirectOutput(null_file);
            }
            java.lang.Process process = pb.start();

            Field pidField = process.getClass().getDeclaredField("pid");
            pidField.setAccessible(true);
            pid = pidField.getInt(process);
            pidField.setAccessible(false);

            if (!debugCallbacks.isEmpty()) {
                createDebugThread(process.getInputStream());
                createDebugThread(process.getErrorStream());
            }

            if (terminationCallback != null) createWaitForThread(process, terminationCallback);

        }
        catch (Exception e) {

        }
        return pid;
    }

    private static void createDebugThread(final InputStream inputStream) {
        Executors.newSingleThreadExecutor().execute(() -> {
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(inputStream))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    if (PRINT_DEBUG) System.out.println(line);
                    synchronized (debugCallbacks) {
                        if (!debugCallbacks.isEmpty()) {
                            for (Callback<String> callback : debugCallbacks) callback.call(line);
                        }
                    }
                }
            }
            catch (IOException e) {

            }
        });
    }

    private static void createWaitForThread(java.lang.Process process, final Callback<Integer> terminationCallback) {
        Executors.newSingleThreadExecutor().execute(new Runnable() {
            @Override
            public void run() {
                try {
                    int status = process.waitFor();
                    terminationCallback.call(status);
                }
                catch (InterruptedException e) {

                }
            }
        });
    }

    public static void removeAllDebugCallbacks() {
        synchronized (debugCallbacks) {
            debugCallbacks.clear();

        }
    }

    public static void addDebugCallback(Callback<String> callback) {
        synchronized (debugCallbacks) {
            if (!debugCallbacks.contains(callback)) debugCallbacks.add(callback);

        }
    }

    public static void removeDebugCallback(Callback<String> callback) {
        synchronized (debugCallbacks) {
            debugCallbacks.remove(callback);

        }
    }

    public static String[] splitCommand(String command) {
        ArrayList<String> result = new ArrayList<>();
        boolean startedQuotes = false;
        String value = "";
        char currChar, nextChar;
        for (int i = 0, count = command.length(); i < count; i++) {
            currChar = command.charAt(i);

            if (startedQuotes) {
                if (currChar == '"') {
                    startedQuotes = false;
                    if (!value.isEmpty()) {
                        value += '"';
                        result.add(value);
                        value = "";
                    }
                }
                else value += currChar;
            }
            else if (currChar == '"') {
                startedQuotes = true;
                value += '"';
            }
            else {
                nextChar = i < count-1 ? command.charAt(i+1) : '\0';
                if (currChar == ' ' || (currChar == '\\' && nextChar == ' ')) {
                    if (currChar == '\\') {
                        value += ' ';
                        i++;
                    }
                    else if (!value.isEmpty()) {
                        result.add(value);
                        value = "";
                    }
                }
                else {
                    value += currChar;
                    if (i == count-1) {
                        result.add(value);
                        value = "";
                    }
                }
            }
        }

        return result.toArray(new String[0]);
    }

    public static String getAffinityMaskAsHexString(String cpuList) {
        String[] values = cpuList.split(",");
        int affinityMask = 0;
        for (String value : values) {
            byte index = Byte.parseByte(value);
            affinityMask |= (int)Math.pow(2, index);
        }
        return Integer.toHexString(affinityMask);
    }

    public static int getAffinityMask(String cpuList) {
        if (cpuList == null || cpuList.isEmpty()) return 0;
        String[] values = cpuList.split(",");
        int affinityMask = 0;
        for (String value : values) {
            byte index = Byte.parseByte(value);
            affinityMask |= (int)Math.pow(2, index);
        }
        return affinityMask;
    }

    public static int getAffinityMask(boolean[] cpuList) {
        int affinityMask = 0;
        for (int i = 0; i < cpuList.length; i++) {
            if (cpuList[i]) affinityMask |= (int)Math.pow(2, i);
        }
        return affinityMask;
    }

    public static int getAffinityMask(int from, int to) {
        int affinityMask = 0;
        for (int i = from; i < to; i++) affinityMask |= (int)Math.pow(2, i);
        return affinityMask;
    }

    public static ArrayList<String> listRunningWineProcesses(){
        File proc = new File("/proc");
        String[] allPids;
        ArrayList<String> filteredPids = new ArrayList<String>();
        allPids = proc.list(new FilenameFilter(){
            public boolean accept(File proc, String filename){
                return new File(proc, filename).isDirectory() && filename.matches("[0-9]+");
            }
        });
        if (allPids == null) return filteredPids;

        for (int index = 0; index < allPids.length; index++){
            String data = "";
            try {
                FileInputStream fr = new FileInputStream(proc + "/" + allPids[index] + "/stat");
                BufferedReader br = new BufferedReader(new InputStreamReader(fr));
                data = br.readLine();
                br.close();
            } catch (IOException e) { continue; }
            // Extract comm field: everything between first '(' and last ')'
            int commStart = data.indexOf('(');
            int commEnd = data.lastIndexOf(')');
            if (commStart < 0 || commEnd <= commStart) continue;
            String comm = data.substring(commStart + 1, commEnd);
            for (String filter : WINE_FILTERS) {
                if (comm.contains(filter)) {
                    filteredPids.add(allPids[index]);
                    break;
                }
            }
        }
        return filteredPids;
    }
}
