shell=$1
folder=$2
platform=$3
HISTFILE="$folder/commands.history"
export HISTFILE

case "${shell##*/}" in
    zsh)
        WORKPANE_USER_ZDOTDIR=${ZDOTDIR:-$HOME}
        WORKPANE_ZDOTDIR=$folder
        WORKPANE_HISTORY_FILE=$HISTFILE
        ZDOTDIR=$folder
        export WORKPANE_USER_ZDOTDIR WORKPANE_ZDOTDIR WORKPANE_HISTORY_FILE ZDOTDIR

        if [ "$platform" = macos ]; then
            exec "$shell" -l
        fi

        exec "$shell" -i
        ;;
    bash)
        WORKPANE_HISTORY_FILE=$HISTFILE
        WORKPANE_PLATFORM=$platform
        export WORKPANE_HISTORY_FILE WORKPANE_PLATFORM
        exec "$shell" --rcfile "$folder/bashrc" -i
        ;;
    *)
        if [ "$platform" = macos ]; then
            exec "$shell" -l
        fi

        exec "$shell" -i
        ;;
esac
