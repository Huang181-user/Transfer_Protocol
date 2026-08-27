function Log-Realtime {
    param([string]$Message, [string]$Color = "Cyan")
    $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"
    Write-Host "[$ts] $Message" -ForegroundColor $Color
}

$startTime = [System.Diagnostics.Stopwatch]::StartNew()
Log-Realtime "[START] BAT DAU BIEN DICH PURE C++ CLIENT (FUSE API)..." "Yellow"

$buildDir = "build"
if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }

$rawIncDirs = @("src", "src\bridge", "src\rpc_client", "src\system", "src\vfs", "src\rpc_quic", "src\common", "src\nlohmann")
$gppIncArgs = @()
foreach ($d in ($rawIncDirs | Select-Object -Unique)) { $gppIncArgs += "-I`"$d`"" }

$searchRoots = @("C:\Program Files (x86)\WinFsp", "C:\Program Files\WinFsp", "C:\msys64")
$foundHeader = $null
foreach ($root in $searchRoots) {
    if (Test-Path $root) {
        $foundHeader = Get-ChildItem -Path $root -Filter "fuse.h" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($foundHeader) { break }
    }
}

if ($foundHeader) {
    $fuseIncDir = $foundHeader.DirectoryName
    $winfspIncDir = (Get-Item $fuseIncDir).Parent.FullName
    $gppIncArgs += "-I`"$winfspIncDir`""
    $gppIncArgs += "-I`"$fuseIncDir`""
    Log-Realtime "[WINFSP] Tim thay thuc muc Include: $winfspIncDir" "Green"
} else {
    Log-Realtime "[FATAL] Khong tim thay WinFSP!" "Red"
    exit 1
}

$foundLib = $null
foreach ($root in $searchRoots) {
    if (Test-Path $root) {
        $foundLib = Get-ChildItem -Path $root -Filter "winfsp-x64.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($foundLib) { break }
    }
}

$fuseLibArg = ""
if ($foundLib) {
    $fuseLibArg = "`"" + $foundLib.FullName + "`""
    Log-Realtime "[WINFSP] Tim thay thuc muc Library: $fuseLibArg" "Green"
} else {
    Log-Realtime "[FATAL] Khong tim thay winfsp-x64.dll!" "Red"
    exit 1
}

$libs = @("-L$buildDir", "-lws2_32", "-liphlpapi", "-lcredui", $fuseLibArg, "-lsodium", "-lmsquic")
$cppFiles = Get-ChildItem -Path "src" -Recurse -Include *.cpp, *.c | ForEach-Object { $_.FullName }

Log-Realtime "[COMPILE] Dang bien dich..." "Magenta"
$argsList = $gppIncArgs + $cppFiles + @("-std=c++17", "-O3", "-DQUIC_SAL_STUB", "-o", "$buildDir\huang_client.exe") + $libs

& g++ @argsList

if ($LASTEXITCODE -ne 0) {
    Log-Realtime "[FATAL] Bien dich that bai!" "Red"
    exit 1
}

$elapsed = $startTime.Elapsed.TotalSeconds.ToString("F2")
Log-Realtime "==========================================================================" "Yellow"
Log-Realtime "[SUCCESS] PURE C++ CLIENT ĐÃ SẴN SÀNG TRONG $elapsed GIÂY!" "Green"
Log-Realtime "==========================================================================" "Yellow"
