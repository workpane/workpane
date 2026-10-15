# How the interface looks and behaves

Whatever the platform, the interface you build is clean, modern, consistent and easy to use. Beautiful is not decoration: it is clarity, rhythm and care for every state.

## Design system first

- Use the design system, component library and tokens the project already has. Never hard-code a color, a font, a size or a spacing value in a component: name the token or the theme role instead, so a change lands in one place and dark mode works.
- When the project has no design system, establish a small one before building screens: a spacing scale such as 4, 8, 12, 16, 24, 32 and 48, a type scale with three to five sizes and two weights, a neutral palette with one accent color and semantic colors for success, warning, danger and information, a radius and an elevation scale.
- Respect the platform: Human Interface Guidelines on Apple platforms, Material Design on Android, the conventions of the web for browsers. A native user should feel at home.

## Layout

- Align everything to a grid and to the spacing scale. Group related things with proximity and separate groups with space rather than with lines and boxes.
- One primary action per screen, visually dominant. Secondary actions are quieter. Destructive actions are clearly marked and ask for confirmation.
- Typography carries hierarchy: a clear title, readable body text at a comfortable line length, and muted secondary text.
- Design for the smallest supported screen first and grow from there. Nothing overflows horizontally, long text wraps or truncates with intent, and touch targets are at least 44 by 44 points on touch screens.

## Every state is designed

- Loading shows progress or a skeleton in place of the content, never a frozen screen.
- Empty states explain what belongs there and offer the action that fills it.
- Errors say what happened in human words and what the user can do next, next to the place where it happened. Never show a stack trace or a raw error code to a user.
- Disabled, focused, hovered, pressed, selected and offline states are all visible and distinct.
- Long lists are paginated or virtualized. Optimistic updates roll back visibly when the server refuses them.

## Accessibility is part of done

- Every interactive element is reachable and operable with the keyboard or the accessibility services of the platform, with a visible focus indicator.
- Every image and icon that carries meaning has a text alternative, and decorative ones are hidden from assistive technology.
- Text contrast meets WCAG AA: 4.5 to 1 for body text and 3 to 1 for large text and interface elements. Color never carries meaning alone.
- Text scales with the settings of the user without breaking the layout. Motion respects the reduced motion preference.
- Labels are attached to their fields, and the reading order matches the visual order.

## Text and localization

- Every text the user sees comes from the localization resources of the project, never hard-coded in a component.
- Dates, numbers, currencies and plurals are formatted for the locale of the user.
- Layouts tolerate text that is 40 percent longer than English and right-to-left scripts when the product supports them.
