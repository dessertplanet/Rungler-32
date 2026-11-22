#!/bin/sh
# Pre-commit check: fails if a file that git would commit is build output or another denied file type,
# or contains an absolute local path or a private IP address. Extra local patterns, one extended regex
# per line, can be kept in .git/release-denylist (inside .git, so never committed).
# Installed as .git/hooks/pre-commit by tools/install_hooks.sh.
cd "$(git rev-parse --show-toplevel)" || exit 1
DENY='/Users/|/home/[a-z]|C:\\Users|192\.168\.[0-9]|10\.[0-9]+\.[0-9]+\.[0-9]+'
if [ -f .git/release-denylist ]; then
    DENY="$DENY|$(grep -v '^[[:space:]]*$' .git/release-denylist | paste -s -d '|' -)"
fi
BADTYPES='\.(syx|bin|out|obj|map|hex|elf|pdf|zip|jsonl)$'
files=$(git ls-files -c -o --exclude-standard | grep -v -E '^(LICENSE|tools/check_release\.sh)$')
status=0
bad=$(printf '%s\n' "$files" | grep -i -E "$BADTYPES")
if [ -n "$bad" ]; then echo "denied file types:"; printf '  %s\n' $bad; status=1; fi
for f in $files; do
    [ -f "$f" ] || continue
    if hits=$(grep -n -i -E "$DENY" "$f"); then echo "$f:"; printf '%s\n' "$hits" | sed 's/^/  /'; status=1; fi
done
[ $status = 0 ] && echo "check_release: clean ($(printf '%s\n' "$files" | wc -l | tr -d ' ') files)"
exit $status
