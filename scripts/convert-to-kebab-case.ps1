# convert-to-kebab-case.ps1
# Converts all .hpp and .cpp files from snake_case to kebab-case
# Also updates #include statements and CMakeLists.txt references

param(
    [switch]$DryRun = $false,
    [switch]$Verbose = $false
)

$ErrorActionPreference = "Stop"

# Root directory (script's parent directory, i.e., the repo root)
$RootDir = Split-Path -Parent $PSScriptRoot
if (-not $RootDir -or -not (Test-Path "$RootDir\include\sigslot")) {
    $RootDir = Get-Location
}

Write-Host "=== Snake_case to kebab-case Converter ===" -ForegroundColor Cyan
Write-Host "Root directory: $RootDir"
if ($DryRun) {
    Write-Host "DRY RUN MODE - No changes will be made" -ForegroundColor Yellow
}
Write-Host ""

# Track all renames for updating references
$renames = @{}

# Function to convert snake_case to kebab-case
function Convert-ToKebabCase {
    param([string]$Name)
    return $Name -replace '_', '-'
}

# Function to check if filename contains underscore (needs conversion)
function Needs-Conversion {
    param([string]$Name)
    # Only convert if the base name (without extension) contains underscore
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($Name)
    return $baseName -match '_'
}

# Step 1: Find all files that need renaming
Write-Host "Step 1: Finding files to rename..." -ForegroundColor Green

$filesToRename = @()

# Search in include directory
$includeFiles = Get-ChildItem -Path "$RootDir\include" -Recurse -Include "*.hpp" -File -ErrorAction SilentlyContinue
foreach ($file in $includeFiles) {
    if (Needs-Conversion $file.Name) {
        $filesToRename += $file
    }
}

# Search in test directory
$testFiles = Get-ChildItem -Path "$RootDir\test" -Recurse -Include "*.cpp", "*.hpp" -File -ErrorAction SilentlyContinue
foreach ($file in $testFiles) {
    if (Needs-Conversion $file.Name) {
        $filesToRename += $file
    }
}

# Search in benchmark directory
$benchFiles = Get-ChildItem -Path "$RootDir\benchmark" -Recurse -Include "*.cpp", "*.hpp" -File -ErrorAction SilentlyContinue
foreach ($file in $benchFiles) {
    if (Needs-Conversion $file.Name) {
        $filesToRename += $file
    }
}

# Search in example directory
$exampleFiles = Get-ChildItem -Path "$RootDir\example" -Recurse -Include "*.cpp", "*.hpp" -File -ErrorAction SilentlyContinue
foreach ($file in $exampleFiles) {
    if (Needs-Conversion $file.Name) {
        $filesToRename += $file
    }
}

Write-Host "Found $($filesToRename.Count) files to rename:" -ForegroundColor Yellow
foreach ($file in $filesToRename) {
    $oldName = $file.Name
    $newName = Convert-ToKebabCase $oldName
    $relativePath = $file.FullName.Substring($RootDir.Length + 1)
    Write-Host "  $relativePath -> $newName"
    
    # Store the mapping (old base name -> new base name) for reference updates
    $oldBaseName = [System.IO.Path]::GetFileNameWithoutExtension($oldName)
    $newBaseName = Convert-ToKebabCase $oldBaseName
    $renames[$oldBaseName] = $newBaseName
    $renames[$oldName] = $newName
}

Write-Host ""

# Step 2: Update all source files that reference the renamed files
Write-Host "Step 2: Updating #include statements and references..." -ForegroundColor Green

$allSourceFiles = @()
$allSourceFiles += Get-ChildItem -Path "$RootDir\include" -Recurse -Include "*.hpp", "*.cpp" -File -ErrorAction SilentlyContinue
$allSourceFiles += Get-ChildItem -Path "$RootDir\test" -Recurse -Include "*.hpp", "*.cpp" -File -ErrorAction SilentlyContinue
$allSourceFiles += Get-ChildItem -Path "$RootDir\benchmark" -Recurse -Include "*.hpp", "*.cpp" -File -ErrorAction SilentlyContinue
$allSourceFiles += Get-ChildItem -Path "$RootDir\example" -Recurse -Include "*.hpp", "*.cpp" -File -ErrorAction SilentlyContinue

$updatedFiles = 0

foreach ($file in $allSourceFiles) {
    $content = Get-Content -Path $file.FullName -Raw -ErrorAction SilentlyContinue
    if (-not $content) { continue }
    
    $originalContent = $content
    
    # Replace each old filename with new filename in #include statements
    foreach ($oldName in $renames.Keys) {
        if ($oldName -match '\.') {
            # It's a full filename (with extension)
            $newName = $renames[$oldName]
            # Match #include "..." or #include <...> patterns
            $content = $content -replace "(?<=#include\s*[""<])([^""<>]*[/\\])?$([regex]::Escape($oldName))(?=[""<>])", "`$1$newName"
        }
    }
    
    if ($content -ne $originalContent) {
        $relativePath = $file.FullName.Substring($RootDir.Length + 1)
        Write-Host "  Updating: $relativePath"
        $updatedFiles++
        
        if (-not $DryRun) {
            Set-Content -Path $file.FullName -Value $content -NoNewline
        }
    }
}

Write-Host "Updated $updatedFiles source files" -ForegroundColor Yellow
Write-Host ""

# Step 3: Update CMakeLists.txt files (excluding build directories)
Write-Host "Step 3: Updating CMakeLists.txt files..." -ForegroundColor Green

$cmakeFiles = Get-ChildItem -Path $RootDir -Recurse -Include "CMakeLists.txt" -File -ErrorAction SilentlyContinue | 
    Where-Object { $_.FullName -notmatch "\\build[^\\]*\\" }
$updatedCmake = 0

foreach ($file in $cmakeFiles) {
    $content = Get-Content -Path $file.FullName -Raw -ErrorAction SilentlyContinue
    if (-not $content) { continue }
    
    $originalContent = $content
    
    # Replace snake_case references with kebab-case
    foreach ($oldName in $renames.Keys) {
        $newName = $renames[$oldName]
        $content = $content -replace [regex]::Escape($oldName), $newName
    }
    
    if ($content -ne $originalContent) {
        $relativePath = $file.FullName.Substring($RootDir.Length + 1)
        Write-Host "  Updating: $relativePath"
        $updatedCmake++
        
        if (-not $DryRun) {
            Set-Content -Path $file.FullName -Value $content -NoNewline
        }
    }
}

Write-Host "Updated $updatedCmake CMakeLists.txt files" -ForegroundColor Yellow
Write-Host ""

# Step 4: Actually rename the files
Write-Host "Step 4: Renaming files..." -ForegroundColor Green

foreach ($file in $filesToRename) {
    $oldPath = $file.FullName
    $newName = Convert-ToKebabCase $file.Name
    $newPath = Join-Path -Path $file.DirectoryName -ChildPath $newName
    
    $relativePath = $file.FullName.Substring($RootDir.Length + 1)
    Write-Host "  Renaming: $relativePath -> $newName"
    
    if (-not $DryRun) {
        Move-Item -Path $oldPath -Destination $newPath -Force
    }
}

# Step 5: Rename directories with underscores
Write-Host "Step 5: Renaming directories..." -ForegroundColor Green

$dirsToRename = @()
$searchPaths = @("$RootDir\include", "$RootDir\test", "$RootDir\benchmark", "$RootDir\example")
foreach ($searchPath in $searchPaths) {
    if (Test-Path $searchPath) {
        $dirs = Get-ChildItem -Path $searchPath -Recurse -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '_' } |
            Sort-Object { $_.FullName.Length } -Descending  # Rename deepest first
        $dirsToRename += $dirs
    }
}

foreach ($dir in $dirsToRename) {
    $oldPath = $dir.FullName
    $newName = Convert-ToKebabCase $dir.Name
    $newPath = Join-Path -Path $dir.Parent.FullName -ChildPath $newName
    
    $relativePath = $dir.FullName.Substring($RootDir.Length + 1)
    Write-Host "  Renaming dir: $relativePath -> $newName"
    
    if (-not $DryRun) {
        Move-Item -Path $oldPath -Destination $newPath -Force
    }
}

Write-Host ""
Write-Host "=== Conversion Complete ===" -ForegroundColor Cyan

if ($DryRun) {
    Write-Host ""
    Write-Host "This was a DRY RUN. To apply changes, run without -DryRun flag:" -ForegroundColor Yellow
    Write-Host "  .\scripts\convert-to-kebab-case.ps1" -ForegroundColor White
}

# Summary
Write-Host ""
Write-Host "Summary:" -ForegroundColor Green
Write-Host "  Files to rename: $($filesToRename.Count)"
Write-Host "  Source files updated: $updatedFiles"
Write-Host "  CMakeLists.txt updated: $updatedCmake"
