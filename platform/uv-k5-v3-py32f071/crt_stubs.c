/* CRT shim for the PY32F071 bare-metal build.
 *
 * The vendor Reset_Handler in startup_py32f071.s (not ours to edit)
 * unconditionally calls __libc_init_array right before main(). We define it
 * here as a real object-file symbol so it resolves before (and overrides) any
 * copy newlib-nano might provide via --specs=nano.specs — keeping init
 * behaviour under our control regardless of the C library flags. It walks the
 * linker-provided .init_array section (see firmware_py32f071.ld) so any
 * __attribute__((constructor)) functions still run correctly.
 */
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

void __libc_init_array(void)
{
    for (void (**fn)(void) = __init_array_start; fn < __init_array_end; fn++)
        (*fn)();
}
