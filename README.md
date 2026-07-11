# GBFR_modtools

GUI tools for Granblue Fantasy Relink texture modding.

## Setup

Place these binaries in the root of this folder (next to `GBFR提取工具.bat`):

| Binary | Download |
|--------|----------|
| `GraniteTextureReader.exe` | https://github.com/Nenkai/GraniteTextureReader/releases |
| `flatc.exe` | https://github.com/google/flatbuffers/releases → `Windows.flatc.binary.zip` |

All binaries are gitignored. `MMat_ModelMaterial.fbs` (FlatBuffers schema) is already in this repo.

## Usage

Double-click `GBFR提取工具.bat` to open the GUI.

### Extract tab

- Left list: drop `.minfo` files (add all model components for a full character — `pl`, `fp`, `wp`, `fn`, `np`)
- Right list: drop `.gts` files (can drop multiple to search across tile sets)
- Click **Extract** — processes all minfo × gts combinations
- Output: `output_textures/<charId>/<gts_name>/`

Common `.gts` paths:
```
data/granite/2k/gts/1/1.gts   <- main characters / faces
data/granite/2k/gts/0/0.gts
data/granite/4k/gts/1/1.gts   <- 4K variants
```

### Pack tab

**WTB Textures** (`data/texture/2k/*.texture`)

These are the non-Granite textures (msk3/msk4/msk5). The format is WTB (PlatinumGames
texture container with embedded DDS).

1. Drop original `.texture` files into the left list
2. Click **Extract DDS** → saves DDS files to `output_dds/`
3. Edit the DDS files externally (Photoshop, Substance, etc.)
4. Drop edited `.dds` files into the right list
5. Click **Pack to .texture** → creates replacement files in `output_mod/data/texture/2k/`

Naming: `pl1400_skin_lod0_msk2_0.dds` packs back into `pl1400_skin_lod0_msk2.texture`.

**mmat Edit** (requires `flatc.exe`)

1. Drop `.mmat` files into the left list, click **Decode to JSON**
   - JSON files open automatically in Explorer
2. Edit the JSON in any text editor
3. Drop edited `.json` files into the right list, click **Encode to .mmat**
   - Output: `output_mod/mmat_repacked/<n>.mmat`

## Texture types and where they come from

| Type | Source | Tool |
|------|--------|------|
| `_albd`, `_nrml`, `_msk1`, `_msk2` | Granite GTS tile system | GraniteTextureReader |
| `_msk3`, `_msk4`, `_msk5` | `data/texture/2k/*.texture` (WTB) | built-in WTB packer |

## Output mod structure

```
output_mod/
  data/
    texture/
      2k/
        pl1400_skin_lod0_msk2.texture   <- repacked WTB
        ...
  mmat_repacked/
    0.mmat                              <- edited material config
    ...
```

Place `output_mod/data/` into your Reloaded-II mod folder.

## How it works (Extract)

`mmat` files are FlatBuffers binaries. String fields are stored as raw ASCII, so 64-char
lowercase hex strings can be extracted with a simple regex — no flatc required.

Those 64-char hashes are simultaneously:
- the filename suffix of `.gtp` tile packets (`N_<hash>.gtp`)
- the `-f` argument to `GraniteTextureReader extract`

Each hash maps to one full material variant (all layers: albedo, normal, masks, emissive).
`-l -1` extracts all layers at once.

## Folder structure

```
GBFR_modtools/
  GBFR提取工具.bat             <- main entry point
  GBFR_Extractor.ps1          <- GUI logic
  MMat_ModelMaterial.fbs      <- FlatBuffers schema for mmat
  _step1_parse_mmat.ps1       <- CLI fallback: minfo -> hash table
  _step2_extract_tex.ps1      <- CLI fallback: hash table -> textures
  GraniteTextureReader.exe    <- you provide (gitignored)
  flatc.exe                   <- you provide (gitignored)
  nier_cli_mgrr_<ver>/        <- optional (gitignored)
  output_hashes/              <- auto-created (gitignored)
  output_textures/            <- auto-created (gitignored)
  output_dds/                 <- auto-created (gitignored)
  output_mod/                 <- auto-created (gitignored)
```
