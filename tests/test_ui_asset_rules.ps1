Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\scripts\workspace\ui_asset_rules.ps1')
$labels = ConvertFrom-Json ([IO.File]::ReadAllText(
    (Join-Path $PSScriptRoot '..\_lib\ui_asset_categories_zh.json'),
    [Text.Encoding]::UTF8
))

$cases = @(
    @{ Path = 'ui/fhd/layouts/common/chara_plprm/noatlastextures/cmn_chrprm_1400_00.wtb'; Id = 'pl1400'; Expected = 'status_portrait' }
    @{ Path = 'ui/layouts/common/chara_voice/noatlastextures/cmn_chrvo_pl1400_08.wtb'; Id = 'pl1400'; Expected = 'voice_portrait' }
    @{ Path = 'ui/fhd/layouts/common/image_chara02/noatlastextures/cmn_imgchr02_1400_glow.wtb'; Id = 'pl1400'; Expected = 'character_art_alt' }
    @{ Path = 'ui/layouts/common/image_chara_s/noatlastextures/cmn_imgchr_s_1400.wtb'; Id = 'pl1400'; Expected = 'character_art_small' }
    @{ Path = 'ui/fhd/layouts/common/image_fate/noatlastextures/cmn_imgfate_1400_13_02.wtb'; Id = 'pl1400'; Expected = 'fate_episode' }
    @{ Path = 'ui/layouts/common/image_sboard/noatlastextures/cmn_img_sboard02_pl1400_03.wtb'; Id = 'pl1400'; Expected = 'sboard' }
    @{ Path = 'ui/fhd/layouts/pause/background03/noatlastextures/ps_bg03_pl1400_b_wp1405_c10.wtb'; Id = 'pl1400'; Expected = 'menu_background' }
    @{ Path = 'ui/layouts/common/image_equip/noatlastextures/cmn_imgequ_wp1405_glow.wtb'; Id = 'pl1400'; Expected = 'weapon_preview' }
    @{ Path = 'ui/fhd/layouts/common/image_equip_s/noatlastextures/cmn_imgequ_s_wp1402.wtb'; Id = 'pl1400'; Expected = 'weapon_preview_small' }
    @{ Path = 'ui/layouts/blacksmith/weapon/noatlastextures/bs_wpn_pl1400.wtb'; Id = 'pl1400'; Expected = 'weapon_overview' }
    @{ Path = 'ui/fhd/layouts/common/image_item/noatlastextures/cmn_imgitm_01_1400.wtb'; Id = 'pl1400'; Expected = $null }
    @{ Path = 'ui/fhd/layouts/common/image_summon/noatlastextures/cmn_imgsmn_1400.wtb'; Id = 'pl1400'; Expected = $null }
    @{ Path = 'ui/atlas/common_icon_mini.wtb'; Id = 'pl1400'; Expected = $null }
    @{ Path = 'ui/layouts/common/image_fate/noatlastextures/cmn_imgfate_1400_01_01.wtb'; Id = 'fp1400'; Expected = $null }
    @{ Path = 'ui/layouts/common/image_chara/noatlastextures/cmn_imgchr_1400.wtb'; Id = 'fp1400'; Expected = 'character_art' }
    @{ Path = 'ui/layouts/common/image_equip/noatlastextures/cmn_imgequ_wp1500.wtb'; Id = 'pl1400'; Expected = $null }
    @{ Path = 'ui/fhd/layouts/common/chara_plprm/noatlastextures/cmn_chrprm_em2100_00.wtb'; Id = 'pl2500'; Aliases = @('em2100'); Expected = 'status_portrait' }
    @{ Path = 'ui/layouts/common/chara_voice/noatlastextures/cmn_chrvo_em2100_09.wtb'; Id = 'pl2500'; Aliases = @('em2100'); Expected = 'voice_portrait' }
    @{ Path = 'ui/fhd/layouts/common/image_chara/noatlastextures/cmn_imgchr_em2100_glow.wtb'; Id = 'pl2500'; Aliases = @('em2100'); Expected = 'character_art' }
    @{ Path = 'ui/layouts/common/image_chara_s/noatlastextures/cmn_imgchr_s_em2100.wtb'; Id = 'pl2500'; Aliases = @('em2100'); Expected = 'character_art_small' }
    @{ Path = 'ui/layouts/common/image_chara/noatlastextures/cmn_imgchr_em2100.wtb'; Id = 'pl2500'; Expected = $null }
    @{ Path = 'ui/layouts/telop/bossbattle/noatlastextures/tlp_bbtl_em2100_01.wtb'; Id = 'pl2500'; Aliases = @('em2100'); Expected = $null }
)

foreach ($case in $cases) {
    $arguments = @{
        RelativePath = $case.Path
        CharacterId = $case.Id
    }
    if ($case.ContainsKey('Aliases')) { $arguments.IdentityAliases = $case.Aliases }
    $actual = Get-CharacterUiAssetCategory @arguments
    if ($actual -ne $case.Expected) {
        throw "UI rule mismatch for $($case.Id) / $($case.Path): expected '$($case.Expected)', got '$actual'"
    }
    if ($null -ne $actual -and $null -eq $labels.PSObject.Properties[$actual]) {
        throw "UI category label is missing: $actual"
    }
}

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ('gbfr_ui_alias_test_' + [Guid]::NewGuid().ToString('N'))
try {
    $tableDirectory = Join-Path $temporaryRoot 'system\table'
    New-Item -ItemType Directory -Path $tableDirectory -Force | Out-Null
    $recordSize = 360
    $tableBytes = New-Object byte[] (8 + 2 * $recordSize)
    [BitConverter]::GetBytes([uint32]2).CopyTo($tableBytes, 0)

    $testRows = @(
        @{ Id = '2400'; Alias = 'em2000' }
        @{ Id = '2500'; Alias = 'em2100' }
    )
    for ($index = 0; $index -lt $testRows.Count; $index++) {
        $recordOffset = 8 + $index * $recordSize
        $idBytes = [Text.Encoding]::ASCII.GetBytes($testRows[$index].Id)
        $aliasBytes = [Text.Encoding]::ASCII.GetBytes($testRows[$index].Alias)
        [Array]::Copy($idBytes, 0, $tableBytes, $recordOffset, $idBytes.Length)
        [Array]::Copy($aliasBytes, 0, $tableBytes, $recordOffset + 16, $aliasBytes.Length)
    }
    [IO.File]::WriteAllBytes((Join-Path $tableDirectory 'chara_icon.tbl'), $tableBytes)

    $pl2500Aliases = @(Get-CharacterUiIdentityAliases -GameDataRoot $temporaryRoot -CharacterId 'pl2500')
    if ($pl2500Aliases.Count -ne 1 -or $pl2500Aliases[0] -ne 'em2100') {
        throw "UI alias table mismatch for pl2500: $($pl2500Aliases -join ',')"
    }
    if (@(Get-CharacterUiIdentityAliases -GameDataRoot $temporaryRoot -CharacterId 'pl2300').Count -ne 0) {
        throw 'Unexpected UI alias returned for pl2300'
    }
} finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "UI asset rule tests passed: $($cases.Count) rules + alias table"
