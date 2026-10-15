# Lets the product take part in the input methods of the system, which the window library leaves without a caret or a composition to show.
# On macOS the text being composed is handed to a receiver, the candidate window opens at the caret the product gives, and a key pressed while composing belongs to the input method.
# On X11 the input context of a window is answered, so the product tells the input method where the caret stands.
# The configure step fails when a passage this patch replaces is missing, so a new pin of the library cannot skip it silently, and a passage already replaced is left as it is.
function(replace_passage file anchor replacement)
    file(READ "${SOURCE}/${file}" content)
    string(FIND "${content}" "${replacement}" patched)
    string(FIND "${content}" "${anchor}" found)

    if(NOT patched EQUAL -1)
        return()
    endif()

    if(found EQUAL -1)
        message(FATAL_ERROR "The window library no longer holds the passage the input method patch replaces in \"${file}\": ${anchor}")
    endif()

    string(REPLACE "${anchor}" "${replacement}" content "${content}")
    file(WRITE "${SOURCE}/${file}" "${content}")
endfunction()

replace_passage(include/GLFW/glfw3native.h [=[
GLFWAPI id glfwGetCocoaView(GLFWwindow* window);
#endif
]=] [=[
GLFWAPI id glfwGetCocoaView(GLFWwindow* window);

/*! @brief Hands the text an input method composes in the window to a receiver, with the byte offset of its caret, and an empty text when composing ends.
 */
GLFWAPI void glfwSetCocoaCompositionCallback(GLFWwindow* window, void (*callback)(void* receiver, const char* text, int caret), void* receiver);

/*! @brief Places the caret of the focused text in content coordinates, so the candidate window opens beside it, and a caret that is not visible ends any composition.
 */
GLFWAPI void glfwSetCocoaTextCursor(GLFWwindow* window, int visible, double x, double y, double width, double height);
#endif
]=])

replace_passage(include/GLFW/glfw3native.h [=[
GLFWAPI Window glfwGetX11Window(GLFWwindow* window);
]=] [=[
GLFWAPI Window glfwGetX11Window(GLFWwindow* window);

/*! @brief Returns the input context of the window, or `NULL` when no input method is running.
 */
GLFWAPI XIC glfwGetX11InputContext(GLFWwindow* window);
]=])

replace_passage(src/cocoa_platform.h [=[
    double          cursorWarpDeltaX, cursorWarpDeltaY;
} _GLFWwindowNS;
]=] [=[
    double          cursorWarpDeltaX, cursorWarpDeltaY;

    // The caret of the focused text and the receiver of the text being composed
    double          textCursor[4];
    void            (*composition)(void* receiver, const char* text, int caret);
    void*           compositionReceiver;
} _GLFWwindowNS;
]=])

replace_passage(src/cocoa_window.m [=[
@interface GLFWContentView : NSView <NSTextInputClient>
]=] [=[
// Tells the receiver of the window what is being composed and where its caret stands in bytes
//
static void notifyComposition(_GLFWwindow* window, NSString* text, NSUInteger caret)
{
    if (!window->ns.composition)
        return;

    NSMutableData* whole = [[text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:YES] mutableCopy];
    NSData* before = [[text substringToIndex:MIN(caret, [text length])] dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:YES];
    [whole appendBytes:"" length:1];
    window->ns.composition(window->ns.compositionReceiver, [whole bytes], (int) [before length]);
    [whole release];
}

@interface GLFWContentView : NSView <NSTextInputClient>
]=])

replace_passage(src/cocoa_window.m [=[
    _glfwInputKey(window, key, [event keyCode], GLFW_PRESS, mods);

    [self interpretKeyEvents:@[event]];
]=] [=[
    if (![self hasMarkedText])
        _glfwInputKey(window, key, [event keyCode], GLFW_PRESS, mods);

    [self interpretKeyEvents:@[event]];
]=])

replace_passage(src/cocoa_window.m [=[
        markedText = [[NSMutableAttributedString alloc] initWithString:string];
}

- (void)unmarkText
{
    [[markedText mutableString] setString:@""];
}
]=] [=[
        markedText = [[NSMutableAttributedString alloc] initWithString:string];

    notifyComposition(window, [markedText string], selectedRange.location);
}

- (void)unmarkText
{
    [[markedText mutableString] setString:@""];
    notifyComposition(window, @"", 0);
}
]=])

replace_passage(src/cocoa_window.m [=[
    const NSRect frame = [window->ns.view frame];
    return NSMakeRect(frame.origin.x, frame.origin.y, 0.0, 0.0);
}
]=] [=[
    const NSRect frame = [window->ns.view frame];
    const NSRect caret = NSMakeRect(window->ns.textCursor[0], frame.size.height - window->ns.textCursor[1] - window->ns.textCursor[3], window->ns.textCursor[2], window->ns.textCursor[3]);
    return [window->ns.object convertRectToScreen:[window->ns.view convertRect:caret toView:nil]];
}
]=])

replace_passage(src/cocoa_window.m [=[
- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
]=] [=[
- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
    if ([self hasMarkedText])
        [self unmarkText];

]=])

replace_passage(src/cocoa_window.m [=[
    return window->ns.view;
}
]=] [=[
    return window->ns.view;
}

GLFWAPI void glfwSetCocoaCompositionCallback(GLFWwindow* handle, void (*callback)(void* receiver, const char* text, int caret), void* receiver)
{
    _GLFW_REQUIRE_INIT();

    if (_glfw.platform.platformID != GLFW_PLATFORM_COCOA)
    {
        _glfwInputError(GLFW_PLATFORM_UNAVAILABLE, "Cocoa: Platform not initialized");
        return;
    }

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    window->ns.composition = callback;
    window->ns.compositionReceiver = receiver;
}

GLFWAPI void glfwSetCocoaTextCursor(GLFWwindow* handle, int visible, double x, double y, double width, double height)
{
    _GLFW_REQUIRE_INIT();

    if (_glfw.platform.platformID != GLFW_PLATFORM_COCOA)
    {
        _glfwInputError(GLFW_PLATFORM_UNAVAILABLE, "Cocoa: Platform not initialized");
        return;
    }

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    window->ns.textCursor[0] = x;
    window->ns.textCursor[1] = y;
    window->ns.textCursor[2] = width;
    window->ns.textCursor[3] = height;

    if (!visible && [window->ns.view hasMarkedText])
    {
        [[window->ns.view inputContext] discardMarkedText];
        [window->ns.view unmarkText];
    }
}
]=])

replace_passage(src/x11_window.c [=[
    return window->x11.handle;
}
]=] [=[
    return window->x11.handle;
}

GLFWAPI XIC glfwGetX11InputContext(GLFWwindow* handle)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (_glfw.platform.platformID != GLFW_PLATFORM_X11)
    {
        _glfwInputError(GLFW_PLATFORM_UNAVAILABLE, "X11: Platform not initialized");
        return NULL;
    }

    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    return window->x11.ic;
}
]=])
