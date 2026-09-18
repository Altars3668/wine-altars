param([Parameter(Mandatory=$true)][string]$RunDirectory, [switch]$AllScopes)
$ErrorActionPreference = 'Stop'
$code = 1
$stage = 'read-acl'
try {
    # 新目录不复用旧凭据；取消继承后只允许当前用户和 SYSTEM 访问。
    $acl = Get-Acl -LiteralPath $RunDirectory
    $stage = 'build-private-acl'
    $acl.SetAccessRuleProtection($true, $false)
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
    foreach ($sid in @($identity, (New-Object Security.Principal.SecurityIdentifier('S-1-5-18')))) {
        $rule = New-Object Security.AccessControl.FileSystemAccessRule(
            $sid, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
        $acl.AddAccessRule($rule)
    }
    $stage = 'set-private-acl'
    Set-Acl -LiteralPath $RunDirectory -AclObject $acl
    $exe = Join-Path $RunDirectory 'wamprobe.exe'
    $code = 0
    $cases = @(
        @{Mode='plain'; Name='plain'; Scope='service::ssl.live.com::MBI_SSL_SHORT openid profile'},
        @{Mode='office'; Name='office'; Scope='service::ssl.live.com::MBI_SSL_SHORT openid profile'}
    )
    if ($AllScopes) {
        $scopes = @(
            'service::officeapps.live.com::MBI_SSL_SHORT openid profile',
            'service::ssl.live.com::MBI_SSL_SHORT openid profile',
            'openid service::https://graph.microsoft.com/.default::DELEGATION profile',
            'https://consentservice.microsoft.com/checkin/UnifiedUserConsent.Read openid profile',
            'https://substrate.office.com/.default openid profile',
            'service::ads.arcct.msn.com::MBI_SSL openid profile',
            'service::messaging.engagement.office.com::MBI_SSL_SHORT openid profile',
            'service::outlook.office.com::MBI_SSL openid profile',
            'service::substrate.office.com::MBI_SSL openid profile'
        )
        $cases = @($scopes | ForEach-Object {
            @{Mode='office-silent'; Name=($_ -replace '[^A-Za-z0-9._-]', '_'); Scope=$_}
        })
    }
    foreach ($case in $cases) {
        $mode = $case.Mode
        $name = $case.Name
        $stage = 'create-output-' + $name
        $output = Join-Path $RunDirectory $name
        New-Item -ItemType Directory -Path $output | Out-Null
        $stage = 'execute-' + $name
        & $exe $mode $output $case.Scope 2>&1 | Out-File -Encoding utf8 (Join-Path $RunDirectory ($name + '.log'))
        $exitCode = $LASTEXITCODE
        Add-Content -Encoding utf8 (Join-Path $RunDirectory ($name + '.log')) ('process-exit=' + $exitCode)
        if ($exitCode -ne 0) { $code = 1 }
    }
} catch {
    # 异常可能携带请求参数，不记录 Message；只记录类型及 HRESULT。
    $errorRecord = $_
    $failure = 'stage=' + $stage + ' type=' + $errorRecord.Exception.GetType().FullName +
        ' hr=' + $errorRecord.Exception.HResult + ' id=' + $errorRecord.FullyQualifiedErrorId
    [IO.File]::WriteAllText((Join-Path $RunDirectory 'failed.log'), $failure)
    $code = 1
} finally {
    [IO.File]::WriteAllText((Join-Path $RunDirectory 'completed'), [string]$code)
}
exit $code
