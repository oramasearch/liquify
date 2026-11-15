param(
    [Parameter(Mandatory = $true)]
    [string]$BinaryPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [string]$PfxPath = '',
    [string]$PfxPassword = '',
    [string]$Publisher = 'CN=Liquify CI',
    [string]$TimestampUrl = 'http://timestamp.digicert.com',
    [string]$PackageVersion = '0.0.1.0'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

# A release is never self-signed (liquify#77). LIQUIFY_RELEASE=1, or a build of a v* tag, requires a
# real certificate (-PfxPath) and its publisher; the self-signed CN=Liquify CI path is for local and
# test builds only.
$isRelease = ($env:LIQUIFY_RELEASE -eq '1') -or ("$env:GITHUB_REF" -like 'refs/tags/v*')
if ($isRelease) {
    if ([string]::IsNullOrWhiteSpace($PfxPath) -or -not (Test-Path $PfxPath)) {
        throw 'refusing to package a release without a signing certificate (-PfxPath)'
    }
    if ([string]::IsNullOrWhiteSpace($Publisher) -or $Publisher -eq 'CN=Liquify CI') {
        throw 'refusing to package a release under the test publisher CN=Liquify CI'
    }
}

if ($PackageVersion -notmatch '^(\d+)\.(\d+)\.(\d+)\.(\d+)$') {
    throw 'PackageVersion must contain four numeric components'
}
$versionParts = $PackageVersion.Split('.')

$resolvedBinary = (Resolve-Path $BinaryPath).Path
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
$temporaryRoot = $env:RUNNER_TEMP
if ([string]::IsNullOrWhiteSpace($temporaryRoot)) {
    $temporaryRoot = [System.IO.Path]::GetTempPath()
}
$workDirectory = Join-Path $temporaryRoot ([System.Guid]::NewGuid().ToString('N'))
$layoutDirectory = Join-Path $workDirectory 'layout'
$assetsDirectory = Join-Path $layoutDirectory 'Assets'
$unsignedPackage = Join-Path $workDirectory 'liquify-unsigned.msix'
$artifactVersion = $versionParts[0..2] -join '.'
$finalPackage = Join-Path $resolvedOutput "liquify-$artifactVersion-windows-x64.msix"
$unsignedDigest = "$finalPackage.unsigned.sha256"
$signedDigest = "$finalPackage.sha256"
$testCertificate = $null
$installedPackage = $null
$visitedDependencies = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase
)

function Find-WindowsSdkTool {
    param([Parameter(Mandatory = $true)][string]$Name)

    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $matches = Get-ChildItem -Path $sdkRoot -Filter $Name -Recurse |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object -Property FullName -Descending
    if ($matches.Count -eq 0) {
        throw "$Name was not found in the Windows SDK"
    }
    return $matches[0].FullName
}

function Write-SolidPng {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][int]$Width,
        [Parameter(Mandatory = $true)][int]$Height
    )

    $bitmap = New-Object System.Drawing.Bitmap($Width, $Height)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([System.Drawing.Color]::FromArgb(15, 23, 42))
            $brush = New-Object System.Drawing.SolidBrush(
                [System.Drawing.Color]::FromArgb(56, 189, 248)
            )
            try {
                $margin = [Math]::Max(2, [int]([Math]::Min($Width, $Height) / 5))
                $graphics.FillRectangle(
                    $brush,
                    $margin,
                    $margin,
                    $Width - (2 * $margin),
                    $Height - (2 * $margin)
                )
            }
            finally {
                $brush.Dispose()
            }
        }
        finally {
            $graphics.Dispose()
        }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $bitmap.Dispose()
    }
}

function Write-Digest {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$OutputPath,
        [Parameter(Mandatory = $true)][string]$DisplayName
    )

    $hash = (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
    "$hash  $DisplayName" | Set-Content -Path $OutputPath -Encoding ascii
}

function Copy-RuntimeDependencies {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ObjdumpPath,
        [Parameter(Mandatory = $true)][string[]]$SearchDirectories
    )

    if (-not $visitedDependencies.Add($Path)) {
        return
    }

    $dependencyNames = & $ObjdumpPath --private-headers $Path |
        Select-String -Pattern '^\s*DLL Name:\s*(.+)$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() }
    foreach ($dependencyName in $dependencyNames) {
        $resolvedDependency = $null
        foreach ($directory in $SearchDirectories) {
            if ([string]::IsNullOrWhiteSpace($directory)) {
                continue
            }
            $candidate = Join-Path $directory $dependencyName
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $resolvedDependency = (Resolve-Path -LiteralPath $candidate).Path
                break
            }
        }

        if ($null -eq $resolvedDependency) {
            throw "runtime dependency was not found: $dependencyName"
        }

        $windowsDirectory = [System.IO.Path]::GetFullPath($env:SystemRoot)
        if ($resolvedDependency.StartsWith(
                $windowsDirectory,
                [System.StringComparison]::OrdinalIgnoreCase
            )) {
            continue
        }

        $destination = Join-Path $layoutDirectory $dependencyName
        if (-not (Test-Path -LiteralPath $destination)) {
            Copy-Item -LiteralPath $resolvedDependency -Destination $destination
        }
        Copy-RuntimeDependencies $destination $ObjdumpPath $SearchDirectories
    }
}

try {
    New-Item -ItemType Directory -Path $layoutDirectory, $assetsDirectory, $resolvedOutput -Force |
        Out-Null
    Copy-Item -Path $resolvedBinary -Destination (Join-Path $layoutDirectory 'liquify.exe')
    Copy-Item `
        -Path (Join-Path (Split-Path -Parent $resolvedBinary) 'frontends') `
        -Destination (Join-Path $layoutDirectory 'frontends') `
        -Recurse

    $objdump = (Get-Command 'llvm-objdump.exe').Source
    $dependencySearchDirectories = @((Split-Path -Parent $resolvedBinary))
    if (-not [string]::IsNullOrWhiteSpace($env:OPENSSL_ROOT_DIR)) {
        $dependencySearchDirectories += Join-Path $env:OPENSSL_ROOT_DIR 'bin'
    }
    $dependencySearchDirectories += $env:PATH -split ';'
    Copy-RuntimeDependencies `
        (Join-Path $layoutDirectory 'liquify.exe') `
        $objdump `
        $dependencySearchDirectories
    Get-ChildItem (Join-Path $layoutDirectory 'frontends') -Filter '*.dll' | ForEach-Object {
        Copy-RuntimeDependencies $_.FullName $objdump $dependencySearchDirectories
    }

    [xml]$manifest = Get-Content -Raw (Join-Path $scriptDirectory 'AppxManifest.xml')
    $manifest.Package.Identity.SetAttribute('Publisher', $Publisher)
    $manifest.Package.Identity.SetAttribute('Version', $PackageVersion)
    $manifestPath = Join-Path $layoutDirectory 'AppxManifest.xml'
    $writerSettings = New-Object System.Xml.XmlWriterSettings
    $writerSettings.Encoding = New-Object System.Text.UTF8Encoding($false)
    $writerSettings.Indent = $true
    $manifestWriter = [System.Xml.XmlWriter]::Create($manifestPath, $writerSettings)
    try {
        $manifest.Save($manifestWriter)
    }
    finally {
        $manifestWriter.Dispose()
    }

    Add-Type -AssemblyName System.Drawing
    Write-SolidPng (Join-Path $assetsDirectory 'StoreLogo.png') 50 50
    Write-SolidPng (Join-Path $assetsDirectory 'Square44x44Logo.png') 44 44
    Write-SolidPng (Join-Path $assetsDirectory 'Square150x150Logo.png') 150 150
    Write-SolidPng (Join-Path $assetsDirectory 'Wide310x150Logo.png') 310 150
    Write-SolidPng (Join-Path $assetsDirectory 'Square310x310Logo.png') 310 310

    $makeAppx = Find-WindowsSdkTool 'makeappx.exe'
    $signTool = Find-WindowsSdkTool 'signtool.exe'
    & $makeAppx pack /d $layoutDirectory /p $unsignedPackage /o
    Write-Digest $unsignedPackage $unsignedDigest (Split-Path -Leaf $finalPackage)

    if ([string]::IsNullOrWhiteSpace($PfxPath)) {
        $securePassword = ConvertTo-SecureString 'liquify-ci-certificate' -AsPlainText -Force
        $testCertificate = New-SelfSignedCertificate `
            -Type Custom `
            -Subject $Publisher `
            -FriendlyName 'Liquify CI package signing' `
            -CertStoreLocation 'Cert:\CurrentUser\My' `
            -KeyAlgorithm RSA `
            -KeyLength 2048 `
            -KeyUsage DigitalSignature `
            -TextExtension @(
                '2.5.29.37={text}1.3.6.1.5.5.7.3.3',
                '2.5.29.19={text}'
            )
        $PfxPath = Join-Path $workDirectory 'liquify-ci.pfx'
        $certificatePath = Join-Path $workDirectory 'liquify-ci.cer'
        Export-PfxCertificate `
            -Cert $testCertificate `
            -FilePath $PfxPath `
            -Password $securePassword |
            Out-Null
        Export-Certificate -Cert $testCertificate -FilePath $certificatePath | Out-Null
        Import-Certificate `
            -FilePath $certificatePath `
            -CertStoreLocation 'Cert:\CurrentUser\TrustedPeople' |
            Out-Null
        $PfxPassword = 'liquify-ci-certificate'
        $certificateKind = 'self-signed CI certificate'
    }
    else {
        $PfxPath = (Resolve-Path $PfxPath).Path
        $certificateKind = 'release certificate'
    }

    Copy-Item -Path $unsignedPackage -Destination $finalPackage -Force
    & $signTool sign `
        /fd sha256 `
        /tr $TimestampUrl `
        /td sha256 `
        /f $PfxPath `
        /p $PfxPassword `
        $finalPackage
    & $signTool verify /pa /v $finalPackage
    Write-Digest $finalPackage $signedDigest (Split-Path -Leaf $finalPackage)

    Get-AppxPackage -Name 'OramaSearch.Liquify' -ErrorAction SilentlyContinue |
        Remove-AppxPackage -ErrorAction SilentlyContinue
    Add-AppxPackage -Path $finalPackage
    $installedPackage = Get-AppxPackage -Name 'OramaSearch.Liquify'
    if ($null -eq $installedPackage) {
        throw 'MSIX installation did not register OramaSearch.Liquify'
    }
    $installedBinary = Join-Path $installedPackage.InstallLocation 'liquify.exe'
    $savedPath = $env:PATH
    try {
        $env:PATH = [System.Environment]::SystemDirectory
        & $installedBinary version
        if ($LASTEXITCODE -ne 0) {
            throw "installed liquify version exited with $LASTEXITCODE"
        }
    }
    finally {
        $env:PATH = $savedPath
    }
    Remove-AppxPackage -Package $installedPackage.PackageFullName
    $installedPackage = $null

    Write-Host "Windows signing: $certificateKind"
    Write-Host "MSIX artifact: $finalPackage"
}
finally {
    if ($null -ne $installedPackage) {
        Remove-AppxPackage `
            -Package $installedPackage.PackageFullName `
            -ErrorAction SilentlyContinue
    }
    if ($null -ne $testCertificate) {
        Remove-Item -Path "Cert:\CurrentUser\My\$($testCertificate.Thumbprint)" -ErrorAction SilentlyContinue
        Remove-Item `
            -Path "Cert:\CurrentUser\TrustedPeople\$($testCertificate.Thumbprint)" `
            -ErrorAction SilentlyContinue
    }
    Remove-Item -Path $workDirectory -Recurse -Force -ErrorAction SilentlyContinue
}
