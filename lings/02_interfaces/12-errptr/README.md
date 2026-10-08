# 12 - Return an error through a pointer

## One return value, two kinds of result

Some kernel functions need to return either a valid pointer or a specific error.
`NULL` can mean failure, but it cannot say which failure occurred.

An error pointer encodes a negative errno in a pointer-shaped value:

```text
try_alloc(false) -----------------> valid allocation

try_alloc(true) --> ERR_PTR(-ENOMEM)
                          |
                          +--> IS_ERR(ptr) == true
                          +--> PTR_ERR(ptr) == -ENOMEM
```

The pointer is an encoded result, not an address that may be dereferenced.

## The three macros

```c
ERR_PTR(-ENOMEM)  /* encode a negative errno as a pointer */
IS_ERR(ptr)       /* test whether a pointer contains an errno */
PTR_ERR(ptr)      /* decode that errno */
```

`IS_ERR(NULL)` is false.
APIs that may return both `NULL` and error pointers require both checks:

```c
ptr = get_object();
if (IS_ERR(ptr))
	return PTR_ERR(ptr);
if (!ptr)
	return -ENOMEM;
```

Always follow the documented return convention of the function being called.
For example, `kmalloc()` returns `NULL`, while `filp_open()` returns an error pointer.

## What to change

The scaffold calls `try_alloc(true)` and publishes the decoded result through `/proc/rk_errptr`.
Its simulated failure currently returns `NULL`, so no errno can be decoded.

Change that one return value to:

```c
return ERR_PTR(-ENOMEM);
```

Leave the successful `kmalloc()` path unchanged.
The supplied caller already demonstrates the correct `IS_ERR`, `PTR_ERR`, and `NULL` order.

## Observe it

```bash
./rootkitlings start L12
$EDITOR .rootkitlings/work/error-pointer/hello.c
./rootkitlings check L12
```

The running module must report:

```text
error=-12
allocation=ok
```

`-12` is `-ENOMEM` on Linux.
The check reads these values from the live proc entry and verifies that unloading removes it.

## Limits and detection

Error pointers are an internal calling convention.
They do not hide the proc entry or the loaded module.
Dereferencing one instead of checking it can crash the kernel.

Next: [13 - Lists](../13-lists/README.md).
