# 08 - Roll back partial initialization

Module exit runs only after init succeeds.

If init acquires one resource and a later step fails, init must release the earlier resource before returning the error.

This module allocates two objects and provides `fail_second=1` to force failure between them.

## Observe both paths

```sh
insmod hello.ko fail_second=1
echo $?
lsmod | grep hello
```

The load fails and `hello` is absent.

The normal path acquires both objects and releases them on unload:

```sh
insmod hello.ko
rmmod hello
dmesg | tail
```

## Reverse-order cleanup

```text
allocate primary
       |
       v
allocate secondary ---- failure ----> free primary -> return error
       |
     success
       |
       v
module exit: free secondary -> free primary
```

A cleanup label represents one ownership boundary.

Every failure after `primary` is acquired jumps to the label that frees it.

The success path returns before that label and transfers both objects to module exit.

Resources are released in reverse acquisition order because later resources may depend on earlier ones.

## Implement it

Keep the primary allocation and add a second pointer plus a read-only boolean parameter named `fail_second`.

After allocating `primary`, set an error and jump to `free_primary` when the parameter is true.

Use the same label when allocation of `secondary` returns `NULL`.

At `free_primary`, free the primary object, clear its pointer, and return the saved error.

On normal exit, free `secondary` before `primary` and clear both pointers.

## Verify

```sh
./rootkitlings check L08
```

The guest exercises the forced failure and successful load paths and confirms that neither leaves module state behind.

The host verification confirms that the failure path jumps to cleanup, frees `primary`, and returns the saved error.

The source-level check proves the requested ownership shape; runtime leak detection requires dedicated allocator instrumentation.

Next: [09 - Publish a procfs entry](../../01_procfs/09-proc-entry/README.md).
