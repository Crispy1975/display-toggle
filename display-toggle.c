/*
 * display-toggle — toggle any macOS display on or off by UUID
 * Copyright (C) 2025 Crispy1975
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * ---
 *
 * macOS removes a disabled display's UUID from the online display list,
 * making it impossible to re-enable via UUID lookup alone. This tool saves
 * a UUID → CGDirectDisplayID mapping to disk while the display is active,
 * then reuses the saved ID to re-enable it after it has been disabled.
 *
 * The private SkyLight function CGSConfigureDisplayEnabled is used because
 * CGConfigureDisplayEnabled is not exposed in the public CoreGraphics headers.
 *
 * Build: make
 * Install: make install   (copies to /usr/local/bin)
 */

#include <ColorSync/ColorSyncDevice.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CGDisplayConfiguration.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Private API loaded from SkyLight at runtime */
typedef CGError (*CGSConfigureDisplayEnabledFn)(CGDisplayConfigRef, CGDirectDisplayID, bool);
static CGSConfigureDisplayEnabledFn g_set_enabled = NULL;

/* ── UUID helpers ─────────────────────────────────────────────────────────── */

static void uuid_for_display(CGDirectDisplayID did, char out[40]) {
    out[0] = '\0';
    CFUUIDRef ref = CGDisplayCreateUUIDFromDisplayID(did);
    if (!ref) return;
    CFStringRef s = CFUUIDCreateString(NULL, ref);
    CFStringGetCString(s, out, 40, kCFStringEncodingUTF8);
    CFRelease(s);
    CFRelease(ref);
}

/* ── State file ───────────────────────────────────────────────────────────── */
/*
 * Stores UUID → CGDirectDisplayID pairs so disabled displays (which vanish
 * from the online list) can still be referenced for re-enabling.
 * Location: ~/.config/display-toggle/state
 * Format: one "<UUID> <id>" pair per line.
 */

#define MAX_DISPLAYS 32
#define MAX_ENTRIES  32

typedef struct { char uuid[40]; CGDirectDisplayID did; } Entry;
static Entry g_entries[MAX_ENTRIES];
static int   g_entry_count = 0;

static void state_file_path(char *buf, size_t n) {
    const char *home = getenv("HOME");
    snprintf(buf, n, "%s/.config/display-toggle/state", home ? home : "/tmp");
}

static void load_state(void) {
    char path[512];
    state_file_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[128];
    while (g_entry_count < MAX_ENTRIES && fgets(line, sizeof(line), f)) {
        unsigned int did;
        if (sscanf(line, "%39s %u", g_entries[g_entry_count].uuid, &did) == 2) {
            g_entries[g_entry_count].did = (CGDirectDisplayID)did;
            g_entry_count++;
        }
    }
    fclose(f);
}

static void save_state(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    char config_dir[512], state_dir[512], path[512];
    snprintf(config_dir, sizeof(config_dir), "%s/.config", home);
    snprintf(state_dir,  sizeof(state_dir),  "%s/.config/display-toggle", home);
    snprintf(path,       sizeof(path),       "%s/.config/display-toggle/state", home);
    mkdir(config_dir, 0755);
    mkdir(state_dir,  0755);
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < g_entry_count; i++)
        fprintf(f, "%s %u\n", g_entries[i].uuid, g_entries[i].did);
    fclose(f);
}

static void upsert(const char *uuid, CGDirectDisplayID did) {
    for (int i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].uuid, uuid) == 0) { g_entries[i].did = did; return; }
    }
    if (g_entry_count < MAX_ENTRIES) {
        strncpy(g_entries[g_entry_count].uuid, uuid, 39);
        g_entries[g_entry_count].uuid[39] = '\0';
        g_entries[g_entry_count].did = did;
        g_entry_count++;
    }
}

static CGDirectDisplayID lookup(const char *uuid) {
    for (int i = 0; i < g_entry_count; i++)
        if (strcmp(g_entries[i].uuid, uuid) == 0) return g_entries[i].did;
    return 0;
}

/* ── Commands ─────────────────────────────────────────────────────────────── */

static void cmd_list(void) {
    uint32_t count = 0;
    CGGetOnlineDisplayList(0, NULL, &count);
    CGDirectDisplayID displays[MAX_DISPLAYS];
    if (count > MAX_DISPLAYS) count = MAX_DISPLAYS;
    CGGetOnlineDisplayList(count, displays, &count);

    printf("%-36s  %-8s  %-8s  Resolution\n", "UUID", "Active", "Built-in");
    printf("%-36s  %-8s  %-8s  ----------\n", "----", "------", "--------");

    for (uint32_t i = 0; i < count; i++) {
        CGDirectDisplayID did = displays[i];
        char uuid[40];
        uuid_for_display(did, uuid);
        bool active  = CGDisplayIsActive(did)  != 0;
        bool builtin = CGDisplayIsBuiltin(did) != 0;
        uint32_t w   = CGDisplayPixelsWide(did);
        uint32_t h   = CGDisplayPixelsHigh(did);
        printf("%-36s  %-8s  %-8s  %ux%u\n",
               uuid, active ? "yes" : "no", builtin ? "yes" : "no", w, h);

        /* Seed state while display is visible */
        if (uuid[0]) { upsert(uuid, did); }
    }
    save_state();
}

typedef enum { ACTION_TOGGLE = -1, ACTION_OFF = 0, ACTION_ON = 1 } Action;

static int cmd_toggle(const char *uuid, Action action) {
    uint32_t count = 0;
    CGGetOnlineDisplayList(0, NULL, &count);
    CGDirectDisplayID displays[MAX_DISPLAYS];
    if (count > MAX_DISPLAYS) count = MAX_DISPLAYS;
    CGGetOnlineDisplayList(count, displays, &count);

    /* Search online list first */
    for (uint32_t i = 0; i < count; i++) {
        CGDirectDisplayID did = displays[i];
        char u[40];
        uuid_for_display(did, u);
        if (strcmp(u, uuid) != 0) continue;

        upsert(uuid, did);
        save_state();

        bool active = CGDisplayIsActive(did) != 0;
        bool enable = (action == ACTION_TOGGLE) ? !active : (action == ACTION_ON);

        if (enable == active) {
            const char *state = active ? "on" : "off";
            printf("%s (already %s)\n", state, state);
            return 0;
        }

        CGDisplayConfigRef config;
        CGBeginDisplayConfiguration(&config);
        g_set_enabled(config, did, enable);
        CGError err = CGCompleteDisplayConfiguration(config, kCGConfigurePermanently);
        if (err != kCGErrorSuccess) {
            fprintf(stderr, "error: CGCompleteDisplayConfiguration returned %d\n", (int)err);
            return 1;
        }
        printf("%s\n", enable ? "on" : "off");
        return 0;
    }

    /* Display not in online list */
    if (action == ACTION_OFF) {
        printf("off (already off)\n");
        return 0;
    }

    CGDirectDisplayID saved = lookup(uuid);
    if (saved == 0) {
        fprintf(stderr,
            "error: display %s has never been seen while active.\n"
            "       Connect the display and run `display-toggle list` to register it.\n",
            uuid);
        return 1;
    }

    CGDisplayConfigRef config;
    CGBeginDisplayConfiguration(&config);
    g_set_enabled(config, saved, true);
    CGError err = CGCompleteDisplayConfiguration(config, kCGConfigurePermanently);
    if (err != kCGErrorSuccess) {
        fprintf(stderr, "error: CGCompleteDisplayConfiguration returned %d\n", (int)err);
        return 1;
    }
    printf("on\n");
    return 0;
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s list              List online displays with UUIDs and state\n"
        "  %s <UUID>            Toggle display on/off\n"
        "  %s <UUID> on         Turn display on\n"
        "  %s <UUID> off        Turn display off\n"
        "\n"
        "UUIDs are stable across reboots. Run `list` to find yours.\n"
        "State is persisted in ~/.config/display-toggle/state so a disabled\n"
        "display (which vanishes from the online list) can still be re-enabled.\n",
        prog, prog, prog, prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }

    void *sl = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY);
    g_set_enabled = sl ? (CGSConfigureDisplayEnabledFn)dlsym(sl, "CGSConfigureDisplayEnabled") : NULL;
    if (!g_set_enabled) {
        fprintf(stderr, "error: CGSConfigureDisplayEnabled not found in SkyLight\n");
        return 1;
    }

    load_state();

    if (strcmp(argv[1], "list") == 0) {
        cmd_list();
        return 0;
    }

    const char *uuid = argv[1];
    Action action = ACTION_TOGGLE;
    if (argc >= 3) {
        if      (strcmp(argv[2], "on")  == 0) action = ACTION_ON;
        else if (strcmp(argv[2], "off") == 0) action = ACTION_OFF;
        else { usage(argv[0]); return 1; }
    }

    return cmd_toggle(uuid, action);
}
