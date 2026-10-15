if [[ -r "${WORKPANE_USER_ZDOTDIR}/.zshrc" ]]; then
    ZDOTDIR="${WORKPANE_USER_ZDOTDIR}"
    source "${WORKPANE_USER_ZDOTDIR}/.zshrc"
fi

unsetopt SHARE_HISTORY INC_APPEND_HISTORY_TIME
setopt INC_APPEND_HISTORY
WORKPANE_READER_HISTORY="${HISTFILE}"

if [[ "${WORKPANE_READER_HISTORY}" == "${WORKPANE_ZDOTDIR}/.zsh_history" ]]; then
    WORKPANE_READER_HISTORY="${WORKPANE_USER_ZDOTDIR}/.zsh_history"
fi

fc -p "${WORKPANE_HISTORY_FILE}" 10000 10000

if [[ ! -s "${WORKPANE_HISTORY_FILE}" && -r "${WORKPANE_READER_HISTORY}" ]]; then
    fc -R "${WORKPANE_READER_HISTORY}"
fi

function workpane_directory() {
    printf '\033]7;file://localhost%s\007' "$PWD"
}

autoload -Uz add-zsh-hook
add-zsh-hook precmd workpane_directory

if [[ "${WORKPANE_USER_ZDOTDIR}" == "${HOME}" ]]; then
    unset ZDOTDIR
else
    ZDOTDIR="${WORKPANE_USER_ZDOTDIR}"
fi

unset WORKPANE_HISTORY_FILE WORKPANE_READER_HISTORY WORKPANE_USER_ZDOTDIR WORKPANE_ZDOTDIR
