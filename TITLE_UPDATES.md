# Title updates

Xbox 360 title updates are optional. Recompiled executables are version-specific,
so each PC build targets either its supported disc or one exact title update.

## Disc builds

Disc builds need no update. Omit `patched_file_path` from every codegen module and
leave `GameDescriptor::title_update` empty. Runtime XEX patching is disabled, and
startup rejects stray `.xexp` files instead of combining updated data with old
recompiled code.

## Update builds

Keep update packages outside the repository. Extract the target package into a
private staging tree and place a copy of each base XEX next to its matching XEXP:

```text
staging/
  default.xex
  default.xexp
  EAWebKit.xex
  EAWebKit.xexp
```

Point each patched codegen module at its staged base executable:

```toml
[entrypoint]
file_path = "assets/default.xex"
patched_file_path = "private/tu/staging/default.xex"
out_directory_path = "generated/default"

[[modules]]
guest_path = "EAWebKit.xex"
file_path = "assets/EAWebKit.xex"
patched_file_path = "private/tu/staging/EAWebKit.xex"
out_directory_path = "generated/EAWebKit"
```

Codegen applies each sibling `.xexp` with the same loader used at runtime. If
one targeted XEX is patched, all targeted XEX modules must use this mode.

Pin runtime installation to the same package in the game's descriptor:

```cpp
descriptor.title_update = recomp::TitleUpdateDescriptor{
    .label = "Title Update 3",
    .title_id = 0x12345678,
    .media_id = 0x9abcdef0,
    .version = 0x00000003,
    .code_patches = {
        {.path = "default.xexp", .size = 123456, .content_hash = "<xxh3-128>"},
    },
};
```

The launcher accepts the player's STFS title update package, checks its title,
media and version metadata, extracts it to the user-data folder, and verifies
every declared code patch. Its files overlay the disc tree; any file not present
in the update still comes from the disc.

For unattended testing, set `RECOMP_INSTALL_TU` to the package path. The package
is consumed locally and must not be included in source or release archives.
