# Get detailed system information for benchmark reporting
# Usage: .\scripts\get-system-info.ps1

Write-Output "=== System Information for Benchmarks ==="
Write-Output ""

# CPU Information
$cpu = Get-CimInstance -Class Win32_Processor | Select-Object -First 1
Write-Output "CPU: $($cpu.Name)"
Write-Output "Cores: $($cpu.NumberOfCores) logical, $($cpu.NumberOfLogicalProcessors) logical"
Write-Output "Max Clock Speed: $($cpu.MaxClockSpeed) MHz"
Write-Output "Current Clock Speed: $($cpu.CurrentClockSpeed) MHz"
Write-Output ""

# OS Information
$os = Get-CimInstance -Class Win32_OperatingSystem | Select-Object -First 1
Write-Output "OS: $($os.Caption) $($os.Version)"
Write-Output "Build: $($os.BuildNumber)"
Write-Output "Architecture: $($os.OSArchitecture)"
Write-Output ""

# Compiler Information
$cl = & cl 2>&1 | Select-String -Pattern "Microsoft.*Compiler Version" | Select-Object -First 1
if ($cl) {
    Write-Output "Compiler: $($cl.ToString().Trim())"
} else {
    Write-Output "Compiler: MSVC (cl.exe not found in PATH)"
}

# Check for Visual Studio version
$vsWhere = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if ($vsWhere) {
    $vsPath = $vsWhere | Select-Object -First 1
    $vsInfo = Get-ItemProperty $vsPath -Name DisplayName -ErrorAction SilentlyContinue
    if ($vsInfo) {
        Write-Output "Visual Studio: $($vsInfo.DisplayName)"
    }
}
Write-Output ""

# Build Configuration
Write-Output "Build Configuration:"
Write-Output "- Release mode with optimizations"
Write-Output "- Link Time Optimization (LTO) enabled: /GL /LTCG"
Write-Output "- Target: x64"
Write-Output ""

# Memory Information
$memory = Get-CimInstance -Class Win32_ComputerSystem | Select-Object -First 1
$totalMemory = [math]::Round($memory.TotalPhysicalMemory / 1GB, 2)
Write-Output "Memory: ${totalMemory} GB total"
Write-Output ""

# Date and Time
Write-Output "Benchmark Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Write-Output ""

# Hardware Details
Write-Output "=== Hardware Details ==="
Write-Output "Motherboard: $(Get-WmiObject Win32_BaseBoard | Select-Object -ExpandProperty Product)"
Write-Output "BIOS: $(Get-WmiObject Win32_BIOS | Select-Object -ExpandProperty SMBIOSBIOSVersion)"
Write-Output ""

# Environment Variables
Write-Output "=== Environment ==="
Write-Output "PROCESSOR_ARCHITECTURE: $env:PROCESSOR_ARCHITECTURE"
Write-Output "NUMBER_OF_PROCESSORS: $env:NUMBER_OF_PROCESSORS"
Write-Output "Platform: $env:Platform"
