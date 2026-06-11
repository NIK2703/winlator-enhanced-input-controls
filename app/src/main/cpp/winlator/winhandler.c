/*
 * winhandler.c for Winlator copetrol and process management.
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <psapi.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>
#include <windows.h>
#include <winsock2.h>

#define SERVER_PORT 7946
#define CLIENT_PORT 7947
#define BUFFER_SIZE 64
#define POLL_INTERVAL_MS 1000

#define RC_EXIT 0
#define RC_INIT 1
#define RC_EXEC 2
#define RC_KILL_PROCESS 3
#define RC_LIST_PROCESSES 4
#define RC_GET_PROCESS 5
#define RC_SET_PROCESS_AFFINITY 6
#define RC_MOUSE_EVENT 7
#define RC_GET_GAMEPAD 8
#define RC_GET_GAMEPAD_STATE 9
#define RC_RELEASE_GAMEPAD 10
#define RC_KEYBOARD_EVENT 11
#define RC_BRING_TO_FRONT 12
#define RC_CURSOR_POS_FEEDBACK 13
#define MAX_CMDLINE 2048

#pragma pack(push, 1)
typedef struct {
  BYTE code;
  DWORD padding;
  WORD numProcesses;
  WORD index;
  DWORD pid;
  DWORD64 memory;
  DWORD affinity;
  BYTE isWow64;
  char name[32];
} ProcessPacket;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  BYTE code;
  BYTE _pad[4];
  int pid;
  int mask;
  BYTE nameLen;
} SetAffinityMsg;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  BYTE code;
  int nameLen;
} BringToFrontMsg;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  int nameLen;
} KillProcessMsg;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  int filenameLen;
  int paramsLen;
} ExecMsg;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  BYTE code;
  BYTE _pad[4];
  int flags;
  short dx;
  short dy;
  short wheel;
  BYTE feedback;
} MouseEventMsg;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  BYTE code;
  BYTE vkey;
  int flags;
} KeyboardEventMsg;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  uint8_t cmd;
  int32_t pid;
} ExecHeader;
#pragma pack(pop)

SOCKET sock;
struct sockaddr_in clientAddr;
int clientAddrLen = sizeof(clientAddr);
volatile BOOL running = TRUE;

typedef struct PidNode {
  DWORD pid;
  struct PidNode *next;
} PidNode;

PidNode *seenPidsHead = NULL;

void addSeenPid(DWORD pid) {
  PidNode *newNode = (PidNode *)malloc(sizeof(PidNode));
  if (!newNode)
    return;
  newNode->pid = pid;
  newNode->next = seenPidsHead;
  seenPidsHead = newNode;
}

BOOL isPidSeen(DWORD pid) {
  PidNode *current = seenPidsHead;
  while (current) {
    if (current->pid == pid)
      return TRUE;
    current = current->next;
  }
  return FALSE;
}

void freeSeenPids(void) {
  PidNode *current = seenPidsHead;
  while (current) {
    PidNode *next = current->next;
    free(current);
    current = next;
  }
  seenPidsHead = NULL;
}

typedef BOOL (*ProcessMatchCallback)(void *ctx, DWORD pid, const char *name);

static BOOL enumerate_processes_by_name(const char *name, ProcessMatchCallback cb,
                                        void *ctx) {
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap == INVALID_HANDLE_VALUE)
    return FALSE;
  PROCESSENTRY32 pe32;
  pe32.dwSize = sizeof(PROCESSENTRY32);
  BOOL found = FALSE;
  if (Process32First(hSnap, &pe32)) {
    do {
      if (_stricmp(pe32.szExeFile, name) == 0) {
        if (cb(ctx, pe32.th32ProcessID, pe32.szExeFile)) {
          found = TRUE;
          break;
        }
      }
    } while (Process32Next(hSnap, &pe32));
  }
  CloseHandle(hSnap);
  return found;
}

typedef void (*ProcessAction)(HANDLE hProcess, void *ctx);

typedef struct {
  int mask;
} AffinityCtx;

typedef struct {
  ProcessPacket *packet;
} ListProcessCtx;

static bool with_process(DWORD access, DWORD pid, ProcessAction action, void *ctx) {
  HANDLE h = OpenProcess(access, FALSE, pid);
  if (!h)
    return false;
  action(h, ctx);
  CloseHandle(h);
  return true;
}

static void kill_action(HANDLE hProcess, void *ctx) {
  TerminateProcess(hProcess, 1);
}

static void set_affinity_action(HANDLE hProcess, void *ctx) {
  AffinityCtx *actx = (AffinityCtx *)ctx;
  SetProcessAffinityMask(hProcess, actx->mask);
}

static void list_process_action(HANDLE hProcess, void *ctx) {
  ListProcessCtx *lctx = (ListProcessCtx *)ctx;
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
    lctx->packet->memory = pmc.WorkingSetSize;
  }
  DWORD_PTR procAffinity, sysAffinity;
  if (GetProcessAffinityMask(hProcess, &procAffinity, &sysAffinity)) {
    lctx->packet->affinity = (DWORD)procAffinity;
  }
  BOOL isWow64 = FALSE;
  IsWow64Process(hProcess, &isWow64);
  lctx->packet->isWow64 = (BYTE)isWow64;
}

void sendCursorFeedback() {
  POINT ptr;
  if (GetCursorPos(&ptr)) {
    BYTE buffer[5];
    buffer[0] = RC_CURSOR_POS_FEEDBACK;
    *(int16_t *)(buffer + 1) = (int16_t)(ptr.x > INT16_MAX ? INT16_MAX : ptr.x < INT16_MIN ? INT16_MIN : ptr.x);
    *(int16_t *)(buffer + 3) = (int16_t)(ptr.y > INT16_MAX ? INT16_MAX : ptr.y < INT16_MIN ? INT16_MIN : ptr.y);
    sendto(sock, (const char *)buffer, 5, 0, (struct sockaddr *)&clientAddr,
           clientAddrLen);
  }
}

void handleListProcesses() {
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap == INVALID_HANDLE_VALUE)
    return;

  PROCESSENTRY32 pe32;
  pe32.dwSize = sizeof(PROCESSENTRY32);

  int count = 0;
  if (Process32First(hSnap, &pe32)) {
    do {
      count++;
    } while (Process32Next(hSnap, &pe32));
  }

  if (count == 0) {
    CloseHandle(hSnap);
    return;
  }

  if (!Process32First(hSnap, &pe32)) {
    CloseHandle(hSnap);
    return;
  }

  int index = 0;
  do {
    ProcessPacket packet;
    ZeroMemory(&packet, sizeof(packet));
    packet.code = RC_GET_PROCESS;
    packet.numProcesses = (WORD)count;
    packet.index = (WORD)index;
    packet.pid = pe32.th32ProcessID;
    strncpy(packet.name, pe32.szExeFile, sizeof(packet.name) - 1);
    packet.name[31] = '\0';

    ListProcessCtx lctx = {&packet};
    with_process(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                 pe32.th32ProcessID, list_process_action, &lctx);

    sendto(sock, (const char *)&packet, sizeof(packet), 0,
           (struct sockaddr *)&clientAddr, clientAddrLen);
    index++;
  } while (Process32Next(hSnap, &pe32) && index < count);

  CloseHandle(hSnap);
}

static char *extract_string_from_payload(const void *payload, int nameLen) {
  char *name = (char *)malloc(nameLen + 1);
  if (!name)
    return NULL;
  memcpy(name, payload, nameLen);
  name[nameLen] = '\0';
  return name;
}

void handleExec(const char *payload, int len) {
  if (len < (int)sizeof(ExecMsg))
    return;
  const ExecMsg *msg = (const ExecMsg *)payload;
  int filenameLen = msg->filenameLen;
  int paramsLen = msg->paramsLen;
  if (filenameLen < 0 || paramsLen < 0)
    return;
  if (len < (int)sizeof(ExecMsg) + filenameLen + paramsLen)
    return;

  char *filename = extract_string_from_payload(payload + sizeof(ExecMsg), filenameLen);
  char *params = extract_string_from_payload(payload + sizeof(ExecMsg) + filenameLen, paramsLen);
  if (!filename || !params) {
    free(filename);
    free(params);
    return;
  }

  ShellExecuteA(NULL, "open", filename, params, NULL, SW_SHOW);

  free(filename);
  free(params);
}

static BOOL kill_process_cb(void *ctx, DWORD pid, const char *name) {
  with_process(PROCESS_TERMINATE, pid, kill_action, NULL);
  return FALSE;
}

void handleKillProcess(const char *payload, int len) {
  if (len < (int)sizeof(KillProcessMsg))
    return;
  const KillProcessMsg *msg = (const KillProcessMsg *)payload;
  int nameLen = msg->nameLen;
  if (nameLen < 0 || len < (int)sizeof(KillProcessMsg) + nameLen)
    return;

  char *name = extract_string_from_payload(payload + sizeof(KillProcessMsg), nameLen);
  if (!name)
    return;

  enumerate_processes_by_name(name, kill_process_cb, NULL);

  free(name);
}

static BOOL set_affinity_cb(void *ctx, DWORD pid, const char *name) {
  with_process(PROCESS_SET_INFORMATION, pid, set_affinity_action, ctx);
  return FALSE;
}

void handleSetAffinity(const char *buffer, int len) {
  if (len < (int)sizeof(SetAffinityMsg))
    return;
  const SetAffinityMsg *msg = (const SetAffinityMsg *)buffer;
  int pid = msg->pid;
  int mask = msg->mask;

  if (pid != 0) {
    AffinityCtx actx = {mask};
    with_process(PROCESS_SET_INFORMATION, pid, set_affinity_action, &actx);
  } else {
    int nameLen = msg->nameLen;
    if (len < (int)sizeof(SetAffinityMsg) + nameLen)
      return;

    char *name = extract_string_from_payload(buffer + sizeof(SetAffinityMsg), nameLen);
    if (!name)
      return;

    AffinityCtx ctx;
    ctx.mask = mask;
    enumerate_processes_by_name(name, set_affinity_cb, &ctx);

    free(name);
  }
}

struct FindWindowData {
  DWORD pid;
  HWND hWnd;
};

BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
  struct FindWindowData *data = (struct FindWindowData *)lParam;
  DWORD currPid = 0;
  GetWindowThreadProcessId(hWnd, &currPid);
  if (currPid == data->pid && IsWindowVisible(hWnd) &&
      GetWindow(hWnd, GW_OWNER) == NULL) {
    data->hWnd = hWnd;
    return FALSE;
  }
  return TRUE;
}

typedef struct {
  DWORD pid;
} BringToFrontCtx;

static BOOL bring_to_front_find_cb(void *ctx, DWORD pid, const char *name) {
  BringToFrontCtx *btfctx = (BringToFrontCtx *)ctx;
  btfctx->pid = pid;
  return TRUE;
}

void handleBringToFront(const char *buffer, int len) {
  if (len < (int)sizeof(BringToFrontMsg))
    return;
  const BringToFrontMsg *msg = (const BringToFrontMsg *)buffer;
  int nameLen = msg->nameLen;
  if (nameLen < 0 || len < (int)sizeof(BringToFrontMsg) + nameLen)
    return;

  char *name = extract_string_from_payload(buffer + sizeof(BringToFrontMsg), nameLen);
  if (!name)
    return;

  BringToFrontCtx ctx;
  ctx.pid = 0;
  enumerate_processes_by_name(name, bring_to_front_find_cb, &ctx);

  if (ctx.pid != 0) {
    struct FindWindowData winData = {ctx.pid, NULL};
    EnumWindows(EnumWindowsProc, (LPARAM)&winData);
    if (winData.hWnd) {
      if (IsIconic(winData.hWnd))
        ShowWindow(winData.hWnd, SW_RESTORE);
      else
        ShowWindow(winData.hWnd, SW_SHOW);
      SetForegroundWindow(winData.hWnd);
    }
  }

  free(name);
}

void handleInput(const char *buffer, int len) {
  BYTE code = buffer[0];
  if (code == RC_MOUSE_EVENT) {
    if (len < (int)sizeof(MouseEventMsg))
      return;
    const MouseEventMsg *msg = (const MouseEventMsg *)buffer;
    mouse_event(msg->flags, msg->dx, msg->dy, msg->wheel, 0);
    if (msg->feedback)
      sendCursorFeedback();
  } else if (code == RC_KEYBOARD_EVENT) {
    if (len < (int)sizeof(KeyboardEventMsg))
      return;
    const KeyboardEventMsg *msg = (const KeyboardEventMsg *)buffer;
    keybd_event(msg->vkey, 0, msg->flags, 0);
  }
}

static SOCKET setup_server_socket(void) {
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    return INVALID_SOCKET;

  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) {
    WSACleanup();
    return INVALID_SOCKET;
  }

  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(SERVER_PORT);
  serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

  if (bind(s, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) ==
      SOCKET_ERROR) {
    closesocket(s);
    WSACleanup();
    return INVALID_SOCKET;
  }

  clientAddr.sin_family = AF_INET;
  clientAddr.sin_port = htons(CLIENT_PORT);
  clientAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

  char initBuffer[BUFFER_SIZE];
  initBuffer[0] = RC_INIT;
  sendto(s, initBuffer, 1, 0, (struct sockaddr *)&clientAddr, clientAddrLen);

  return s;
}

static void server_main_loop(SOCKET s) {
  char buffer[BUFFER_SIZE];
  while (running) {
    struct sockaddr_in sender;
    int senderLen = sizeof(sender);
    int len = recvfrom(s, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&sender,
                       &senderLen);

    if (len > 0) {
      BYTE command = buffer[0];

      switch (command) {
      case RC_EXIT:
        ExitProcess(0);
        break;
      case RC_INIT:
        break;
      case RC_EXEC:
        handleExec(buffer + sizeof(ExecHeader), len - sizeof(ExecHeader));
        break;
      case RC_KILL_PROCESS:
        handleKillProcess(buffer + 1, len - 1);
        break;
      case RC_LIST_PROCESSES:
        handleListProcesses();
        break;
      case RC_SET_PROCESS_AFFINITY:
        handleSetAffinity(buffer, len);
        break;
      case RC_MOUSE_EVENT:
      case RC_KEYBOARD_EVENT:
        handleInput(buffer, len);
        break;
      case RC_BRING_TO_FRONT:
        handleBringToFront(buffer, len);
        break;
      }
    }
  }
}

DWORD WINAPI ServerThread(LPVOID lpParam) {
  SOCKET s = setup_server_socket();
  if (s == INVALID_SOCKET)
    return 0;

  sock = s;
  server_main_loop(s);

  closesocket(s);
  WSACleanup();
  return 0;
}

void handleChildProcesses(int affinityMask) {
  if (affinityMask <= 0)
    return;

  DWORD myPid = GetCurrentProcessId();

  while (running) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
      Sleep(POLL_INTERVAL_MS);
      continue;
    }
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    if (Process32First(hSnap, &pe32)) {
      do {
        if (pe32.th32ProcessID <= myPid)
          continue;
        if (isPidSeen(pe32.th32ProcessID))
          continue;
        AffinityCtx actx = {affinityMask};
        with_process(PROCESS_SET_INFORMATION, pe32.th32ProcessID,
                     set_affinity_action, &actx);
        addSeenPid(pe32.th32ProcessID);
      } while (Process32Next(hSnap, &pe32));
    }
    CloseHandle(hSnap);
    Sleep(POLL_INTERVAL_MS);
  }
}

static void parse_args(int *affinity, char **directory, char **executable,
                       char *execArgs, size_t execArgsSize) {
  *affinity = 0;
  *directory = NULL;
  *executable = "wfm.exe";

  int argc = __argc;
  char **argv = __argv;

  int argIdx = 1;
  while (argIdx < argc) {
    if (strcmp(argv[argIdx], "/affinity") == 0) {
      if (argIdx + 1 < argc) {
        *affinity = (int)strtol(argv[argIdx + 1], NULL, 16);
        argIdx += 2;
      } else {
        argIdx++;
      }
    } else if (strcmp(argv[argIdx], "/dir") == 0) {
      if (argIdx + 1 < argc) {
        *directory = argv[argIdx + 1];
        argIdx += 2;
      } else {
        argIdx++;
      }
    } else {
      *executable = argv[argIdx];
      argIdx++;
      break;
    }
  }

  execArgs[0] = '\0';
  if (argIdx < argc) {
    for (int i = argIdx; i < argc; i++) {
      size_t remaining = execArgsSize - strlen(execArgs) - 1;
      strncat(execArgs, "\"", remaining);
      remaining = execArgsSize - strlen(execArgs) - 1;
      strncat(execArgs, argv[i], remaining);
      remaining = execArgsSize - strlen(execArgs) - 1;
      strncat(execArgs, "\" ", remaining);
    }
  }
}

static void launch_process(const char *executable, const char *execArgs,
                            const char *directory) {
  SHELLEXECUTEINFOA sei = {0};
  sei.cbSize = sizeof(SHELLEXECUTEINFOA);
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.lpFile = executable;
  sei.lpParameters = execArgs[0] ? execArgs : NULL;
  sei.lpDirectory = directory;
  sei.nShow = SW_SHOW;

  ShellExecuteExA(&sei);
  if (sei.hProcess)
    CloseHandle(sei.hProcess);
}

static void wait_for_server(HANDLE hThread) {
  WaitForSingleObject(hThread, INFINITE);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
  int affinity;
  char *directory;
  char *executable;
  char execArgs[MAX_CMDLINE] = {0};

  parse_args(&affinity, &directory, &executable, execArgs, sizeof(execArgs));
  launch_process(executable, execArgs, directory);

  HANDLE hThread = CreateThread(NULL, 0, ServerThread, NULL, 0, NULL);

  if (affinity > 0) {
    handleChildProcesses(affinity);
  } else {
    wait_for_server(hThread);
  }

  freeSeenPids();
  return 0;
}
