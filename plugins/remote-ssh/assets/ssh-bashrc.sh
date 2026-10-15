if [ "$WORKPANE_PLATFORM" = macos ]; then
    if [ -r /etc/profile ]; then
        source /etc/profile
    fi

    for profile in "$HOME/.bash_profile" "$HOME/.bash_login" "$HOME/.profile"; do
        if [ -r "$profile" ]; then
            source "$profile"
            break
        fi
    done
else
    if [ -r "$HOME/.bashrc" ]; then
        source "$HOME/.bashrc"
    fi
fi

WORKPANE_READER_HISTORY=$HISTFILE
HISTFILE=$WORKPANE_HISTORY_FILE
HISTSIZE=10000
HISTFILESIZE=10000

if [ ! -s "$HISTFILE" ] && [ -r "$WORKPANE_READER_HISTORY" ]; then
    history -r "$WORKPANE_READER_HISTORY"
fi

workpane_reader_prompts=("${PROMPT_COMMAND[@]}")

function workpane_prompt() {
    local status=$?
    local command
    printf '\033]7;file://localhost%s\007' "$PWD"
    history -a

    for command in "${workpane_reader_prompts[@]}"; do
        eval "$command"
    done

    return "$status"
}

PROMPT_COMMAND=workpane_prompt
unset WORKPANE_HISTORY_FILE WORKPANE_READER_HISTORY WORKPANE_PLATFORM
