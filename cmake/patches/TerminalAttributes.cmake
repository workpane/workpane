# Teaches the terminal library faint text and the hyperlinks of OSC 8, which it parses but never keeps with the cells they cover.
# Faint is set by SGR 2 and cleared with bold by SGR 22, and a link is a number the product gives the pen, carried by every cell written with it.
# The configure step fails when a passage this patch replaces is missing, so a new pin of the library cannot skip it silently, and a passage already replaced is left as it is.
function(replace_passage file anchor replacement)
    file(READ "${SOURCE}/${file}" content)
    string(FIND "${content}" "${replacement}" patched)
    string(FIND "${content}" "${anchor}" found)

    if(NOT patched EQUAL -1)
        return()
    endif()

    if(found EQUAL -1)
        message(FATAL_ERROR "The terminal library no longer holds the passage the attributes patch replaces in \"${file}\": ${anchor}")
    endif()

    string(REPLACE "${anchor}" "${replacement}" content "${content}")
    file(WRITE "${SOURCE}/${file}" "${content}")
endfunction()

replace_passage(include/vterm.h [=[
  VTERM_ATTR_BASELINE,   // number: 73, 74, 75
]=] [=[
  VTERM_ATTR_BASELINE,   // number: 73, 74, 75
  VTERM_ATTR_FAINT,      // bool:   2, 22
  VTERM_ATTR_LINK,       // number: OSC 8
]=])

replace_passage(include/vterm.h [=[
    unsigned int baseline  : 2;
} VTermScreenCellAttrs;
]=] [=[
    unsigned int baseline  : 2;
    unsigned int faint     : 1;
    unsigned int link      : 16;
} VTermScreenCellAttrs;
]=])

replace_passage(include/vterm.h [=[
  VTERM_ATTR_BASELINE_MASK   = 1 << 11,
]=] [=[
  VTERM_ATTR_BASELINE_MASK   = 1 << 11,
  VTERM_ATTR_FAINT_MASK      = 1 << 12,
  VTERM_ATTR_LINK_MASK       = 1 << 13,
]=])

replace_passage(include/vterm.h [=[
  VTERM_ALL_ATTRS_MASK = (1 << 12) - 1
]=] [=[
  VTERM_ALL_ATTRS_MASK = (1 << 14) - 1
]=])

replace_passage(include/vterm.h [=[
int  vterm_state_get_penattr(const VTermState *state, VTermAttr attr, VTermValue *val);
]=] [=[
int  vterm_state_get_penattr(const VTermState *state, VTermAttr attr, VTermValue *val);
void vterm_state_set_link(VTermState *state, int link);
]=])

replace_passage(src/vterm_internal.h [=[
  unsigned int baseline:2;
};
]=] [=[
  unsigned int baseline:2;
  unsigned int faint:1;
  unsigned int link:16;
};
]=])

replace_passage(src/vterm.c [=[
    case VTERM_ATTR_BASELINE:   return VTERM_VALUETYPE_INT;
]=] [=[
    case VTERM_ATTR_BASELINE:   return VTERM_VALUETYPE_INT;
    case VTERM_ATTR_FAINT:      return VTERM_VALUETYPE_BOOL;
    case VTERM_ATTR_LINK:       return VTERM_VALUETYPE_INT;
]=])

replace_passage(src/pen.c [=[
  state->pen.baseline = 0;  setpenattr_int (state, VTERM_ATTR_BASELINE, 0);
]=] [=[
  state->pen.baseline = 0;  setpenattr_int (state, VTERM_ATTR_BASELINE, 0);
  state->pen.faint = 0;     setpenattr_bool(state, VTERM_ATTR_FAINT, 0);
]=])

replace_passage(src/pen.c [=[
    setpenattr_int (state, VTERM_ATTR_BASELINE,  state->pen.baseline);
]=] [=[
    setpenattr_int (state, VTERM_ATTR_BASELINE,  state->pen.baseline);
    setpenattr_bool(state, VTERM_ATTR_FAINT,     state->pen.faint);
    setpenattr_int (state, VTERM_ATTR_LINK,      state->pen.link);
]=])

replace_passage(src/pen.c [=[
    case 3: // Italic on
      state->pen.italic = 1;
]=] [=[
    case 2: // Faint on
      state->pen.faint = 1;
      setpenattr_bool(state, VTERM_ATTR_FAINT, 1);
      break;

    case 3: // Italic on
      state->pen.italic = 1;
]=])

replace_passage(src/pen.c [=[
    case 22: // Bold off
      state->pen.bold = 0;
      setpenattr_bool(state, VTERM_ATTR_BOLD, 0);
]=] [=[
    case 22: // Bold and faint off
      state->pen.bold = 0;
      setpenattr_bool(state, VTERM_ATTR_BOLD, 0);
      state->pen.faint = 0;
      setpenattr_bool(state, VTERM_ATTR_FAINT, 0);
]=])

replace_passage(src/state.c [=[
    (*state->callbacks->initpen)(state->cbdata);

  vterm_state_resetpen(state);
]=] [=[
    (*state->callbacks->initpen)(state->cbdata);

  vterm_state_resetpen(state);
  vterm_state_set_link(state, 0);
]=])

replace_passage(src/pen.c [=[
  if(state->pen.bold)
    args[argi++] = 1;
]=] [=[
  if(state->pen.bold)
    args[argi++] = 1;

  if(state->pen.faint)
    args[argi++] = 2;
]=])

replace_passage(src/pen.c [=[
  case VTERM_ATTR_BASELINE:
    val->number = state->pen.baseline;
    return 1;
]=] [=[
  case VTERM_ATTR_BASELINE:
    val->number = state->pen.baseline;
    return 1;

  case VTERM_ATTR_FAINT:
    val->boolean = state->pen.faint;
    return 1;

  case VTERM_ATTR_LINK:
    val->number = state->pen.link;
    return 1;
]=])

file(READ "${SOURCE}/src/pen.c" pen)
string(FIND "${pen}" "void vterm_state_set_link(VTermState *state, int link)\n{" defined)

if(defined EQUAL -1)
    file(APPEND "${SOURCE}/src/pen.c" [=[

void vterm_state_set_link(VTermState *state, int link)
{
  state->pen.link = link;
  setpenattr_int(state, VTERM_ATTR_LINK, link);
}
]=])
endif()

replace_passage(src/screen.c [=[
  unsigned int baseline  : 2;

  /* Extra state storage that isn't strictly pen-related */
]=] [=[
  unsigned int baseline  : 2;
  unsigned int faint     : 1;
  unsigned int link      : 16;

  /* Extra state storage that isn't strictly pen-related */
]=])

replace_passage(src/screen.c [=[
  case VTERM_ATTR_BASELINE:
    screen->pen.baseline = val->number;
    return 1;
]=] [=[
  case VTERM_ATTR_BASELINE:
    screen->pen.baseline = val->number;
    return 1;
  case VTERM_ATTR_FAINT:
    screen->pen.faint = val->boolean;
    return 1;
  case VTERM_ATTR_LINK:
    screen->pen.link = val->number;
    return 1;
]=])

replace_passage(src/screen.c [=[
        dst->pen.baseline  = src->attrs.baseline;
]=] [=[
        dst->pen.baseline  = src->attrs.baseline;
        dst->pen.faint     = src->attrs.faint;
        dst->pen.link      = src->attrs.link;
]=])

replace_passage(src/screen.c [=[
  cell->attrs.baseline  = intcell->pen.baseline;
]=] [=[
  cell->attrs.baseline  = intcell->pen.baseline;
  cell->attrs.faint     = intcell->pen.faint;
  cell->attrs.link      = intcell->pen.link;
]=])

replace_passage(src/screen.c [=[
  if((attrs & VTERM_ATTR_BASELINE_MASK)    && (a->pen.baseline != b->pen.baseline))
    return 1;
]=] [=[
  if((attrs & VTERM_ATTR_BASELINE_MASK)    && (a->pen.baseline != b->pen.baseline))
    return 1;
  if((attrs & VTERM_ATTR_FAINT_MASK)       && (a->pen.faint != b->pen.faint))
    return 1;
  if((attrs & VTERM_ATTR_LINK_MASK)        && (a->pen.link != b->pen.link))
    return 1;
]=])
