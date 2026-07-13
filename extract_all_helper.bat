@echo off
chcp 65001 >nul 2>&1
set GR=%~dp0GraniteTextureReader.exe
set BASE=D:\Steam\steamapps\common\Granblue Fantasy Relink\data\granite
set OUT=%~dp0output_textures\_extract_all

echo.
echo ============================================================
echo  GBFR extract-all helper
echo  Extracts ALL inline tiles from every gts file (layer 0)
echo  Covers both 2k and 4k.  Takes ~10-20 min, ~10GB disk.
echo ============================================================
echo.
echo Output: %OUT%
echo.

if not exist "%GR%" (
    echo [ERROR] GraniteTextureReader.exe not found.
    pause & exit /b 1
)

mkdir "%OUT%" 2>nul

echo --- 2k ---
for /L %%n in (0,1,11) do (
    if exist "%BASE%\2k\gts\%%n\%%n.gts" (
        echo [2k/%%n] ...
        "%GR%" extract-all -t "%BASE%\2k\gts\%%n\%%n.gts" -l 0 -o "%OUT%\2k_%%n"
    )
)

echo.
echo --- 4k ---
for /L %%n in (0,1,11) do (
    if exist "%BASE%\4k\gts\%%n\%%n.gts" (
        echo [4k/%%n] ...
        "%GR%" extract-all -t "%BASE%\4k\gts\%%n\%%n.gts" -l 0 -o "%OUT%\4k_%%n"
    )
)

echo.
echo === Done! Searching for pl1400_body and pl1400_sheath... ===
for /R "%OUT%" %%f in (*pl1400_body* *pl1400_sheath*) do (
    echo %%f
)
echo.
pause
