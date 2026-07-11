# GBFR_modtools

Drag-and-drop texture extraction and packing tools for Granblue Fantasy Relink modding.

## Setup

Place the following release binaries in the root of this folder (next to the `.bat` files):

| Binary | Download |
|--------|----------|
| `GraniteTextureReader.exe` | https://github.com/Nenkai/GraniteTextureReader/releases |
| `nier_cli_mgrr_<ver>/` (folder) | https://github.com/ArthurHeitmann/nier_cli/releases/tag/v1.3.0_mgrr |

Both are gitignored — replace with a newer release at any time, scripts use relative paths.

## Workflow

### Step 1 — minfo → hash table

Drag a `.minfo` file onto `01_minfo转Hash表.bat`.

The `.minfo` must sit next to its `vars/` folder (standard GBFRDataTools unpack layout):

```
pl1400/
  pl1400.minfo   <- drag this
  vars/
    0.mmat
    1.mmat
    ...
```

Output: `output_hashes/<charname>_hashes.txt`

### Step 2 — hash table → extract textures

Drag a `.gts` file onto `02_Hash表提取纹理.bat`.

Common `.gts` locations inside the game data:
```
data/granite/2k/gts/0/0.gts
data/granite/2k/gts/1/1.gts   <- characters / faces
data/granite/2k/gts/2/2.gts   <- eyes
data/granite/4k/gts/...
```

If a hash is not found in the chosen `.gts`, the tool reports it as failed — drag a
different `.gts` to retry. If `output_hashes/` has multiple characters, you are
prompted to pick one.

Output: `output_textures/<charname>/`

### Step 3 — pack modified textures back (nier_cli)

After editing textures in Photoshop / Substance etc., use `nier_cli_mgrr` to repack
them into the `.wtb` container that GBFR reads.

```
nier_cli_mgrr\nier_cli_mgrr.exe wtbPack -i <folder_with_dds> -o <output.wtb>
```

Useful flags:
- `-i`  input folder (DDS files exported from GraniteTextureReader)
- `-o`  output `.wtb` file
- `--platform pc` (default for GBFR)

The repacked `.wtb` goes into your mod's data directory alongside the other mod files
and is loaded by the Reloaded-II mod loader.

## How it works

`mmat` files are FlatBuffers binaries. String fields are stored as raw ASCII, so 64-char
lowercase hex strings can be extracted with a simple regex — no flatc required.

Those 64-char strings are simultaneously:
- the filename suffix of `.gtp` tile packets (`N_<hash>.gtp`)
- the `-f` argument to `GraniteTextureReader extract`

Each hash maps to one full material variant (all texture layers: albedo, normal, mask,
emissive, etc.). Passing `-l -1` extracts all layers at once.

## Folder structure

```
GBFR_modtools/
  01_minfo转Hash表.bat          <- step 1 entry (drag .minfo here)
  02_Hash表提取纹理.bat           <- step 2 entry (drag .gts here)
  _step1_parse_mmat.ps1         <- step 1 logic (internal)
  _step2_extract_tex.ps1        <- step 2 logic (internal)
  GraniteTextureReader.exe      <- you provide (gitignored)
  nier_cli_mgrr_<ver>/          <- you provide (gitignored)
  output_hashes/                <- step 1 output, auto-created (gitignored)
  output_textures/              <- step 2 output, auto-created (gitignored)
```
