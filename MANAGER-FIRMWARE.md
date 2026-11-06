# Indexed F0 Manager firmware

Imported from the preserved Manager 0.5.3 source, based on the minimal selectable-bitrate firmware. Extended configuration addresses a module by hardware ID and current data index. Legacy configuration remains available in firmware. Use the indexed Manager for modules sharing a hardware ID.

Built release and application SHA256: `115fdf2327c34ee9c6c4a42efd5959679e707484614573c498f71ea976b8750c`. Bootloader matches the selectable firmware. CAN updates must connect exactly one module. No physical hardware validation has been performed.

Dependencies are the existing pinned ChibiOS and libfirmware submodules. Build from `firmware/boards/f0_module` with ARM GNU 14.3.Rel1 and the project's crc32 tool. See the Manager repository for configuration tools and regression tests.
