#!/usr/bin/env bash
# 공식 최신 릴리스에 hooks.patch 가 그대로 들어가는지 시험하고, 새 버전이면 결과를 이슈로 남긴다.
# GitHub Actions 에서 실행 (GH_TOKEN, GITHUB_REPOSITORY 필요). 사용: check_upstream.sh
set -u
KIT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d)"

# 폴더 이름, 공식 저장소, 기준 버전(이 자료를 만든 버전)
EMUS=(
  "mgba mgba-emu/mgba 0.10.5"
  "melonds melonDS-emu/melonDS 1.1"
  "azahar azahar-emu/azahar 2126.2"
)

for entry in "${EMUS[@]}"; do
  read -r name repo base <<< "$entry"
  tag=$(gh api "repos/$repo/releases/latest" --jq .tag_name 2>/dev/null)
  if [ -z "$tag" ]; then
    echo "$name: 최신 릴리스를 읽지 못함"
    continue
  fi
  if [ "$tag" = "$base" ]; then
    echo "$name: 최신 릴리스가 기준 버전과 같음 ($tag)"
    continue
  fi
  title="[upstream] $name $tag"
  if gh issue list --repo "$GITHUB_REPOSITORY" --state all --search "\"$title\" in:title" --json title --jq '.[].title' | grep -Fxq "$title"; then
    echo "$name: $tag 은 이미 이슈가 있음"
    continue
  fi

  src="$WORK/$name"
  if ! git clone -q --depth 1 --branch "$tag" "https://github.com/$repo.git" "$src" 2>"$WORK/$name.clone.log"; then
    echo "$name: $tag 소스를 받지 못함"
    continue
  fi

  ok=()
  bad=()
  for p in "$KIT/$name"/hooks/*.patch; do
    target=$(sed -n 's#^+++ b/##p' "$p" | head -1)
    if git -C "$src" apply --check "$p" 2>/dev/null; then
      ok+=("$target")
    else
      bad+=("$target")
    fi
  done

  {
    echo "공식 [$repo]($(printf 'https://github.com/%s/releases/tag/%s' "$repo" "$tag")) 의 새 릴리스 **$tag** (이 자료의 기준: $base)"
    echo
    if [ ${#bad[@]} -eq 0 ]; then
      echo "결과: \`$name/hooks.patch\` 가 **그대로 적용됨** (원본 파일 ${#ok[@]}개)."
      echo
      echo "빌드와 소리는 확인하지 않았습니다. README 의 프롬프트로 AI 에게 적용·빌드를 맡기고 게임으로 확인하세요."
    else
      echo "결과: 원본 파일 ${#bad[@]}개에서 **충돌** (그대로 적용 ${#ok[@]}개)."
      echo
      echo "충돌한 파일 (\`$name/PORTING-AI.md\` 의 '끼우는 지점' 표에서 같은 파일의 '역할'을 보고 옮기세요):"
      for f in "${bad[@]}"; do echo "- \`$f\`"; done
    fi
    echo
    echo "_이 이슈는 GitHub Actions 가 자동으로 만들었습니다. 저장소는 관리되지 않습니다._"
  } > "$WORK/$name.md"

  gh issue create --repo "$GITHUB_REPOSITORY" --title "$title" --body-file "$WORK/$name.md"
  echo "$name: $tag 결과를 이슈로 남김 (충돌 ${#bad[@]}개)"
done
