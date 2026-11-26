#!/bin/bash
TOKEN="$(cat /tmp/ghtoken)"
ACC=ys3027289298-creator
REPO=mini-shadow-lab
SRC=/mnt/c/Users/30272/Documents/deepseek-harness/default-workspace/selfmade18/mini-shadow-lab

tmp=$(mktemp -d)
cp -r "$SRC/." "$tmp/"
cd "$tmp" || exit 1
rm -f _b.sh
for n in shadowmap cascade pcf; do find . -type f -name "$n" -delete; done

git init -q -b main
git -c user.email=s@l -c user.name=s add -A
git -c user.email=s@l -c user.name=s commit -qm "mini-shadow-lab: shadowmap/cascade/pcf" >/dev/null
SHA=$(git rev-parse HEAD)
echo "sha=$SHA"

CODE=$(curl -s -o /tmp/mk.json -w "%{http_code}" -X POST \
  -H "Authorization: Bearer $TOKEN" -H "Accept: application/vnd.github+json" \
  https://api.github.com/user/repos -d "{\"name\":\"$REPO\",\"private\":false}")
echo "create=$CODE"

OK=0
for i in 1 2 3; do
  if git -c http.version=HTTP/1.1 push --force "https://x-access-token:$TOKEN@github.com/$ACC/$REPO.git" HEAD:refs/heads/main >/tmp/push.log 2>&1; then
    OK=1; break
  fi
  sleep 5
done
echo "push=$OK"