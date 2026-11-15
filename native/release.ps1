# Publica uma release do OpenSpeak nativo de ponta a ponta: confere o repositório e o
# changelog, empacota (package.ps1), grava native/VERSION, faz o commit, cria a tag,
# envia e publica a release no GitHub com os instaladores e o SHA256SUMS.
#   .\release.ps1 1.0.47                 # pergunta antes de publicar
#   .\release.ps1 1.0.47 -DryRun         # compila e empacota, mostra o que publicaria e para
#   .\release.ps1 1.0.47 -Yes            # sem a pergunta final
#   .\release.ps1 1.1.0-beta.1           # sufixo na versão = pré-release
#   .\release.ps1 1.0.47 -AllTests       # suíte completa em vez dos testes afetados
#   .\release.ps1 1.0.47 -Full -Portable # também a edição completa e as portáteis (só se pedido)
# Antes, escreva no topo do native/CHANGELOG.md a seção "# OpenSpeak <versão> — <resumo>";
# ela vira o título e as notas da release e pode estar sem commit (entra no commit da versão).
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Version,
    [switch]$DryRun,
    [switch]$Yes,
    [switch]$AllTests,
    [switch]$Full,
    [switch]$Portable
)
# git e gh escrevem progresso no stderr; os erros são vistos pelo código de saída.
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
$tag = "v$Version"

function Fail([string]$Message) { Write-Host "release: $Message" -ForegroundColor Red; exit 1 }
function Invoke-Git {
    $out = & git -C $script:root @args
    if ($LASTEXITCODE) { Fail "git $($args -join ' ') falhou" }
    return $out
}

# ---------------------------------------------------------------- conferências
if ($Version -cnotmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z][0-9A-Za-z.-]*)?$') { Fail "versão inválida: $Version (use 1.2.3 ou 1.2.3-beta.1)" }
$prerelease = $Version.Contains('-')

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail 'git não encontrado no PATH' }
$script:root = (& git -C $here rev-parse --show-toplevel 2>$null)
if (-not $script:root) { Fail 'fora de um repositório git' }
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { Fail 'GitHub CLI (gh) não encontrado' }
& gh auth status *> $null
if ($LASTEXITCODE) { Fail 'gh sem login (gh auth login)' }

# main, em dia com a origin e limpa (só o changelog e a versão podem estar pendentes: entram
# no commit da versão). Roda de novo depois do empacotamento, que leva minutos.
function Assert-RepoState {
    $branch = Invoke-Git rev-parse --abbrev-ref HEAD
    if ($branch -ne 'main') { Fail "releases saem da main (atual: $branch)" }
    Invoke-Git fetch --quiet origin main | Out-Null
    $behind = [int](Invoke-Git rev-list --count HEAD..origin/main)
    if ($behind) { Fail "a main local está $behind commit(s) atrás da origin/main; rode git pull antes" }
    $allowed = @('native/CHANGELOG.md', 'native/VERSION')
    $dirty = @(Invoke-Git -c core.quotepath=off status --porcelain | ForEach-Object { $_.Substring(3).Trim('"') } | Where-Object { $allowed -notcontains $_ })
    if ($dirty) { Fail "alterações fora do changelog; faça commit ou guarde antes:`n  $($dirty -join "`n  ")" }
    return (Invoke-Git rev-parse HEAD)
}
$head = Assert-RepoState

# Precedência semver simplificada: núcleo numérico; sem sufixo > com sufixo; sufixos por partes.
function Compare-Version([string]$A, [string]$B) {
    $c = ([version]($A -replace '-.*$')).CompareTo([version]($B -replace '-.*$'))
    if ($c) { return $c }
    $pa = if ($A.Contains('-')) { $A.Substring($A.IndexOf('-') + 1) }
    $pb = if ($B.Contains('-')) { $B.Substring($B.IndexOf('-') + 1) }
    if (-not $pa -or -not $pb) { return [int][bool]$pb - [int][bool]$pa }
    $xa = $pa.Split('.'); $xb = $pb.Split('.')
    for ($i = 0; $i -lt [Math]::Min($xa.Count, $xb.Count); $i++) {
        $na = 0; $nb = 0
        $c = if ([int]::TryParse($xa[$i], [ref]$na) -and [int]::TryParse($xb[$i], [ref]$nb)) { $na.CompareTo($nb) } else { [string]::CompareOrdinal($xa[$i], $xb[$i]) }
        if ($c) { return [Math]::Sign($c) }
    }
    return $xa.Count.CompareTo($xb.Count)
}

$versionRegex = '^\d+\.\d+\.\d+(-[0-9A-Za-z][0-9A-Za-z.-]*)?$'
$current = (Get-Content -LiteralPath (Join-Path $here 'VERSION') -Raw -ErrorAction SilentlyContinue)
if (-not $current -or $current.Trim() -notmatch $versionRegex) { Fail 'native/VERSION ausente ou inválido' }
$current = $current.Trim()
# A nova versão tem de passar a VERSION e todas as tags do GitHub (o updater não faz downgrade).
$remoteTags = @(Invoke-Git ls-remote --tags origin | ForEach-Object { if ($_ -match 'refs/tags/v([^\^]+)$') { $Matches[1] } } | Sort-Object -Unique)
if ($remoteTags -contains $Version) { Fail "a tag $tag já existe no GitHub" }
& git -C $script:root rev-parse -q --verify "refs/tags/$tag" *> $null
if (-not $LASTEXITCODE) { Fail "a tag $tag já existe localmente (apague com git tag -d $tag se sobrou de uma tentativa)" }
& gh release view $tag -R JonasHenriqueDev/OpenSpeak *> $null
if (-not $LASTEXITCODE) { Fail "a release $tag já existe" }
foreach ($known in @($current) + @($remoteTags | Where-Object { $_ -match $versionRegex })) {
    if ((Compare-Version $Version $known) -le 0) { Fail "$Version não é maior que $known, já usada (VERSION ou tag do GitHub)" }
}

# Seção do changelog: do "# OpenSpeak <versão>" do topo até o próximo título "# ".
$changelog = @(Get-Content -LiteralPath (Join-Path $here 'CHANGELOG.md') -Encoding UTF8)
$start = [array]::FindIndex([string[]]$changelog, [Predicate[string]] { param($l) $l -like '# *' })
if ($start -lt 0 -or $changelog[$start] -notmatch '^# OpenSpeak (\S+)(?:\s+—\s+(.+))?$' -or $Matches[1] -ne $Version) {
    Fail "o topo do native/CHANGELOG.md precisa ser '# OpenSpeak $Version — <resumo>'"
}
$title = $changelog[$start].Substring(2).Trim()
$summary = $Matches[2]
# O PowerShell 5.1 não escapa aspas duplas ao passar argumentos ao git e ao gh.
if ($title.Contains('"')) { Fail 'o título do changelog não pode ter aspas duplas (use aspas simples ou “ ”)' }
$end = $start + 1
while ($end -lt $changelog.Count -and $changelog[$end] -notlike '# *') { $end++ }
$notes = if ($end -gt $start + 1) { (($changelog[($start + 1)..($end - 1)]) -join "`n").Trim() }
if (-not $notes) { Fail "a seção $Version do changelog está vazia" }

# ---------------------------------------------------------------- pacote
# O package.ps1 usa throw/Write-Error com Stop: o erro chega aqui como exceção, não como código.
try {
    & (Join-Path $here 'package.ps1') -Version $Version -AllTests:$AllTests -Full:$Full -Portable:$Portable
    $packaged = -not $LASTEXITCODE
} catch { Write-Host $_; $packaged = $false }
if (-not $packaged) { Fail 'package.ps1 falhou (build, testes ou instalador)' }

$editions = @('OpenSpeak') + @(if ($Full) { 'OpenSpeak-Full' })
$assets = foreach ($prefix in $editions) {
    "$prefix-Setup-$Version.exe"; "$prefix-Setup.exe"
    if ($Portable) { "$prefix-$Version-portable.zip"; "$prefix-Portable-$Version.exe" }
}
$assets = @($assets) + "SHA256SUMS-$Version.txt"
$dist = Join-Path $here 'dist'
foreach ($a in $assets) { if (-not (Test-Path -LiteralPath (Join-Path $dist $a))) { Fail "faltou dist\$a" } }

# O updater recusa o instalador se o hash não bater: confere de novo antes de enviar,
# e exige uma linha para cada arquivo publicado (nem a mais, nem a menos).
$covered = @()
foreach ($line in [System.IO.File]::ReadAllLines((Join-Path $dist "SHA256SUMS-$Version.txt"))) {
    if (-not $line) { continue }
    if ($line -cnotmatch '^([0-9a-f]{64}) \*(.+)$') { Fail "linha inválida no SHA256SUMS: $line" }
    $name = $Matches[2]; $expected = $Matches[1]
    if ($assets -notcontains $name -or $name -like 'SHA256SUMS-*') { Fail "o SHA256SUMS cita $name, que não será publicado" }
    $actual = (Get-FileHash -LiteralPath (Join-Path $dist $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -cne $expected) { Fail "hash de $name não confere com o SHA256SUMS" }
    $covered += $name
}
$missing = @($assets | Where-Object { $_ -notlike 'SHA256SUMS-*' -and $covered -notcontains $_ })
if ($missing) { Fail "o SHA256SUMS não cobre: $($missing -join ', ')" }

Write-Host ''
Write-Host "Release $tag$(if ($prerelease) { ' (pré-release)' })" -ForegroundColor Cyan
Write-Host "  Título: $title"
foreach ($a in $assets) { Write-Host ("  {0,-34} {1,8:N1} MB" -f $a, ((Get-Item -LiteralPath (Join-Path $dist $a)).Length / 1MB)) }
Write-Host '  Notas:'
$notes -split "`n" | ForEach-Object { Write-Host "    $_" }
Write-Host ''

if ($DryRun) { Write-Host 'DryRun: nada foi gravado, commitado nem publicado.'; exit 0 }
if (-not $Yes) {
    $answer = Read-Host "Gravar a versão, criar a tag $tag e publicar? (s/N)"
    if ($answer -notmatch '^[sS]') { Write-Host 'Cancelado; o pacote ficou em dist\.'; exit 1 }
}

# ---------------------------------------------------------------- publicação
# O pacote saiu do HEAD conferido no começo; se algo mudou nesses minutos, não publica.
if ((Assert-RepoState) -ne $head) { Fail 'a main mudou durante o empacotamento; rode o release.ps1 de novo' }

# Até o push, qualquer falha desfaz o que foi feito aqui (VERSION, commit, tag).
$versionPath = Join-Path $here 'VERSION'
$versionBefore = [System.IO.File]::ReadAllText($versionPath)
$script:committed = $false; $script:tagged = $false
function Undo-Local([string]$Message) {
    if ($script:tagged) { & git -C $script:root tag -d $tag *> $null }
    if ($script:committed) { & git -C $script:root reset --quiet --soft HEAD~1 *> $null }
    & git -C $script:root restore --staged -- native/VERSION native/CHANGELOG.md *> $null
    [System.IO.File]::WriteAllText($versionPath, $versionBefore, [System.Text.UTF8Encoding]::new($false))
    Fail "$Message; nada foi publicado e o commit/tag locais foram desfeitos"
}
[System.IO.File]::WriteAllText($versionPath, "$Version`n", [System.Text.UTF8Encoding]::new($false))
& git -C $script:root add -- native/VERSION native/CHANGELOG.md
if ($LASTEXITCODE) { Undo-Local 'git add falhou' }
& git -C $script:root diff --cached --quiet
if ($LASTEXITCODE) {
    $message = if ($summary) { "chore(native): versão $Version ($summary)" } else { "chore(native): versão $Version" }
    & git -C $script:root commit --quiet -m $message
    if ($LASTEXITCODE) { Undo-Local 'git commit falhou' }
    $script:committed = $true
}
& git -C $script:root tag $tag
if ($LASTEXITCODE) { Undo-Local 'git tag falhou' }
$script:tagged = $true
# Atômico: a main e a tag sobem juntas ou nenhuma sobe.
& git -C $script:root push --atomic --quiet origin main "refs/tags/$tag"
if ($LASTEXITCODE) { Undo-Local 'o push foi recusado (a origin andou? rode git pull e tente de novo)' }

$notesFile = Join-Path ([System.IO.Path]::GetTempPath()) "openspeak-notes-$Version.md"
[System.IO.File]::WriteAllText($notesFile, "$notes`n", [System.Text.UTF8Encoding]::new($false))
$files = @($assets | ForEach-Object { Join-Path $dist $_ })
$channel = if ($prerelease) { '--prerelease' } else { '--latest' }
& gh release create $tag @files -R JonasHenriqueDev/OpenSpeak --verify-tag --title $title --notes-file $notesFile $channel
if ($LASTEXITCODE) {
    $quoted = ($files | ForEach-Object { "`"$_`"" }) -join ' '
    Fail ("o commit e a tag $tag já estão no GitHub, mas a release falhou. Veja se ela ficou criada pela metade: gh release view $tag -R JonasHenriqueDev/OpenSpeak`n" +
        "  se existe, complete os arquivos: gh release upload $tag $quoted -R JonasHenriqueDev/OpenSpeak --clobber`n" +
        "  se não existe, crie:             gh release create $tag $quoted -R JonasHenriqueDev/OpenSpeak --verify-tag --title `"$title`" --notes-file `"$notesFile`" $channel")
}
Remove-Item -LiteralPath $notesFile -ErrorAction SilentlyContinue
Write-Host "Publicada: https://github.com/JonasHenriqueDev/OpenSpeak/releases/tag/$tag" -ForegroundColor Green
exit 0
