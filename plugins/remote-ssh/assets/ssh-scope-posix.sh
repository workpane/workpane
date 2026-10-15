umask 077
trap '' PIPE
lease=$1
sessionSource=$2

case "$lease" in
    ''|*[!0-9]*) exit 1 ;;
esac

[ "$lease" -ge 1 ] && [ "$lease" -le 120 ] || exit 1

case "$(uname -s)" in
    Darwin) platform=macos ;;
    Linux) platform=linux; sessionSource= ;;
    *) printf 'PLATFORM unsupported\n'; exit 1 ;;
esac

printf 'PLATFORM %s\n' "$platform"

scope=$(mktemp -d) || exit 1
canonical=$(cd "$scope" && pwd -P)

if [ "$?" -ne 0 ]; then
    rm -rf "$scope"
    exit 1
fi

scope=$canonical
closing=0
watcher=
highest=0
active=0
printf '0\n' > "$scope/heartbeat"
printf '%s\n' "$platform" > "$scope/platform"
printf '%s' "$sessionSource" > "$scope/session.js"

signal_program() {
    directory=$1
    signal=$2
    [ -f "$directory/session" ] || return 0
    IFS= read -r process < "$directory/session" || return 1
    IFS= read -r programKind < "$directory/kind" || return 1

    case "$programKind" in
        pipe|terminal) ;;
        *) return 1 ;;
    esac

    if [ "$signal" -eq 15 ] && [ "$programKind" = terminal ]; then
        signal=1
    fi

    case "$process" in
        ''|*[!0-9]*) return 1 ;;
    esac

    [ "$process" -gt 1 ] || return 1

    if [ "$platform" = macos ]; then
        /bin/ps -e -o pid= | /usr/bin/osascript -l JavaScript "$scope/session.js" signal "$process" "$signal" >/dev/null || return 1
        return 0
    fi

    if [ "$signal" -ne 9 ]; then
        kill -STOP "$process" 2>/dev/null || :
    fi

    pkill -"$signal" -s "$process" 2>/dev/null
    result=$?
    [ "$result" -eq 0 ] || [ "$result" -eq 1 ]
}

find_program() {
    program=$1
    shift
    found=

    case "$program" in
        /*)
            [ -f "$program" ] && [ -x "$program" ] && found=$program
            return
            ;;
        */*) return ;;
    esac

    remaining=$PATH

    while :; do
        directory=${remaining%%:*}
        [ -n "$directory" ] || directory=.
        candidate="$directory/$program"

        if [ -f "$candidate" ] && [ -x "$candidate" ]; then
            case "$candidate" in
                /*) found=$candidate ;;
                *) found="$PWD/$candidate" ;;
            esac

            return
        fi

        case "$remaining" in
            *:*) remaining=${remaining#*:} ;;
            *) break ;;
        esac
    done

    for encoded in "$@"; do
        if [ "$platform" = macos ]; then
            decoded=$(printf '%s' "$encoded" | /usr/bin/base64 -D; printf '.')
        else
            decoded=$(printf '%s' "$encoded" | base64 -d; printf '.')
        fi

        directory=${decoded%.}
        candidate="$directory/$program"

        if [ -f "$candidate" ] && [ -x "$candidate" ]; then
            found=$candidate
            return
        fi
    done
}

cleanup() {
    [ "$closing" -eq 0 ] || return
    closing=1
    trap '' HUP INT TERM
    : > "$scope/closed"

    for directory in "$scope"/program.*; do
        [ -d "$directory" ] || continue
        : > "$directory/closed"
        signal_program "$directory" 15 || :
    done

    sleep 0.1

    for directory in "$scope"/program.*; do
        [ -d "$directory" ] || continue
        signal_program "$directory" 9 || :
    done

    if [ -n "$watcher" ]; then
        kill "$watcher" 2>/dev/null || :
        wait "$watcher" 2>/dev/null || :
    fi

    for declaration in "$scope"/stage.*; do
        [ -f "$declaration" ] || continue
        encoded=$(cat "$declaration") || continue

        if [ "$platform" = macos ]; then
            decoded=$(printf '%s' "$encoded" | /usr/bin/base64 -D; printf '.')
        else
            decoded=$(printf '%s' "$encoded" | base64 -d; printf '.')
        fi

        rm -rf "${decoded%.}"
    done

    rm -rf "$scope"
}

trap 'cleanup; exit 1' HUP INT TERM
trap cleanup EXIT

(
    trap - EXIT HUP INT TERM
    last=0
    missed=0

    while [ -d "$scope" ] && [ ! -f "$scope/closed" ]; do
        sleep 1

        for directory in "$scope"/program.*; do
            [ -f "$directory/code" ] && [ ! -f "$directory/notified" ] || continue
            IFS=' ' read -r code unsuccessful < "$directory/code" || continue
            identity=${directory##*/program.}
            : > "$directory/notified" || continue
            printf 'DONE %s %s %s\n' "$identity" "$code" "$unsuccessful"
        done

        current=$(cat "$scope/heartbeat" 2>/dev/null) || exit

        if [ "$current" != "$last" ]; then
            missed=0
            last=$current
        else
            missed=$((missed + 1))
        fi

        if [ "$missed" -ge "$lease" ]; then
            cleanup
            trap - TERM
            kill -TERM 0
            exit
        fi
    done
) &
watcher=$!
printf 'SCOPE %s %s\n' "$scope" "$$"
heartbeat=0

while IFS=' ' read -r method identity extra; do
    if [ "$method" != info ] && [ "$method" != stage ] && [ "$method" != find ]; then
        [ -z "$extra" ] || exit 1
    fi

    case "$method" in
        stage|unstage)
            case "$identity" in
                ''|*[!0-9]*) exit 1 ;;
            esac

            declaration="$scope/stage.$identity"

            if [ "$method" = stage ]; then
                case "$extra" in
                    ''|*[!A-Za-z0-9+/=]*) exit 1 ;;
                esac

                [ "${#extra}" -le 5464 ] && [ ! -e "$declaration" ] || exit 1
                count=0

                for entry in "$scope"/stage.*; do
                    [ -f "$entry" ] && count=$((count + 1))
                done

                [ "$count" -lt 32 ] || exit 1
                encoded=$extra
            else
                [ -f "$declaration" ] || exit 1
                encoded=$(cat "$declaration") || exit 1
            fi

            if [ "$platform" = macos ]; then
                decoded=$(printf '%s' "$encoded" | /usr/bin/base64 -D; printf '.')
            else
                decoded=$(printf '%s' "$encoded" | base64 -d; printf '.')
            fi

            path=${decoded%.}

            if [ "$method" = stage ]; then
                trap '' HUP INT TERM

                if ! mkdir -m 700 "$path" 2>/dev/null; then
                    trap 'cleanup; exit 1' HUP INT TERM
                    printf 'ERROR %s remote_file_write_failed\n' "$identity"
                    continue
                fi

                if ! printf '%s' "$encoded" > "$declaration"; then
                    rm -rf "$path" || exit 1
                    rm -f "$declaration" || exit 1
                    trap 'cleanup; exit 1' HUP INT TERM
                    printf 'ERROR %s remote_file_write_failed\n' "$identity"
                    continue
                fi

                trap 'cleanup; exit 1' HUP INT TERM
                printf 'STAGED %s\n' "$identity"
            else
                rm -rf "$path" || exit 1
                rm "$declaration" || exit 1
                printf 'UNSTAGED %s\n' "$identity"
            fi

            ;;
        info)
            case "$identity" in
                ''|*[!0-9]*) exit 1 ;;
            esac

            case "$extra" in
                ''|*[!A-Za-z0-9+/=]*) exit 1 ;;
            esac

            [ "${#identity}" -le 10 ] && [ "${#extra}" -le 5464 ] || exit 1

            if [ "$platform" = macos ]; then
                decoded=$(printf '%s' "$extra" | /usr/bin/base64 -D; printf '.')
            else
                decoded=$(printf '%s' "$extra" | base64 -d; printf '.')
            fi

            path=${decoded%.}

            if [ ! -e "$path" ]; then
                printf 'ERROR %s remote_path_unavailable\n' "$identity"
                continue
            fi

            if [ "$platform" = macos ]; then
                attributes=$(LC_ALL=C /usr/bin/stat -L -f '%z %m %.9Fm' -- "$path" 2>/dev/null)
            else
                attributes=$(LC_ALL=C stat -L -c '%s %Y %y' -- "$path" 2>/dev/null)
            fi

            if [ "$?" -ne 0 ]; then
                printf 'ERROR %s remote_path_inspection_failed\n' "$identity"
                continue
            fi

            IFS=' ' read -r size epoch stamp <<EOF
$attributes
EOF
            stamp=$(printf '%s' "$stamp" | tr ' ' '_')
            now=$(date +%s) || exit 1
            recent=0
            readable=0
            writable=0
            kind=other
            [ "$epoch" -ge "$((now - 2))" ] && recent=1
            [ -r "$path" ] && readable=1
            [ -w "$path" ] && writable=1
            [ -d "$path" ] && kind=directory
            [ -f "$path" ] && kind=file
            [ "$kind" = file ] || size=0
            printf 'INFO %s %s %s %s %s %s %s\n' "$identity" "$kind" "$size" "$stamp" "$recent" "$readable" "$writable"
            ;;
        find)
            case "$identity" in
                ''|*[!0-9]*) exit 1 ;;
            esac

            case "$extra" in
                ''|*[!A-Za-z0-9+/=\ ]*) exit 1 ;;
            esac

            [ "${#extra}" -le 16300 ] || exit 1
            set -- $extra
            [ "$#" -le 257 ] || exit 1
            encoded=$1
            shift

            if [ "$platform" = macos ]; then
                decoded=$(printf '%s' "$encoded" | /usr/bin/base64 -D; printf '.')
            else
                decoded=$(printf '%s' "$encoded" | base64 -d; printf '.')
            fi

            find_program "${decoded%.}" "$@"

            if [ -z "$found" ]; then
                printf 'ABSENT %s\n' "$identity"
            else
                encoded=$(printf '%s' "$found" | base64 | tr -d '\r\n') || exit 1
                printf 'FOUND %s %s\n' "$identity" "$encoded"
            fi

            ;;
        account)
            [ -z "$identity" ] || exit 1

            if [ "$platform" = macos ]; then
                record=$(/usr/bin/id -P) || exit 1
                IFS=':' read -r account password uid gid class change expire description accountHome shell <<EOF
$record
EOF
            else
                uid=$(id -u) || exit 1
                record=$(getent passwd "$uid") || exit 1
                IFS=':' read -r account password uid gid description accountHome shell <<EOF
$record
EOF
            fi

            [ -n "$account" ] && [ -n "$accountHome" ] && [ -n "$shell" ] || exit 1
            account=$(printf '%s' "$account" | base64 | tr -d '\r\n') || exit 1
            encodedHome=$(printf '%s' "$accountHome" | base64 | tr -d '\r\n') || exit 1
            shell=$(printf '%s' "$shell" | base64 | tr -d '\r\n') || exit 1
            printf 'ACCOUNT %s %s %s\n' "$account" "$encodedHome" "$shell"
            ;;
        ping)
            [ -z "$identity" ] || exit 1
            heartbeat=$((heartbeat + 1))
            printf '%s\n' "$heartbeat" > "$scope/heartbeat.new"
            mv "$scope/heartbeat.new" "$scope/heartbeat"
            printf 'PONG\n'
            ;;
        open|stop)
            case "$identity" in
                ''|*[!0-9]*) exit 1 ;;
            esac

            [ "${#identity}" -le 10 ] && [ "$identity" -ge 1 ] && [ "$identity" -le 2147483647 ] || exit 1
            directory="$scope/program.$identity"

            if [ "$method" = open ]; then
                if [ "$identity" -le "$highest" ] || [ "$active" -ge 32 ]; then
                    printf 'REFUSED %s\n' "$identity"
                    continue
                fi

                highest=$identity
                mkdir "$directory" || exit 1
                active=$((active + 1))
                printf 'OPEN %s\n' "$identity"
                continue
            fi

            if [ "$identity" -gt "$highest" ]; then
                highest=$identity
            fi

            if [ -d "$directory" ]; then
                : > "$directory/closed"

                if [ -f "$directory/session" ]; then
                    signal_program "$directory" 15 || exit 1
                    sleep 0.1
                    signal_program "$directory" 9 || exit 1
                fi

                rm -rf "$directory"
                active=$((active - 1))
            fi

            printf 'STOPPED %s\n' "$identity"
            ;;
        quit)
            [ -z "$identity" ] || exit 1
            break
            ;;
        *) exit 1 ;;
    esac
done
