@echo off
chcp 65001 >nul 2>&1
set GR=%~dp0GraniteTextureReader.exe
set BASE=D:\Steam\steamapps\common\Granblue Fantasy Relink\data\granite
set OUT=%~dp0output_textures\_extract_all

echo.
echo ============================================================
echo  GBFR extract-all helper
echo  Extracts ALL inline tiles from every gts file (layer 0)
echo  This will take a while and use significant disk space.
echo ============================================================
echo.
echo Output folder: %OUT%
echo.

if not exist "%GR%" (
    echo [ERROR] GraniteTextureReader.exe not found in this folder.
    pause & exit /b 1
)

mkdir "%OUT%" 2>nul

echo --- 2k gts files ---
for /L %%n in (0,1,11) do (
    set GTS=%BASE%\2k\gts\%%n\%%n.gts
    if exist "%BASE%\2k\gts\%%n\%%n.gts" (
        echo.
        echo [%%n/11] %BASE%\2k\gts\%%n\%%n.gts
        "%GR%" extract-all -t "%BASE%\2k\gts\%%n\%%n.gts" -l 0 -o "%OUT%\2k_%%n"
    )
)

echo.
echo --- Done! ---
echo Results in: %OUT%
echo.
echo Now checking for pl1400_body and pl1400_sheath files...
dir /b "%OUT%\*\*pl1400_body*" "%OUT%\*\*pl1400_sheath*" 2>nul
pause
