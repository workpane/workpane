originalMask=$(umask)
scope=$1
identity=$2
kind=$3
shift 3

case "$identity" in
    ''|*[!0-9]*) exit 1 ;;
esac

case "$kind" in
    pipe|terminal) ;;
    *) exit 1 ;;
esac

[ "${#identity}" -le 10 ] && [ "$identity" -ge 1 ] && [ "$identity" -le 2147483647 ] && [ "$#" -gt 0 ] || exit 1
directory="$scope/program.$identity"
[ -d "$directory" ] && [ ! -f "$scope/closed" ] && [ ! -f "$directory/closed" ] || exit 1
mkdir "$directory/claimed" 2>/dev/null || exit 1
trap ':' HUP INT TERM
umask 077
mkfifo "$directory/hold" || exit 1
IFS= read -r platform < "$scope/platform" || exit 1

case "$platform" in
    macos) session=$(/usr/bin/osascript -l JavaScript "$scope/session.js" session "$$") || exit 1 ;;
    linux) session=$(/bin/ps -p "$$" -o sid= | tr -d ' ') || exit 1 ;;
    *) exit 1 ;;
esac

case "$session" in
    ''|*[!0-9]*) exit 1 ;;
esac

[ "$session" -gt 1 ] || exit 1
printf '%s\n' "$kind" > "$directory/kind" || exit 1
printf '%s\n' "$session" > "$directory/session" || exit 1
[ -d "$directory" ] && [ ! -f "$scope/closed" ] && [ ! -f "$directory/closed" ] || exit 1
umask "$originalMask"
if [ "$kind" = terminal ]; then
    "$@"
else
    exec 3<&0
    "$@" <&3 3<&- &
    child=$!
    exec 3<&-
    wait "$child"
fi

code=$?
unsuccessful=0
[ "$code" -eq 0 ] || unsuccessful=1
printf '%s %s\n' "$code" "$unsuccessful" > "$directory/code.new" || exit 1
mv "$directory/code.new" "$directory/code" || exit 1

while :; do
    IFS= read -r release < "$directory/hold" || :
done
