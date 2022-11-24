# This script converts Visual Studio 2022 upgraded VS2008 projects to produce VS2022 
# projects that will build executables that will run on all versions of windows since XP
#
ForEach ($arg in ($args))
{
    if (-not (Test-Path -Path $arg -PathType Any)) {if (-not (Get-Item -Path $arg -ErrorAction Ignore)) {Write-Host "No such file: $arg"; continue; }}
    ForEach ($file in (Get-Item -Path $arg))
    {
        $string =  Get-Content -Path $file -Raw
        if ($string.Contains("<WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>")) {Write-Host "$file - already converted"; continue; }
    	Write-Host "Processing: $file"
        $string = $string.Replace(
"<Keyword>Win32Proj</Keyword>
", 
"<Keyword>Win32Proj</Keyword>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
")
        $string = $string.Replace("<PlatformToolset>v143</PlatformToolset>","<PlatformToolset>v141_xp</PlatformToolset>")
        $string = $string.Replace(
' Label="LocalAppDataPlatform" />
  ',' Label="LocalAppDataPlatform" />
    <Import Project="simh.props" />
  ')
        $string | Out-File -Force -FilePath "$file" -Encoding utf8
    }
}
