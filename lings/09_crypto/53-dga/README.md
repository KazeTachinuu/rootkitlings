# 53 - Derive a daily domain

## From a fixed address to a DGA

When an implant calls back to its command-and-control server (C2), it needs an address to connect to.
The simplest approach is to store the server's IP in the implant:

```text
implant --------------------> 203.0.113.10
             fixed IP
```

That IP can be discovered and blocked.
It may also change when the server is moved or replaced.

A domain lets the operator change the IP in DNS without changing every deployed implant:

```text
implant ---> control.example ---> 203.0.113.10
                       change DNS ---> 203.0.113.42
```

The domain is still fixed.
Once discovered, it can also be blocked, monitored, or redirected.

A **domain generation algorithm** (DGA) calculates the domain instead of storing it.
The implant and controller use the same secret key, time period, and algorithm, so they independently produce the same name:

```text
             shared key + day 20000
                         |
              +----------+----------+
              |                     |
           implant               controller
              |                     |
              +------ same DGA -----+
                         |
                         v
             6f158b1adc32cad8.example
```

No new domain needs to be sent to the deployed implant.
When the day changes, both endpoints derive the next name.
This avoids one permanent domain; it does not make the resulting DNS traffic invisible.

## The result

The time period is represented by a number called a `bucket`.
A daily bucket can be calculated as:

```text
seconds since 1970 / 86400 = day bucket
```

The module accepts it as a parameter so the result is repeatable:

```text
bucket=20000                          bucket=20001
domain=6f158b1adc32cad8.example       domain=0e4997bd6ccb967b.example
```

Loading bucket `20000` again produces the first domain again.
Advancing the bucket changes it.
`.example` is reserved for documentation and cannot resolve to a public domain.

## How

```text
campaign key                 bucket
     |                          |
     +------------+-------------+
                  |
                  v
       siphash_1u64(bucket, &key)
                  |
                  v
           64-bit value
                  |
                  v
       16 hex digits + ".example"
```

SipHash is a fast keyed hash in the kernel.
The key changes the mapping; the bucket selects today's value.
A hash is not encryption: the result is not decoded later.

The existing code provides:

- `siphash_key_t campaign_key` — the shared 128-bit key;
- `module_param()` — accepts `bucket=...`
  when the module loads;
- `/proc/rk_dga` — displays the selected bucket and domain;
- module initialization and cleanup.

`derive_domain()` currently formats the bucket itself.
That produces a valid name, but it is predictable without the key:

```c
scnprintf(domain, size, "%016llx.example",
          (unsigned long long)bucket_value);
```

Change the function so it:

1. derives a `u64` with `siphash_1u64(bucket_value, &campaign_key)`;
2. formats that derived value into `domain` with `scnprintf()`.

`%016llx` renders the value as exactly 16 lowercase hexadecimal characters.
The cast to `unsigned long long` matches `%llx` on every supported architecture.

## Try it

```sh
./rootkitlings start L53
$EDITOR .rootkitlings/work/keyed-domain-generation/hello.c
./rootkitlings check L53
```

The check reads the real `/proc/rk_dga` output and verifies:

- same bucket, same domain;
- new bucket, new domain;
- valid `.example` name;
- clean module unload.

## Limits and detection

This module derives a name.
It does not perform DNS lookup or network I/O.

Repeated `NXDOMAIN` responses (name not found) can reveal a DGA.
A defender may also register a predicted domain and observe connections to it: a **sinkhole**.
