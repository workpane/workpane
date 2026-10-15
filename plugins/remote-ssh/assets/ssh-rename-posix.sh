from=$1
to=$2
platform=$3

if [ "$platform" = macos ]; then
    /usr/bin/osascript -l JavaScript -e 'ObjC.bindFunction("renamex_np", ["int", ["char *", "char *", "unsigned int"]]); function run(values) { if ($.renamex_np(values[0], values[1], 4) !== 0) { throw new Error("The entry could not be moved exclusively") } }' -- "$from" "$to" >/dev/null 2>&1 || exit 11
else
    mv --no-copy -nT -- "$from" "$to" || exit 11
    [ ! -e "$from" ] && [ ! -L "$from" ] || exit 11
fi
