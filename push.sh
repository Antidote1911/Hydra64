#!/usr/bin/env bash
# Commits everything that changed and pushes it to GitHub (Antidote1911/Hydra64).
#
#   ./push.sh                  commits with a dated message, then pushes
#   ./push.sh "Mon message"    commits with this message, then pushes
#   ./push.sh --dry-run        only shows what would be committed, pushes nothing
#
# The first run creates the local git repository and links it to GitHub.
# What .gitignore lists (build/, build-win/, ...) is never sent.
set -euo pipefail
cd "$(dirname "$0")"

remote=https://github.com/Antidote1911/Hydra64.git

dry=0
message=
for arg in "$@"; do
    case "$arg" in
        -n|--dry-run) dry=1 ;;
        -h|--help)    sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "Option inconnue : $arg (voir ./push.sh --help)" >&2; exit 1 ;;
        *)  message=$arg ;;
    esac
done
[[ -n $message ]] || message="Mise à jour du $(date '+%d/%m/%Y %H:%M')"

if (( dry )); then
    if [[ -d .git ]]; then
        git status --short
    else
        echo "Pas encore de dépôt git : le premier ./push.sh le créera et enverra tout le projet."
    fi
    exit 0
fi

if [[ ! -d .git ]]; then
    echo "==> Création du dépôt git local"
    git init --quiet
fi
if ! git remote get-url origin >/dev/null 2>&1; then
    echo "==> Liaison avec $remote"
    git remote add origin "$remote"
fi

git add --all
if git diff --cached --quiet; then
    echo "==> Rien de nouveau à valider"
else
    echo "==> Commit : $message"
    git commit --quiet --message "$message"
fi

branch=$(git symbolic-ref --short HEAD)
echo "==> Envoi sur GitHub (branche $branch)"
git push --set-upstream origin "$branch"
echo "==> OK : https://github.com/Antidote1911/Hydra64"
