Set-StrictMode -Version Latest

function Get-CharacterUiAssetCategory {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$CharacterId
    )

    $identityMatch = [regex]::Match($CharacterId, '^(?<prefix>[a-z]+)(?<number>\d+)$', [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (-not $identityMatch.Success) { return $null }

    $normalized = $RelativePath.Replace('\', '/').ToLowerInvariant()
    $character = $CharacterId.ToLowerInvariant()
    $prefix = $identityMatch.Groups['prefix'].Value.ToLowerInvariant()
    $number = $identityMatch.Groups['number'].Value
    $identityPattern = '(?:' + ((@($number, $character) | Select-Object -Unique | ForEach-Object {
        [regex]::Escape($_)
    }) -join '|') + ')'

    switch -Regex ($normalized) {
        "^ui/(?:fhd/)?layouts/common/chara_plprm/noatlastextures/cmn_chrprm_${identityPattern}_\d+\.wtb$" { return 'status_portrait' }
        "^ui/(?:fhd/)?layouts/common/chara_voice/noatlastextures/cmn_chrvo_${identityPattern}_\d+\.wtb$" { return 'voice_portrait' }
        "^ui/(?:fhd/)?layouts/common/image_chain/noatlastextures/cmn_imgchain_${identityPattern}(?:_glow)?\.wtb$" { return 'sba_chain' }
        "^ui/(?:fhd/)?layouts/common/image_chara/noatlastextures/cmn_imgchr_${identityPattern}(?:_glow)?\.wtb$" { return 'character_art' }
        "^ui/(?:fhd/)?layouts/common/image_chrcolor/noatlastextures/cmn_imgcol_${identityPattern}_c\d+\.wtb$" { return 'color_preview' }
    }

    if ($prefix -ne 'pl') { return $null }

    $playerId = "pl$number"
    $playerPattern = [regex]::Escape($playerId)
    $numberPattern = [regex]::Escape($number)
    $weaponFamilyPattern = if ($number.Length -eq 4) {
        'wp' + [regex]::Escape($number.Substring(0, 2)) + '\d{2}'
    } else {
        'wp' + $numberPattern
    }

    switch -Regex ($normalized) {
        "^ui/(?:fhd/)?layouts/common/image_chara02/noatlastextures/cmn_imgchr02_${numberPattern}(?:_glow)?\.wtb$" { return 'character_art_alt' }
        "^ui/(?:fhd/)?layouts/common/image_chara_s/noatlastextures/cmn_imgchr_s_${numberPattern}(?:_glow)?\.wtb$" { return 'character_art_small' }
        "^ui/(?:fhd/)?layouts/common/image_fate/noatlastextures/cmn_imgfate_${numberPattern}_\d+_\d+\.wtb$" { return 'fate_episode' }
        "^ui/(?:fhd/)?layouts/common/image_sboard/noatlastextures/cmn_img_sboard\d+_${playerPattern}_\d+\.wtb$" { return 'sboard' }
        "^ui/(?:fhd/)?layouts/hud/linklevel/noatlastextures/hud_lnklv_${playerPattern}\.wtb$" { return 'link_hud' }
        "^ui/(?:fhd/)?layouts/pause/background03/noatlastextures/ps_bg03_${playerPattern}_.+\.wtb$" { return 'menu_background' }
        "^ui/(?:fhd/)?layouts/pause/limitbonus/noatlastextures/ps_lb_stree_${playerPattern}(?:_glow)?\.wtb$" { return 'mastery_art' }
        "^ui/(?:fhd/)?layouts/telop/chainburst/noatlastextures/tlp_cbst_${numberPattern}\.wtb$" { return 'chain_burst' }
        "^ui/(?:fhd/)?layouts/blacksmith/weapon/noatlastextures/bs_wpn_${playerPattern}\.wtb$" { return 'weapon_overview' }
        "^ui/(?:fhd/)?layouts/common/image_equip/noatlastextures/cmn_imgequ_${weaponFamilyPattern}(?:_glow)?\.wtb$" { return 'weapon_preview' }
        "^ui/(?:fhd/)?layouts/common/image_equip_s/noatlastextures/cmn_imgequ_s_${weaponFamilyPattern}\.wtb$" { return 'weapon_preview_small' }
    }

    return $null
}
