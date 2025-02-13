//
//  tmpfs_overlay.m
//  objsee
//
//  Created by Ethan Arbuckle on 1/31/25.
//

#include <Foundation/Foundation.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/stat.h>

#if TARGET_OS_MAC && !TARGET_OS_IPHONE

#include <libproc.h>
#include <dlfcn.h>

static int _system(const char *cmd) {
    static dispatch_once_t onceToken;
    static void *system_func = NULL;
    dispatch_once(&onceToken, ^{
        system_func = dlsym(RTLD_DEFAULT, "system");
    });
    
    if (system_func == NULL) {
        return -1;
    }
    
    return ((int (*)(const char *))system_func)(cmd);
}

static bool is_tmpfs_mount(const char *path) {
    struct statfs fs;
    if (statfs(path, &fs) < 0) {
        return false;
    }
    
    return strstr(fs.f_fstypename, "tmpfs") != NULL;
}

pid_t find_pid_of_running_sim_launchd(void) {
    struct kinfo_proc *procs = NULL;
    size_t procs_size = 0;
    int num_procs = 0;
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
    
    if (sysctl(mib, 4, NULL, &procs_size, NULL, 0) < 0) {
        return -1;
    }
    
    procs = malloc(procs_size);
    if (sysctl(mib, 4, procs, &procs_size, NULL, 0) < 0) {
        free(procs);
        return -1;
    }
    
    num_procs = (int)procs_size / sizeof(struct kinfo_proc);
    for (int i = 0; i < num_procs; i++) {
        struct kinfo_proc *proc = &procs[i];
        if (strstr(proc->kp_proc.p_comm, "launchd_sim") != NULL) {
            printf("Found launchd_sim with pid %d\n", proc->kp_proc.p_pid);
            free(procs);
            return proc->kp_proc.p_pid;
        }
    }
    
    free(procs);
    return 0;
}

static int create_overlay_on_path(const char *path, bool copy_contents) {
    if (is_tmpfs_mount(path)) {
        printf("Path is already a tmpfs mount: %s\n", path);
        return 0;
    }
    
    const char *backup_path = NULL;
    if (copy_contents) {
        char template[PATH_MAX];
        snprintf(template, sizeof(template), "/tmp/ios_backup.XXXXXX");
        backup_path = mkdtemp(template);
        if (backup_path == NULL) {
            printf("Failed to create temporary directory\n");
            return -1;
        }

        char cp_cmd[256];
        snprintf(cp_cmd, sizeof(cp_cmd), "cp -R '%s'/* '%s'", path, backup_path);
        if (_system(cp_cmd) != 0) {
            printf("Failed to copy contents from %s to %s\n", path, backup_path);
            return -1;
        }
    }
    
    char mount_cmd[256];
    snprintf(mount_cmd, sizeof(mount_cmd), "mount_tmpfs '%s'", path);
    if (_system(mount_cmd) != 0) {
        printf("Failed to mount tmpfs on %s\n", path);
        return -1;
    }
    
    if (backup_path) {
        char cp_cmd[256];
        snprintf(cp_cmd, sizeof(cp_cmd), "cp -R '%s'/* '%s'", backup_path, path);
        if (_system(cp_cmd) != 0) {
            printf("Failed to restore contents from %s to %s\n", backup_path, path);
            return -1;
        }
        
        NSFileManager *fm = [NSFileManager defaultManager];
        if (![fm removeItemAtPath:[NSString stringWithUTF8String:backup_path] error:nil]) {
            printf("Failed to remove backup directory\n");
            return -1;
        }
    }
    
    return 0;
}

int make_running_simulator_runtime_readwrite(void) {

    pid_t sim_launchd_pid = find_pid_of_running_sim_launchd();
    if (sim_launchd_pid == -1) {
        printf("Failed to find running sim launchd\n");
        return -1;
    }
    
    char sim_runtime_path[PATH_MAX];
    proc_pidpath(sim_launchd_pid, sim_runtime_path, sizeof(sim_runtime_path));
    NSString *simRuntimePathStr = [NSString stringWithUTF8String:sim_runtime_path];
    NSString *simruntimeRoot = [[simRuntimePathStr stringByDeletingLastPathComponent] stringByDeletingLastPathComponent];
    
    NSString *usrLibPath = [simruntimeRoot stringByAppendingPathComponent:@"usr/lib"];
    NSString *dyldSimPath = [usrLibPath stringByAppendingPathComponent:@"dyld_sim"];
    if (![[NSFileManager defaultManager] fileExistsAtPath:dyldSimPath]) {
        printf("Failed to find dyld_sim at %s\n", dyldSimPath.UTF8String);
        return -1;
    }
    
    if (create_overlay_on_path(usrLibPath.UTF8String, YES) != 0) {
        printf("Failed to create overlay on %s\n", usrLibPath.UTF8String);
        return -1;
    }

    return 0;
}

#else

int create_overlay_on_path(const char *path, bool copy_contents) {
    printf("Not implemented on non-simulator platform\n");
    return -1;
}

#endif
