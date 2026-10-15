stage=$1
destination=$2
expected=$3
platform=$4
payload="$stage/payload"
previous="$stage/previous"
umask 077

if [ -z "$expected" ]; then
    [ ! -e "$destination" ] && [ ! -L "$destination" ] || exit 10
else
    [ -f "$destination" ] || exit 10
    dd if="$destination" of="$previous" bs=32768 count=129 2>/dev/null || exit 11

    if [ "$platform" = macos ]; then
        size=$(stat -f %z "$previous") || exit 11
        digest=$(/usr/bin/shasum -a 256 < "$previous") || exit 11
        mode=$(stat -f %Lp "$destination") || exit 11
    else
        size=$(stat -c %s "$previous") || exit 11
        digest=$(sha256sum < "$previous") || exit 11
        mode=$(stat -c %a "$destination") || exit 11
    fi

    [ "$size" -le 4194304 ] && [ "${digest%% *}" = "$expected" ] || exit 10
    chmod "$mode" "$payload" || exit 11
    cmp -s "$previous" "$destination" || exit 10
fi

if [ "$platform" = macos ]; then
    if [ -z "$expected" ]; then
        /usr/bin/osascript -l JavaScript -e 'ObjC.bindFunction("link", ["int", ["char *", "char *"]]); function run(values) { if ($.link(values[0], values[1]) !== 0) { throw new Error("The new file could not be published exclusively") } }' -- "$payload" "$destination" >/dev/null 2>&1 || exit 10
    else
        /usr/bin/osascript -l JavaScript -e 'ObjC.bindFunction("rename", ["int", ["char *", "char *"]]); function run(values) { if ($.rename(values[0], values[1]) !== 0) { throw new Error("The file could not be replaced") } }' -- "$payload" "$destination" >/dev/null 2>&1 || exit 11
    fi
else
    if [ -z "$expected" ]; then
        ln -T "$payload" "$destination" || exit 10
    else
        mv -fT "$payload" "$destination" || exit 11
    fi
fi
