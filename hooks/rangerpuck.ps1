# ============================================================================
#  rangerpuck.ps1 — drive the RangerPuck from Claude Code on the MSI (Windows).
#
#  Native PowerShell: NO jq, curl, bash or Git Bash needed — Invoke-RestMethod
#  POSTs straight to the board. MUST always exit 0; a broken puck never breaks
#  the session it reports on. Mirrors ~/esp32-projects/1-ranger-puck/hooks/
#  rangerpuck.sh (the Mac/Linux hook), tagged as WHO=MSI (violet on the board).
#
#  Wired via settings.json, e.g.:
#    powershell -NoProfile -ExecutionPolicy Bypass -File <this> PreToolUse
#  Claude Code pipes the event's JSON payload on stdin.
# ============================================================================
param([string]$HookEvent = "")
$ErrorActionPreference = "SilentlyContinue"
$WHO = "MSI"

$raw = ""
try { $raw = [Console]::In.ReadToEnd() } catch {}
$p = $null
if ($raw) { try { $p = $raw | ConvertFrom-Json } catch {} }

function Squash([string]$s, [int]$n = 18) {
  if (-not $s) { return "" }
  $s = ($s -replace '\s+', ' ').Trim()
  if ($s.Length -gt $n) { $s = $s.Substring(0, $n) }
  return $s
}
function DetailFor($tool, $inp) {
  switch ($tool) {
    "Bash"      { $c = "$($inp.command)" -replace '^\s*sudo\s+',''; return (Squash ((($c -split '\s+') | Select-Object -First 3) -join ' ')) }
    "WebSearch" { return (Squash "$($inp.query)") }
    "WebFetch"  { $u = "$($inp.url)" -replace '^[a-zA-Z]+://',''; return (Squash (($u -split '/')[0])) }
    "Read"      { return (Squash (Split-Path "$($inp.file_path)" -Leaf)) }
    "Edit"      { return (Squash (Split-Path "$($inp.file_path)" -Leaf)) }
    "Write"     { return (Squash (Split-Path "$($inp.file_path)" -Leaf)) }
    "Grep"      { return (Squash "$($inp.pattern)") }
    "Glob"      { return (Squash "$($inp.pattern)") }
    "Task"      { return (Squash "$($inp.description)") }
    default     { return (Squash "$tool") }
  }
}

$state = "IDLE"; $l1 = "ready"; $l2 = ""
switch ($HookEvent) {
  "UserPromptSubmit" { $state="THINKING"; $l1 = Squash "$($p.prompt)"; if (-not $l1) { $l1 = "working" } }
  "PreToolUse"       { $t = "$($p.tool_name)"; $state="RUNNING"; $l1 = DetailFor $t $p.tool_input; $l2 = Squash $t }
  "Stop"             { $state="DONE"; $l1="done" }
  "SessionStart"     { $state="IDLE"; $l1="ready" }
  "Notification"     {
    $m = "$($p.message)"
    if     ($m -match "permission|approve|confirm|Approve") { $state="APPROVE"; $l1="needs a yes" }
    elseif ($m -match "waiting|idle")                       { $state="WAITING"; $l1="your move" }
    else                                                    { $state="APPROVE"; $l1="needs a yes" }
  }
}

# find the board: cached IP first (mDNS is unreliable across subnets), then the name
$cache = Join-Path $env:USERPROFILE ".ranger-memory\config\rangerpuck.ip"
$targets = @()
if (Test-Path $cache) { $targets += (Get-Content $cache -Raw).Trim() }
$targets += "rangerpuck.local"

$body = @{ state=$state; line1=$l1; line2=$l2; who=$WHO } | ConvertTo-Json -Compress
foreach ($h in $targets) {
  if (-not $h) { continue }
  try {
    Invoke-RestMethod -Uri "http://$h/state" -Method Post -Body $body -ContentType "application/json" -TimeoutSec 2 | Out-Null
    break
  } catch {}
}
exit 0
