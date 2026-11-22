#!/bin/sh
# Install the pre-commit hook that runs tools/check_release.sh before every commit.
cd "$(git rev-parse --show-toplevel)" || exit 1
printf '#!/bin/sh\nexec tools/check_release.sh\n' > .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
echo "installed .git/hooks/pre-commit"
