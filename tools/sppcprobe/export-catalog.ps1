param([string]$OutputPath)
$ErrorActionPreference = 'Stop'
try {
    # 只取三项已注册产品元数据；不查询/导出密钥、激活状态或有效期。
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
    try {
        $configuration = $base.OpenSubKey('SOFTWARE\Microsoft\Office\ClickToRun\Configuration', $false)
        if (!$configuration) { throw 'Office configuration is missing' }
        try {
            $version = [string]$configuration.GetValue('VersionToReport')
            $selection = @(([string]$configuration.GetValue('ProductReleaseIds')) -split ',')
        } finally { $configuration.Dispose() }
    } finally { $base.Dispose() }
    $rows = @(Get-CimInstance -ClassName SoftwareLicensingProduct `
        -Filter 'ApplicationID="0ff1ce15-a989-479d-af46-f275c6370663"' `
        -Property ID,ApplicationID,Name | Sort-Object ID | ForEach-Object {
            [PSCustomObject]@{sku_id=$_.ID; application_id=$_.ApplicationID; name=$_.Name}
        })
    if (!$version -or !$selection.Count -or !$rows.Count) { throw 'registration metadata is incomplete' }
    $document = [PSCustomObject]@{
        schema_version=1
        source=[PSCustomObject]@{
            kind='windows-cim-office-registration'
            office_version=$version
            product_release_ids=$selection
            captured_at=[DateTime]::UtcNow.ToString('o')
        }
        products=$rows
    }
    $json = ConvertTo-Json -Depth 5 -InputObject $document
    $encoding = New-Object Text.UTF8Encoding($false)
    if ($OutputPath) {
        $stream = [IO.File]::Open($OutputPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try {
            $bytes = $encoding.GetBytes($json)
            $stream.Write($bytes, 0, $bytes.Length)
        } finally { $stream.Dispose() }
    } else {
        [Console]::OutputEncoding = $encoding
        Write-Output $json
    }
} catch {
    Write-Error ('catalog export failed: ' + $_.Exception.GetType().FullName)
    exit 1
}
