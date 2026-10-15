if [[ -r "${WORKPANE_USER_ZDOTDIR}/.zprofile" ]]; then
    ZDOTDIR="${WORKPANE_USER_ZDOTDIR}"
    source "${WORKPANE_USER_ZDOTDIR}/.zprofile"
fi

ZDOTDIR="${WORKPANE_ZDOTDIR}"
