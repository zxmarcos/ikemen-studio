param([string]$Ref = "upstream/develop")
# Merges the engine's upstream into the current branch; build and test after it (build.ps1).
# Upstream's workflows and dependabot config are for its repository (its tokens, its releases):
# they are taken out of every merge, whether upstream changed or added them; the fork's own two
# stay. A conflict anywhere
# else stops here, with the merge left open: resolve it, git add, git commit. rerere remembers.
# Exit code: 0 merged or nothing new, 1 conflicts left.
Set-Location (Resolve-Path "$PSScriptRoot\..\..\..").Path
git fetch upstream
git merge --no-commit --no-ff --no-edit $Ref
if (-not (git rev-parse -q --verify MERGE_HEAD)) { "Nothing to merge."; exit 0 }
$theirs = git ls-files .github/workflows .github/dependabot.yml | Select-Object -Unique |
    Where-Object { $_ -notin ".github/workflows/upstream-check.yml", ".github/workflows/build.yml" }
if ($theirs) { git rm -q -f --ignore-unmatch -- $theirs }
$left = git diff --name-only --diff-filter=U
if ($left) { "Conflicts to resolve:"; $left; exit 1 }
git commit -q --no-edit
"Merged $Ref."
exit 0
