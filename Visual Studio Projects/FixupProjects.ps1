# This script converts Visual Studio 2019, 2022 and 2026 upgraded VS2008 projects to 
# produce VS2019 or VS2022 or VS2026 projects that will build executables that will 
# leverate the Visual Studio 2017 tool chain which is stable in the windows_build repo.
#
# Additionally, it will also convert to projects in an attempt to produce reproducible
# executables when the same input files are processed by the same tool chain on 
# potentially different hosts.
#
param(
    [string]$Solution,
    [switch]$Convert
    )
$changedProjects = 0
$processedProjects = 0
$changedSolution = 0
[string]$uncommittedChanges = ''
$reproduciblePropsFile = 'Directory.Build.props'
$SDK = $env:WindowsSDKVersion
$SDK = $SDK.Replace("\","")
$solutionFile = $Solution
$solutionPath = Split-Path -Path $solutionFile -Parent
if (-not $solutionPath.Contains('\'))
{
    $solutionPath = "."
}
if (-not (Test-Path -Path $solutionFile -PathType Any)) {if (-not (Get-Item -Path $solutionFile -ErrorAction Ignore)) {Write-Host "No such file: $solutionFile"; }}
$solution = Get-Content -Path $solutionFile -Raw
$startingSolution = $Solution
if ($solution -match 'Project\("{([0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12})}"\) = "BuildROMs", "BuildROMs.[a-z]*", "{([0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12})}"')
{
    $BuildROMsGUID = $($Matches[2])
}
$Projects = [Regex]::Matches($solution, '(?sm)}"\) = "(?<project>.*?)", "(?<file>.*?)"')
if ($solution.Contains(".vcproj"))
{
    Write-Host "Attempting to fix Solution file which wasn't completely migrated:"
    Write-Host $solutionFile
}
$dependencyPattern = "(?sm)\s\sProjectSection\(ProjectDependencies\) \= postProject.*?({.*?}).*?EndProjectSection"
$solution = $solution -replace $dependencyPattern, ""
$solution = $solution -replace ".vcproj", ".vcxproj"

if (-not $solution.Contains("GlobalSection(ExtensibilityGlobals)"))
{
    $solution = $solution.Replace(
"EndGlobal
",
"	GlobalSection(ExtensibilityGlobals) = postSolution
		SolutionGuid = {$(New-Guid)}
	EndGlobalSection
EndGlobal
")

    $solution = $solution.Replace("Format Version 10.00", "Format Version 12.00")
    $solution = $solution.Replace("Visual C++ Express 2008", "Visual Studio Version 17
VisualStudioVersion = 17.14.36705.20 d17.14
MinimumVisualStudioVersion = 10.0.40219.1")
}
$reproduciblePropsFile = $solutionPath + '\' + $reproduciblePropsFile
$uncommittedChanges = $(git update-index --refresh --)
if ($uncommittedChanges -ne '')
{
    if (Test-Path -Path $reproduciblePropsFile)
    {
        Remove-Item -Path $reproduciblePropsFile
    }
}
else
{
    Set-Content -Path $reproduciblePropsFile -Value '<Project>
  <ItemDefinitionGroup Condition="''$(Configuration)''==''Release''">
    <ClCompile>
      <!-- Avoid compile time info in compiled results -->
      <AdditionalOptions>/experimental:deterministic %(AdditionalOptions)</AdditionalOptions>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup>
    <Link>
      <AdditionalOptions>/fixed:no  /Brepro /PDBALTPATH:%_PDB% /INCREMENTAL:NO</AdditionalOptions>
    </Link>
  </ItemDefinitionGroup>
</Project>
'
}
ForEach ($Project in $Projects)
{
    $processedProjects = $processedProjects + 1
    $projFile = $solutionPath + '\' + $($Project.Groups['file'].Value).Replace(".vcproj", ".vcxproj")
    if (-not (Test-Path -Path $ProjFile -PathType Any)) {if (-not (Get-Item -Path $ProjFile -ErrorAction Ignore)) {Write-Host "No such file: $ProjFile"; continue; }}
    $projString  =  Get-Content -Path $projFile -Raw
    $startingProjString = $projString
    if ($Convert)
    {
        if ($projString.Contains("<WindowsTargetPlatformVersion>")) {Write-Host "$projFile - already converted"; continue; }
        $projString = $projString.Replace(
"<Keyword>Win32Proj</Keyword>
", 
"<Keyword>Win32Proj</Keyword> 
    <WindowsTargetPlatformVersion>$SDK</WindowsTargetPlatformVersion>
")
        $projString = $projString.Replace("<PlatformToolset>v143</PlatformToolset>","<PlatformToolset>v141</PlatformToolset>")
        $projString = $projString.Replace("<PlatformToolset>v144</PlatformToolset>","<PlatformToolset>v141</PlatformToolset>")
        $projString = $projString.Replace("<PlatformToolset>v145</PlatformToolset>","<PlatformToolset>v141</PlatformToolset>")
        $projString = $projString.Replace(
' Label="LocalAppDataPlatform" />
  ',' Label="LocalAppDataPlatform" />
    <Import Project="simh.props" />
  ')
    }
    if ((-not $projString.Contains($BuildROMsGUID)) -and (-not $projString.Contains($BuildROMsGUID.ToLower())))
    {
        $ProjString = $ProjString.Replace(
'  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />',
('  <ItemGroup>
    <ProjectReference Include="BuildROMs.vcxproj">
      <Project>{' + $BuildROMsGUID.ToLower() + '}</Project>
      <ReferenceOutputAssembly>false</ReferenceOutputAssembly>
    </ProjectReference>
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />'))
    }
    if (-not ($projString -ceq $startingProjString))
    {
        $projString | Out-File -Force -FilePath "$projFile" -Encoding utf8
        $changedProjects = $changedProjects + 1
    }
}
if (-not ($solution -ceq $startingSolution))
{
    $solution | Out-File -Force -FilePath "$solutionFile" -Encoding utf8
    $changedSolution = 1
}
if ($uncommittedChanges -eq '')
{
    Write-Host "Reproducible Builds Enabled"
}
else
{
    Write-Host "Non Reproducible Builds Enabled"
}
Write-Host "Projects Processed: $processedProjects, Projects Changed: $changedProjects,  Solution Changed: $changedSolution"
