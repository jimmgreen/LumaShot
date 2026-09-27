param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference = 'Stop'
$rows = Import-Csv -LiteralPath $Path
$rows | Where-Object event -In @('mode_demo','mode_isolated','include_cursor','theme','refresh_hz','process_priority','process_affinity','monitor_width','monitor_height') | Select-Object event,value | Format-Table -AutoSize
function Summarize($groups, $column) {
    $groups | ForEach-Object {
        $values = @($_.Group | ForEach-Object { [double]$_.$column } | Sort-Object)
        [pscustomobject]@{
            stage=$_.Name; samples=$values.Count
            p50_ms=[math]::Round($values[[math]::Floor(($values.Count-1)*.5)],3)
            p95_ms=[math]::Round($values[[math]::Floor(($values.Count-1)*.95)],3)
            max_ms=[math]::Round($values[-1],3)
            over_16ms=@($values | Where-Object {$_ -gt 16.667}).Count
        }
    } | Format-Table -AutoSize
}
Summarize ($rows | Where-Object event -In @('capture_and_blur','show_views','render_initialize','draw_commands','end_draw','present','frame_deferred','paint','pointer_move') | Group-Object event) duration_ms
Summarize ($rows | Where-Object event -EQ input_age_ms | Group-Object event) value
Write-Output 'Paint modes: 0 = hover/magnifier, 1 = selecting, 2 = annotation/tools'
Summarize ($rows | Where-Object event -EQ paint | Group-Object value) duration_ms
