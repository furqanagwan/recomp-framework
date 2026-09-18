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

## Asking the player

A build with a `title_update` descriptor opens `UpdateRequiredDialog`, which
follows the console's own Update Required screen: the question on the left with
its rows, what is being updated on the right, and A and B underneath. An update
was the player's choice on the console and stays one here, so the rows are
Download Now, Install From This PC, and carrying on without it. B does what the
last row does.

The console's line about being signed out of Xbox Live is left off; there is no
Xbox Live to sign out of, and nothing here plays online either way.

`can_play_without_update` says whether carrying on is possible. It is only true
for a release that also ships the disc-compiled executable, because an update
build's recompiled code *is* the update's: within that one binary there is no
disc behaviour to fall back to. When it is false the last row quits instead, so
the screen never offers something the build cannot do.

## Downloading

Xbox Live no longer serves these packages. Download Now asks
[XboxUnity](https://xboxunity.net/) instead, which keeps the same files:

| | |
| --- | --- |
| List | `GET http://xboxunity.net/Resources/Lib/TitleUpdateInfo.php?titleid=<8 hex digits>` |
| Download | `GET http://xboxunity.net/Resources/Lib/TitleUpdate.php?tuid=<TitleUpdateID>` |

The listing groups updates by the media ID of the disc each one patches, so
`XboxUnityCatalog::Choose` takes the one matching this build's media ID *and*
the executable version it was recompiled from, and the highest version where
the archive holds several. A game's two regional discs usually have an update
each, and they are not interchangeable.

The site is plain HTTP and has no certificate to offer. That is not what makes a
package trustworthy: whatever arrives is checked against the descriptor's own
sizes and XXH3-128 digests before it is installed, exactly as a package the
player picked by hand is.

Nothing depends on the archive. When it cannot be reached, has no update for
this disc, or returns a package that fails its digest check, the screen says so
and the other rows still work. A host with no way to make requests at all
(`HttpDownload::IsAvailable()` false) simply does not show the row.
