# Spawn a root shell from the kernel

The kernel can launch userspace processes using `call_usermodehelper`. This is
the same mechanism the kernel uses to run hotplug scripts and module loaders.
A rootkit uses it to execute commands as root without any login.

## Mechanism

```
call_usermodehelper_setup(path, argv, envp, GFP_KERNEL, init, cleanup, data)
  |
  v
init callback runs in the new process context
  (set credentials to root here)
  |
  v
call_usermodehelper_exec(info, UMH_WAIT_EXEC)
  |
  v
/bin/sh -c "id > /tmp/rklings_shell_test"
  runs as uid=0
```

The `init` callback is called in the context of the newly spawned process,
before it calls exec. This is where you set credentials to root. The same
callback pattern applies to redirecting file descriptors.

## Your task

Edit `main.c`. Complete:

1. The `rk_umh_init` callback: call `commit_creds(prepare_kernel_cred(NULL))`
   to set the process credentials to root.
2. `rk_init`: set up argv and envp, call `call_usermodehelper_setup` with
   the init callback, then `call_usermodehelper_exec` with `UMH_WAIT_EXEC`.

The spawned command writes its `id` output to `/tmp/rklings_shell_test`.

## Demo

```
vm$ insmod rklings.ko
vm$ cat /tmp/rklings_shell_test
uid=0(root) gid=0(root) groups=0(root)
```

## Verification

```
./rklings check
```
